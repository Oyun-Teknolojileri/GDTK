<shader>
	<type name = "fragmentShader" />
	<include name = "vulkanCompatInc.shader" />
	<include name = "ssrCommonInc.shader" />
	<texture slot = "0" name = "s_trace" />
	<texture slot = "1" name = "s_history" />
	<texture slot = "2" name = "s_normalDepth" />
<texture slot = "3" name = "s_hit" />
	<source>
	<!--

precision highp float;
precision highp int;

TK_LOC(2) in vec2 v_texture;

layout (location = 0) out vec4 fragColor;

TK_SAMPLER_BINDING(0) uniform sampler2D s_trace;       // rgb: this frame's reflection, a: its weight.
TK_SAMPLER_BINDING(1) uniform sampler2D s_history;     // rgb: accumulated reflection, a: its weight.
TK_SAMPLER_BINDING(2) uniform sampler2D s_normalDepth; // b: linear depth.
TK_SAMPLER_BINDING(3) uniform sampler2D s_hit;         // r: view depth of the reflected content, 0 on a miss.

// Taps per axis. The trace gives one sample per pixel, so the resolve is what turns the individual
// samples of neighbouring pixels into a single value; three by three is the smallest kernel that
// does that without erasing the contact hardening the per pixel lobe produced.
#define SSR_RESOLVE_TAPS 1

// Width of the kernel at zero roughness, in texels. A rough surface reflects a cone and its samples
// are noisier, so the kernel widens with it: the reflection is low frequency there anyway, and a
// wider average is what keeps the per frame noise out of the accumulation the temporal resolve does.
#define SSR_RESOLVE_ROUGH_SPREAD 2.0

// Extra kernel width while the view moves, in texels. A reprojected reflection follows the surface it
// belongs to, not the reflected content, so the history is stale exactly when the camera moves. The
// resolve covers that with its own average instead of with the history.
#define SSR_RESOLVE_MOTION_SPREAD 1.0

// Fraction of the history kept while the view moves.
#define SSR_RESOLVE_MOTION_HISTORY 0.05

