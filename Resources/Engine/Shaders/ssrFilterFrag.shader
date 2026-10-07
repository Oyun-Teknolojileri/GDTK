<shader>
	<type name = "fragmentShader" />
	<include name = "vulkanCompatInc.shader" />
	<include name = "ssrPassDataInc.shader" />
	<texture slot = "0" name = "s_trace" />
	<texture slot = "1" name = "s_sceneColor" />
	<texture slot = "2" name = "s_normalDepth" />
	<source>
	<!--

precision highp float;
precision lowp int;

TK_LOC(2) in vec2 v_texture;

layout (location = 0) out vec4 fragColor;

TK_SAMPLER_BINDING(0) uniform sampler2D s_trace;       // rgb: reflection, a: its weight.
TK_SAMPLER_BINDING(1) uniform sampler2D s_sceneColor;  // Scene color before this pass, the fallback.
TK_SAMPLER_BINDING(2) uniform sampler2D s_normalDepth; // a: roughness.

// Taps per axis. A three by three kernel already covers the row level stripes the hit leaves behind,
// and a wider one costs more than the edge it would soften.
#define SSR_FILTER_TAPS 1

void main()
{
	vec2 uv    = v_texture;
	vec4 scene = texture(s_sceneColor, uv);
	vec4 trace = texture(s_trace, uv);

	// Debug views are handed through exactly as the trace pass produced them, so they keep reading as
	// data instead of being smoothed into a gradient.
	if (ssrPass.flags.x > 0.5)
	{
		fragColor = vec4(trace.rgb, scene.a);
		return;
	}

	// Screen space filter with dilation. A screen space reflection stops where the geometry it can see
	// stops, and the environment reflection takes over there with a different colour, so the reflection
	// ends in a hard edge. Averaging the reflection premultiplied by its weight spreads that weight
	// outwards and turns the edge into a gradient, while a pixel whose taps all carry full weight keeps
	// full strength. The same average is what removes the row level stripes the hit leaves behind, so
	// this pass is the reason the trace itself does not have to blur more than the reflection cone.
	float roughness = texture(s_normalDepth, uv).a;
	vec2  step      = ssrPass.screenParams.xy * (1.0 + roughness * 2.0);

	vec3  sumColor  = vec3(0.0);
	float sumWeight = 0.0;
	float sumKernel = 0.0;

	for (int y = -SSR_FILTER_TAPS; y <= SSR_FILTER_TAPS; ++y)
	{
		for (int x = -SSR_FILTER_TAPS; x <= SSR_FILTER_TAPS; ++x)
		{
			vec2  offset = vec2(float(x), float(y));
			float kernel = max(0.0, 1.0 - length(offset) * 0.5);
			vec4  tap    = texture(s_trace, uv + offset * step);

			sumColor  += tap.rgb * tap.a * kernel;
			sumWeight += tap.a * kernel;
			sumKernel += kernel;
		}
	}

	float weight = sumWeight / sumKernel;
	vec3  color  = sumWeight > 0.0 ? sumColor / sumWeight : vec3(0.0);

	fragColor = vec4(mix(scene.rgb, color, clamp(weight, 0.0, 1.0)), scene.a);
}

	-->
	</source>
</shader>
