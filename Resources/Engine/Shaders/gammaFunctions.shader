<shader>
	<type name = "includeShader" />
	<include name = "gammaTonemapFxaaPassDataInc.shader" />
	<source>
	<!--

#ifndef GAMMA_SHADER
#define GAMMA_SHADER

vec3 Gamma(vec3 color)
{
	return pow(color, vec3(1.0/gtf.tonemapParams.y));
}

#endif

	-->
	</source>
</shader>