float Luminance(vec3 c)
{
	return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

// Bounded version of a sample, used for the average. A single sample that caught a specular
// highlight of the reflected scene is orders of magnitude brighter than its neighbours; averaging it
// in raw makes the reflection pulse. The compression is undone after the average, so a neighbour
// hood of equal samples still averages to exactly that value.
vec3 Compress(vec3 c)
{
	return c / (1.0 + Luminance(c));
}

vec3 Expand(vec3 c)
{
	return c / max(1.0 - Luminance(c), 0.05);
}

void main()
{
	vec2 uv     = v_texture;
	float depth = texture(s_normalDepth, uv).b;

	// Debug views are single samples of the trace, hand them through untouched so they keep reading
	// as data instead of being averaged into a gradient.
	if (ssrPass.flags.x > 0.5)
	{
		fragColor = texture(s_trace, uv);
		return;
	}

	if (depth <= 0.0)
	{
		fragColor = vec4(0.0);
		return;
	}

	float motion    = ssrPass.fades.w;
	float roughness = texture(s_normalDepth, uv).a;
	vec2  texel     = ssrPass.screenParams.xy *
	                  (1.0 + roughness * SSR_RESOLVE_ROUGH_SPREAD + motion * SSR_RESOLVE_MOTION_SPREAD);

	vec4  sum       = vec4(0.0);
	float sumWeight = 0.0;
	float sumKernel = 0.0;
	vec4  minimum   = vec4(1e6);
	vec4  maximum   = vec4(-1e6);

	for (int y = -SSR_RESOLVE_TAPS; y <= SSR_RESOLVE_TAPS; ++y)
	{
		for (int x = -SSR_RESOLVE_TAPS; x <= SSR_RESOLVE_TAPS; ++x)
		{
			vec2 offset = vec2(float(x), float(y));
			vec2 tapUV  = uv + offset * texel;

			vec4  tap      = texture(s_trace, tapUV);
			float tapDepth = texture(s_normalDepth, tapUV).b;

			float kernel = max(0.0, 1.0 - length(offset) * 0.5);
			sumKernel   += kernel;

			if (tapDepth <= 0.0)
			{
				continue;
			}

			// A tap across a silhouette belongs to another surface: its reflection would smear over
			// the edge that surface sits on.
			float agree = 1.0 / (1.0 + abs(tapDepth - depth) * 8.0);
			float w     = kernel * agree;

			sum.rgb   += Compress(tap.rgb) * tap.a * w;
			sum.a     += tap.a * w;
			sumWeight += w;

			minimum = min(minimum, tap);
			maximum = max(maximum, tap);
		}
	}

	// Premultiplied average with dilation: dividing the accumulated weight by the whole kernel
	// spreads the weight of a hit outwards, so a reflection fades into its surroundings instead of
	// ending on the silhouette of the geometry that produced it. The sum is in the compressed space
	// above, so it comes back to linear here.
	float weight  = sumWeight > 0.0 ? sum.a / sumKernel : 0.0;
	vec3  color   = sum.a > 0.0 ? Expand(sum.rgb / sum.a) : vec3(0.0);
	vec4  current = vec4(color, clamp(weight, 0.0, 1.0));

	if (sumWeight <= 0.0)
	{
		minimum = current;
		maximum = current;
	}

	// Reprojection: where the reflected content was on screen in the previous frame. A reflection moves
	// with the geometry it shows, not with the surface it lands on, so the history is followed through
	// the depth of the hit: the point at that depth along this pixel's ray is what the reflection is
	// showing, and that point is what moved. Reprojecting the surface instead leaves the history
	// behind the reflection whenever the camera moves, and the mismatch reads as shimmer.
	float hitDepth = texture(s_hit, uv).r;
	float followDepth = hitDepth > 0.0 ? hitDepth : depth;

	vec3 viewPos  = SsrViewPos(uv, followDepth);
	vec4 worldPos = ssrPass.inverseView * vec4(viewPos, 1.0);
	vec4 prevClip = ssrPass.prevViewProjection * worldPos;
	vec2 prevUV   = prevClip.xy / max(prevClip.w, 1e-6) * 0.5 + 0.5;
#ifdef VULKAN
	prevUV.y = 1.0 - prevUV.y;
#endif
	bool valid = prevClip.w > 0.0 && prevUV.x >= 0.0 && prevUV.x <= 1.0 && prevUV.y >= 0.0 && prevUV.y <= 1.0;

	// The history is never read on the frame it is reset: its content is whatever the target held
	// before, and a NaN in it would survive a mix with a zero weight (a value multiplied by zero is
	// still a value). Starting from the current frame keeps that frame exact.
	vec4 previous = current;
	if (ssrPass.temporal.y < 0.5 && valid)
	{
		previous = texture(s_history, clamp(prevUV, vec2(0.0), vec2(1.0)));

		// Neighbourhood clamp: a reprojected value outside the range this frame produced is a stale
		// reflection of something the camera no longer sees, and blending it in would ghost.
		previous = clamp(previous, minimum, maximum);
	}

	// A pixel that flips between hit and miss has nothing to blend with: the old state would stay on
	// screen as a shadow until the accumulation caught up with the new one - a miss where the camera
	// now sees a reflection (or the reverse) reads as a dark patch that fades out over a second. The
	// history is trusted only as far as the two frames agree that this pixel carries a reflection.
	float agreement = 1.0 - abs(current.a - previous.a);

	float blend = ssrPass.temporal.x * (valid ? 1.0 : 0.0) * agreement;
	blend      *= 1.0 - motion * SSR_RESOLVE_MOTION_HISTORY;
	if (ssrPass.temporal.y > 0.5)
	{
		// History dropped: a camera cut, a resize or the first frame of the pass. The trace is all
		// this frame has.
		blend = 0.0;
	}

	fragColor = mix(current, previous, clamp(blend, 0.0, 0.98));
}

	-->
	</source>
</shader>
