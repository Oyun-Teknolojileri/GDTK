<shader>
	<type name = "includeShader" />
	<uniform name = "viewportSize" />
	<source>
	<!--
#ifndef AO_SHADER
#define AO_SHADER

uniform sampler2D s_texture5; // ambient occlusion.
uniform vec2 viewportSize;

float AmbientOcclusion()
{
	if (IsAmbientOcculusionInUse())
	{
		vec2 coords = gl_FragCoord.xy / viewportSize;
		return texture(s_texture5, coords).r;
	}

	return 1.0;
}

#endif
	-->
	</source>
</shader>