<shader>
	<type name = "includeShader" />
	<include name = "drawDataInc.shader" />
	<source>
	<!--
	#ifndef AO_SHADER
	#define AO_SHADER

	uniform sampler2D s_texture5; // ambient occlusion.
	uniform bool ambientOcculutionInUse;

	float AmbientOcclusion()
	{
		if (IsAmbientOcculusionInUse())
		{
			vec2 coords = gl_FragCoord.xy / vec2(textureSize(s_texture5, 0));
			coords = vec2(coords.x, coords.y);
			return texture(s_texture5, coords).r;
		}
	
		return 1.0;
	}

	#endif
	-->
	</source>
</shader>