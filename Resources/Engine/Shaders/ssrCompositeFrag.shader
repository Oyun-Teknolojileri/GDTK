<shader>
	<type name = "fragmentShader" />
	<include name = "vulkanCompatInc.shader" />
	<include name = "ssrPassDataInc.shader" />
	<texture slot = "0" name = "s_trace" />
	<texture slot = "1" name = "s_sceneColor" />
	<texture slot = "2" name = "s_coverage" />
	<source>
	<!--

precision highp float;
precision highp int;

TK_LOC(2) in vec2 v_texture;

layout (location = 0) out vec4 fragColor;

TK_SAMPLER_BINDING(0) uniform sampler2D s_trace;      // rgb: accumulated reflection, a: its variance.
TK_SAMPLER_BINDING(1) uniform sampler2D s_sceneColor; // Scene color before this pass, the fallback.
TK_SAMPLER_BINDING(2) uniform sampler2D s_coverage;   // a: 1 where this pixel is a reflective surface.

void main()
{
	vec2 uv    = v_texture;
	vec4 scene = texture(s_sceneColor, uv);
	vec4 accum = texture(s_trace, uv);

	// Debug views are handed through exactly as the trace pass produced them, so they keep reading as
	// data instead of being weighted into the frame.
	if (ssrPass.flags.x > 0.5)
	{
		fragColor = vec4(accum.rgb, scene.a);
		return;
	}

	// The reflection this pass carries is complete: where the march found nothing, the environment cubemap
	// stands in. So the only thing that decides whether a pixel takes it is whether the pixel is
	// reflective at all - a miss no longer means "keep whatever the previous frame had".
	float coverage = texture(s_coverage, uv).a;
	float weight   = clamp(coverage * ssrPass.params.x, 0.0, 1.0);

	fragColor = vec4(mix(scene.rgb, accum.rgb, weight), scene.a);
}

	-->
	</source>
</shader>
