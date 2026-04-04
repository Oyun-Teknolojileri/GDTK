<shader>
	<type name = "includeShader" />
	<include name = "pbrCommon.shader" />
	<include name = "drawDataInc.shader" />
	<uniform name = "iblRotation" />
	<uniform name = "iblSecondaryRotation" />
	<source>
	<!--

#ifndef IBL_SHADER
#define IBL_SHADER

uniform samplerCube s_texture7; 	// IBL Diffuse Map
uniform samplerCube s_texture15; 	// IBL Pre-Filtered Specular Map
uniform sampler2D s_texture10;		// IBL BRDF Lut

uniform samplerCube s_texture11;	// Secondary IBL Diffuse Map
uniform samplerCube s_texture12;	// Secondary IBL Pre-Filtered Specular Map

uniform mat4 iblRotation;
uniform mat4 iblSecondaryRotation;

// ---------------------------------------------------------------------------
// Filament-style IBL helpers
// ---------------------------------------------------------------------------

// Quadratic fit for roughness-to-LOD mapping
// Filament: perceptualRoughnessToLod
float RoughnessToLod(float roughness, float maxLod)
{
	return maxLod * roughness * (2.0 - roughness);
}

// Specular dominant direction correction
// Filament: getSpecularDominantDirection
vec3 GetSpecularDominantDirection(vec3 n, vec3 r, float roughness)
{
	return mix(r, n, roughness * roughness);
}

// Filament-style specularDFG: pre-integrated environment BRDF
// Uses the DFG LUT to compute the specular contribution factor
// dfg.x = scale, dfg.y = bias → result = mix(dfg.xxx, dfg.yyy, f0)
// which is equivalent to: f0 * dfg.x + dfg.y
vec3 SpecularDFG(vec2 dfg, vec3 f0)
{
	return f0 * dfg.x + dfg.y;
}

// ---------------------------------------------------------------------------
// IBL Diffuse
// Filament approach: Fd = diffuseColor * irradiance * (1.0 - E)
// where E = specularDFG(f0, dfg) is the total specular energy
// ---------------------------------------------------------------------------

vec3 IBLDiffusePBR(vec3 normal, vec3 albedo, float metallic, vec3 E)
{
	vec3 irradiance = vec3(0.0);
	if (IsIBLInUse())
	{
		vec3 diffuseColor = albedo * (1.0 - metallic);
		vec3 iblSamplerVec = (iblRotation * vec4(normal, 0.0)).xyz;
		vec3 iblIrradiance = texture(s_texture7, iblSamplerVec).rgb;
		irradiance = diffuseColor * iblIrradiance * (1.0 - E);
	}

	return irradiance;
}

// ---------------------------------------------------------------------------
// IBL Specular
// Filament approach: Fr = E * prefilteredRadiance * energyCompensation
// Uses specular dominant direction and quadratic LOD mapping
// ---------------------------------------------------------------------------

vec3 IBLSpecularPBR(vec3 normal, vec3 fragToEye, float perceptualRoughness, vec3 E, vec3 energyComp)
{
	vec3 specular = vec3(0.0);
	if (IsIBLInUse())
	{
		vec3 R = reflect(-fragToEye, normal);
		R = GetSpecularDominantDirection(normal, R, perceptualRoughness);
		vec3 iblSamplerVec = (iblRotation * vec4(R, 0.0)).xyz;

		float lod = RoughnessToLod(perceptualRoughness, float(graphicConstants.iblMaxReflectionLod));
		vec3 preFilteredColor = textureLod(s_texture15, iblSamplerVec, lod).rgb;

		specular = E * preFilteredColor;
		specular *= energyComp;
	}

	return specular;
}

// ---------------------------------------------------------------------------
// Secondary IBL helpers (for volume blending)
// ---------------------------------------------------------------------------

vec3 IBLDiffusePBRSecondary(vec3 normal, vec3 albedo, float metallic, vec3 E)
{
	vec3 diffuseColor = albedo * (1.0 - metallic);
	vec3 iblSamplerVec = (iblSecondaryRotation * vec4(normal, 0.0)).xyz;
	vec3 iblIrradiance = texture(s_texture11, iblSamplerVec).rgb;
	return diffuseColor * iblIrradiance * (1.0 - E);
}

vec3 IBLSpecularPBRSecondary(vec3 normal, vec3 fragToEye, float perceptualRoughness, vec3 E, vec3 energyComp)
{
	vec3 R = reflect(-fragToEye, normal);
	R = GetSpecularDominantDirection(normal, R, perceptualRoughness);
	vec3 iblSamplerVec = (iblSecondaryRotation * vec4(R, 0.0)).xyz;

	float lod = RoughnessToLod(perceptualRoughness, float(graphicConstants.iblMaxReflectionLod));
	vec3 preFilteredColor = textureLod(s_texture12, iblSamplerVec, lod).rgb;

	vec3 specular = E * preFilteredColor;
	specular *= energyComp;
	return specular;
}

// ---------------------------------------------------------------------------
// Combined IBL evaluation with per-pixel volume blending
// ---------------------------------------------------------------------------

vec3 IBLPBR(vec3 normal, vec3 fragToEye, vec3 albedo, float metallic, float perceptualRoughness, vec2 dfg, vec3 energyComp, vec3 worldPos)
{
	vec3 f0 = BaseReflectivityPBR(vec3(0.04), albedo, metallic);

	// Compute specular DFG term (Filament: specularDFG)
	vec3 E = SpecularDFG(dfg, f0);

	vec3 Fd = IBLDiffusePBR(normal, albedo, metallic, E);
	vec3 Fr = IBLSpecularPBR(normal, fragToEye, perceptualRoughness, E, energyComp);
	vec3 primary = (Fd + Fr) * GetIBLIntensity();

	float secIntensity = GetSecondaryIBLIntensity();

	if (secIntensity > 0.0)
	{
		float blendFactor = ComputeIBLBlendFactor(worldPos);

		if (blendFactor < 1.0)
		{
			vec3 secFd = IBLDiffusePBRSecondary(normal, albedo, metallic, E);
			vec3 secFr = IBLSpecularPBRSecondary(normal, fragToEye, perceptualRoughness, E, energyComp);
			vec3 secondary = (secFd + secFr) * secIntensity;

			return mix(secondary, primary, blendFactor);
		}
	}

	return primary;
}

#endif

	-->
	</source>
</shader>