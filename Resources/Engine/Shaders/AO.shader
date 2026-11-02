<shader>
	<type name = "includeShader" />
	<include name = "vulkanCompatInc.shader" />
	<include name = "perDrawDataInc.shader" />
	<texture slot = "5" name = "s_ambientOcclusion" />
	<source>
	<!--
#ifndef AO_SHADER
#define AO_SHADER

TK_SAMPLER_BINDING(5) uniform sampler2D s_ambientOcclusion; // ambient occlusion.

float AmbientOcclusion()
{
	if (IsAmbientOcculusionInUse())
	{
		vec2 coords = gl_FragCoord.xy / perDraw._viewportSizeAndPad.xy;
		float ao    = texture(s_ambientOcclusion, coords).r;

		// The map is the raw unoccluded fraction. The strength decides how much of it the lighting
		// takes: 1 leaves the map as it is, 0 turns the darkening off without a recompile, 2 doubles
		// it. A pass can be entirely correct and still be invisible at 4 percent mean occlusion, so
		// how strongly it is painted on is a separate decision from how it is sampled.
		return clamp(1.0 - (1.0 - ao) * perDraw._drawCommand.global0.w, 0.0, 1.0);
	}

	return 1.0;
}

#endif
	-->
	</source>
</shader>