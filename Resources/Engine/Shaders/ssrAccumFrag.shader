<shader>
	<type name = "fragmentShader" />
	<include name = "vulkanCompatInc.shader" />
	<include name = "ssrPassDataInc.shader" />
	<include name = "temporalInc.shader" />
	<texture slot = "0" name = "s_trace" />
	<texture slot = "1" name = "s_history" />
	<texture slot = "2" name = "s_normalDepth" />
	<source>
	<!--

precision highp float;
precision lowp int;

TK_LOC(2) in vec2 v_texture;

layout (location = 0) out vec4 fragColor;

TK_SAMPLER_BINDING(0) uniform sampler2D s_trace;       // rgb: this frame's reflection, a: its weight.
TK_SAMPLER_BINDING(1) uniform sampler2D s_history;     // the same, accumulated over the last frames.
TK_SAMPLER_BINDING(2) uniform sampler2D s_normalDepth; // b: linear view depth.

// Taps per axis of the resolve kernel. It averages the reflection premultiplied by its weight, which
// is what spreads the weight past the silhouettes the trace can not see through and fills the pixels
// the trace left empty.
#define SSR_FILTER_TAPS 1

// How many standard deviations of the resolve kernel a history sample may sit away from this frame's
// mean before it is pulled back to the edge of that range. A reflection is view dependent: the value
// at a fixed point changes as soon as the camera moves, so the neighbourhood is what says how much it
// is allowed to have changed this frame.
#define SSR_TEMPORAL_SIGMA 1.0

// View space position of a pixel from its positive linear view depth. Mirrors SsrPass's trace shader.
vec3 ReconstructViewPos(vec2 uv, float linearDepth)
{
	vec2 ndc = uv * 2.0 - 1.0;
#ifdef VULKAN
	ndc.y = -ndc.y;
#endif
	vec4 viewPos = ssrPass.inverseProjection * vec4(ndc, 0.0, 1.0);
	vec3 viewDir = viewPos.xyz / viewPos.w;
	return viewDir * (linearDepth / -viewDir.z);
}

void main()
{
	vec2 uv      = v_texture;
	float depth  = texture(s_normalDepth, uv).b;

	// Resolve this frame's reflection in screen space, and what that is for decides the kernel.
	//
	// It fills the pixels the trace left empty, with the reflection their neighbours hold. A pixel that
	// has weight of its own is therefore averaged over one texel only: a reflection that is already
	// there is not something to spread. This matters most where the reflection is thin. A pole or a
	// wire reflects as a line a few pixels wide, and a kernel five texels wide turned that line into a
	// band of its colour lying across the floor -- the pole appearing to flow over the ground.
	//
	// A pixel with nothing of its own reaches further, and how far follows the roughness, because that
	// is the width the neighbours it borrows from are blurred by. Filling only where there is a hole is
	// also what keeps the reflection from widening past the silhouettes the trace can not see through,
	// which is the halo a wider kernel shows everywhere, and it is what turns the hard edge where a
	// screen space reflection stops into a gradient.
	float roughness = texture(s_normalDepth, uv).a;
	float empty     = texture(s_trace, uv).a <= 0.0 ? 1.0 : 0.0;
	float reach     = mix(1.0, 1.0 + min(roughness, 1.0) * 4.0, empty);
	vec2  step      = ssrPass.screenParams.xy;

	vec4  sum       = vec4(0.0);
	vec4  sumRaw    = vec4(0.0);
	vec4  sumSquare = vec4(0.0);
	float sumKernel = 0.0;

	for (int y = -SSR_FILTER_TAPS; y <= SSR_FILTER_TAPS; ++y)
	{
		for (int x = -SSR_FILTER_TAPS; x <= SSR_FILTER_TAPS; ++x)
		{
			vec2  offset = vec2(float(x), float(y));
			float kernel = max(0.0, 1.0 - length(offset) * 0.5);
			vec4  tap    = texture(s_trace, uv + offset * step);

			// Premultiplied so an empty tap dilutes the reflection instead of darkening it.
			sum       += vec4(tap.rgb * tap.a, tap.a) * kernel;
			sumRaw    += tap * kernel;
			sumSquare += tap * tap * kernel;
			sumKernel += kernel;
		}
	}

	// The reach is filled in, not stretched. Spreading the same nine points further apart is not a wider
	// kernel, it is a sparser one: the gaps between them sample whatever they land on, and around a thin
	// bright reflection -- a lamp, a pole, a wire -- that draws a comb of its colour across the surface
	// instead of filling the hole it was asked to fill. Two rings, at half the reach and at the reach,
	// leave no gap in the disc at any radius. The near ring is always there, because a hole is filled
	// from its neighbours first; only the far ring depends on how far the surface's own cone lets the
	// reflection be borrowed from. A pixel with weight of its own never enters this.
	for (int ring = 0; ring < 2; ++ring)
	{
		float radius = ring == 0 ? max(reach * 0.5, 1.5) : reach;

		if (ring == 1 && radius < 3.0)
		{
			continue;
		}

		for (int k = 0; k < 8; ++k)
		{
			float ang    = float(k) * 0.7853981634;
			vec2  offset = vec2(cos(ang), sin(ang)) * radius * ssrPass.screenParams.xy;
			float kernel = ring == 0 ? 0.75 : 0.5;
			vec4  tap    = texture(s_trace, uv + offset);

			sum       += vec4(tap.rgb * tap.a, tap.a) * kernel;
			sumRaw    += tap * kernel;
			sumSquare += tap * tap * kernel;
			sumKernel += kernel;
		}
	}

	vec4  current = sum / max(sumKernel, 0.0001);
	vec4  result  = current;

	// Temporal accumulation, with the reprojection and the clamp shared with every other pass that
	// reuses its own result (temporalInc.shader). The range is this pass's own: what this frame's
	// resolve kernel looks like around the pixel.
	//
	// A pixel that has no reflection of its own this frame keeps none of the last frames either. That
	// also covers the ground a moving view has just uncovered, where the old value is a reflection of
	// something the camera is no longer looking at.
	if (ssrPass.temporal.y > 0.5 && depth > 0.0 && current.a > 0.0)
	{
		vec2 prevUv;
		if (PreviousFrameUv(ReconstructViewPos(uv, depth), prevUv))
		{
			// Mean and spread of the kernel rather than its extremes: clamping to the extremes of nine
			// taps let a value the view had already left behind survive whenever one neighbour still
			// agreed with it, which is what a trail of the reflection is.
			vec4 mean     = sumRaw / sumKernel;
			vec4 variance = abs(sumSquare / sumKernel - mean * mean);
			vec4 spread   = SSR_TEMPORAL_SIGMA * sqrt(max(variance, vec4(0.0)));

			vec4 history  = ClampToNeighbourhood(texture(s_history, prevUv),
			                                     mean - spread,
			                                     mean + spread);

			result        = mix(current, history, ssrPass.temporal.x);
		}
	}

	fragColor = result;
}

	-->
	</source>
</shader>
