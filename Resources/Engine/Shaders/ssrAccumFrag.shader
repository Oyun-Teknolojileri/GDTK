<shader>
	<type name = "fragmentShader" />
	<include name = "vulkanCompatInc.shader" />
	<include name = "ssrPassDataInc.shader" />
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

	// Resolve this frame's reflection in screen space. A screen space reflection stops where the
	// geometry it can see stops, and the environment reflection takes over there with a different
	// colour, so the reflection ends in a hard edge. Averaging premultiplied by the weight spreads
	// that weight outwards and turns the edge into a gradient, while a pixel whose taps all carry
	// full weight keeps full strength.
	float roughness = texture(s_normalDepth, uv).a;
	vec2  step      = ssrPass.screenParams.xy * (1.0 + roughness * 2.0);

	vec4  sum       = vec4(0.0);
	float sumKernel = 0.0;
	float minW = 1e9, maxW = -1e9;
	vec3  minC = vec3(1e9), maxC = vec3(-1e9);

	for (int y = -SSR_FILTER_TAPS; y <= SSR_FILTER_TAPS; ++y)
	{
		for (int x = -SSR_FILTER_TAPS; x <= SSR_FILTER_TAPS; ++x)
		{
			vec2  offset = vec2(float(x), float(y));
			float kernel = max(0.0, 1.0 - length(offset) * 0.5);
			vec4  tap    = texture(s_trace, uv + offset * step);

			// Premultiplied so an empty tap dilutes the reflection instead of darkening it.
			sum       += vec4(tap.rgb * tap.a, tap.a) * kernel;
			sumKernel += kernel;

			// Range the history is clamped into: a pixel this frame's reflection disagrees with can
			// not be carried over, and a pixel the reflection has just appeared in is pulled to it.
			minW = min(minW, tap.a); maxW = max(maxW, tap.a);
			minC = min(minC, tap.rgb); maxC = max(maxC, tap.rgb);
		}
	}

	vec4  current = sum / max(sumKernel, 0.0001);
	vec4  result  = current;

	// Temporal accumulation. The reflection is a single sample per pixel per frame, so without
	// reusing the last frames the estimate is resampled from scratch every time the camera or the
	// geometry moves and the reflection shimmers. The previous result is reprojected with the
	// previous frame's view projection and this frame's depth, which is exact for geometry that did
	// not move, and is clamped into the range this frame's reflection spans so a surface that has
	// just appeared, or one the camera has just uncovered, takes the new value instead of trailing
	// the old one.
	if (ssrPass.temporal.y > 0.5 && depth > 0.0)
	{
		vec3  viewPos  = ReconstructViewPos(uv, depth);
		vec4  prevClip = ssrPass.prevReprojection * vec4(viewPos, 1.0);

		if (abs(prevClip.w) > 0.00001)
		{
			vec2 prevUv = prevClip.xy / prevClip.w * 0.5 + 0.5;
#ifdef VULKAN
			prevUv.y = 1.0 - prevUv.y;
#endif
			if (prevUv.x >= 0.0 && prevUv.x <= 1.0 && prevUv.y >= 0.0 && prevUv.y <= 1.0)
			{
				vec4 history = texture(s_history, prevUv);
				history.rgb  = clamp(history.rgb, minC, maxC);
				history.a    = clamp(history.a, minW, maxW);

				result       = mix(current, history, ssrPass.temporal.x);
			}
		}
	}

	fragColor = result;
}

	-->
	</source>
</shader>
