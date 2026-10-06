<shader>
	<type name = "fragmentShader" />
	<include name = "vulkanCompatInc.shader" />
	<include name = "ssrPassDataInc.shader" />
	<texture slot = "0" name = "s_trace" />
	<texture slot = "1" name = "s_sceneColor" />
	<source>
	<!--

precision highp float;
precision lowp int;

TK_LOC(2) in vec2 v_texture;

layout (location = 0) out vec4 fragColor;

TK_SAMPLER_BINDING(0) uniform sampler2D s_trace;      // rgb: reflection, a: its weight, resolve done.
TK_SAMPLER_BINDING(1) uniform sampler2D s_sceneColor; // Scene color before this pass, the fallback.

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

	// Composite: the weight says how much of the pixel the reflection takes over from, the rest stays
	// the forward shaded colour the sky and the environment volumes provided.
	fragColor = vec4(mix(scene.rgb, trace.rgb, clamp(trace.a, 0.0, 1.0)), scene.a);
}

	-->
	</source>
</shader>
