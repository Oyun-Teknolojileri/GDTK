<shader>
	<type name = "includeShader" />
	<include name = "perDrawDataInc.shader" />
	<source>
	<!--
#ifndef AO_SHADER
#define AO_SHADER

uniform sampler2D s_texture5; // ambient occlusion.

float AmbientOcclusion()
{
	if (IsAmbientOcculusionInUse())
	{
		vec2 coords = gl_FragCoord.xy / perDraw._viewportSizeAndPad.xy;
		return texture(s_texture5, coords).r;
	}

	return 1.0;
}

#endif
	-->
	</source>
</shader>