<shader>
	<type name = "fragmentShader" />
	<include name = "vulkanCompatInc.shader" />
	<include name = "ssrPassDataInc.shader" />
	<texture slot = "0" name = "s_trace" />
	<texture slot = "1" name = "s_sceneColor" />
	<texture slot = "2" name = "s_coverage" />
	<texture slot = "3" name = "s_normalDepth" />
	<source>
	<!--

precision highp float;
precision highp int;

TK_LOC(2) in vec2 v_texture;

layout (location = 0) out vec4 fragColor;

TK_SAMPLER_BINDING(0) uniform sampler2D s_trace;      // rgb: accumulated reflection, a: its variance.
TK_SAMPLER_BINDING(1) uniform sampler2D s_sceneColor; // Scene color before this pass, the fallback.
TK_SAMPLER_BINDING(2) uniform sampler2D s_coverage;    // a: 1 where this pixel is a reflective surface.
TK_SAMPLER_BINDING(3) uniform sampler2D s_normalDepth; // a: roughness of the surface under the pixel.

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

	// How much of the pixel the reflection may replace.
	//
	// Mixing towards the reflection replaces the *whole* shaded pixel, diffuse albedo included, because the
	// g buffer carries no albedo and no metalness to separate the two. Taken at full strength on every
	// reflective pixel that paints a rough wall or a floor with whatever the sky and the screen happen to
	// show, which reads as "the albedo is gone".
	//
	// So the share is scaled by what a surface of this roughness reflects: a mirror hands the pixel over and
	// the rougher the surface, the more of its own shaded colour survives. The floor is what a dielectric
	// reflects at normal incidence. This is not a Fresnel term, which would need the albedo to be worth the
	// name, and it is not metalness aware either: it is what a roughness only g buffer allows.
	float roughness     = texture(s_normalDepth, uv).a;
	float specularShare = clamp(0.04 + 0.96 * pow(1.0 - clamp(roughness, 0.0, 1.0), 3.0), 0.0, 1.0);
	float coverage      = texture(s_coverage, uv).a;

	float weight = clamp(coverage * specularShare * ssrPass.params.x, 0.0, 1.0);

	fragColor = vec4(mix(scene.rgb, accum.rgb, weight), scene.a);
}

	-->
	</source>
</shader>
