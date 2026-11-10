<shader>
	<type name = "fragmentShader" />
	<include name = "vulkanCompatInc.shader" />
	<include name = "ssrPassDataInc.shader" />
	<texture slot = "0" name = "s_trace" />
	<texture slot = "1" name = "s_sceneColor" />
	<source>
	<!--

precision highp float;
precision highp int;

TK_LOC(2) in vec2 v_texture;

layout (location = 0) out vec4 fragColor;

TK_SAMPLER_BINDING(0) uniform sampler2D s_trace;      // rgb: accumulated reflection, a: its weight.
TK_SAMPLER_BINDING(1) uniform sampler2D s_sceneColor; // Scene color before this pass, the fallback.

void main()
{
	vec2 uv    = v_texture;
	vec4 scene = texture(s_sceneColor, uv);
	vec4 accum = texture(s_trace, uv);

	// Debug views are handed through exactly as the trace pass produced them, so they keep reading as
	// data instead of being darkened by the composite weight.
	if (ssrPass.flags.x > 0.5)
	{
		fragColor = vec4(accum.rgb, scene.a);
		return;
	}

	// The weight is the fraction of the pixel the reflection covers: the accumulated confidence and
	// the intensity setting. A miss keeps the forward shaded colour, which already carries the
	// environment probe the scene provides, so screen space can only improve on it, never replace it
	// with nothing.
	float weight = clamp(accum.a * ssrPass.params.x, 0.0, 1.0);

	fragColor = vec4(mix(scene.rgb, accum.rgb, weight), scene.a);
}

	-->
	</source>
</shader>
