<shader>
	<type name = "includeShader" />
	<include name = "vulkanCompatInc.shader" />
	<include name = "perDrawDataInc.shader" />
	<source>
	<!--
#ifndef AO_SHADER
#define AO_SHADER

TK_SAMPLER_BINDING(5) uniform sampler2D s_texture5; // ambient occlusion.

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