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

// Local volume 0
uniform samplerCube s_texture7; 	// Diffuse Map
uniform samplerCube s_texture15; 	// Pre-Filtered Specular Map
uniform sampler2D s_texture10;		// IBL BRDF Lut

// Local volume 1
uniform samplerCube s_texture11;	// Diffuse Map
uniform samplerCube s_texture12;	// Pre-Filtered Specular Map

// Sky (global fallback)
uniform samplerCube s_texture16;	// Sky Diffuse Map
uniform samplerCube s_texture17;	// Sky Pre-Filtered Specular Map

uniform mat4 iblRotation;            // Sky rotation
uniform mat4 iblSecondaryRotation;   // Unused placeholder for local volume rotations (local = identity)

// ---------------------------------------------------------------------------
// Filament-style IBL helpers
// ---------------------------------------------------------------------------

vec3 GetParallaxCorrectedReflection(vec3 R, vec3 worldPos, mat4 inverseVolTransform, mat4 volTransform, vec3 volMin, vec3 volMax)
{
	vec3 localPos = (inverseVolTransform * vec4(worldPos, 1.0)).xyz;
	vec3 localDir = (inverseVolTransform * vec4(R, 0.0)).xyz;

	vec3 invLocalDir = 1.0 / (localDir + 0.000001);
	vec3 t0 = (volMin - localPos) * invLocalDir;
	vec3 t1 = (volMax - localPos) * invLocalDir;
	vec3 tMaxPlane = max(t0, t1);
	float dist = min(min(tMaxPlane.x, tMaxPlane.y), tMaxPlane.z);

	vec3 intersectLocal = localPos + localDir * dist;
	vec3 correctedR = (volTransform * vec4(intersectLocal, 0.0)).xyz;

	return normalize(correctedR);
}

float RoughnessToLod(float roughness, float maxLod)
{
	return maxLod * roughness * (2.0 - roughness);
}

vec3 GetSpecularDominantDirection(vec3 n, vec3 r, float roughness)
{
	return mix(r, n, roughness * roughness);
}

vec3 SpecularDFG(vec2 dfg, vec3 f0)
{
	return f0 * dfg.x + dfg.y;
}

// ---------------------------------------------------------------------------
// Sky IBL evaluation
// ---------------------------------------------------------------------------

vec3 EvalSky(vec3 normal, vec3 fragToEye, vec3 albedo, float metallic, float perceptualRoughness, vec3 E, vec3 energyComp)
{
	float skyIntensity = GetSkyIntensity();
	if (skyIntensity <= 0.0)
	{
		return vec3(0.0);
	}

	vec3 color = vec3(0.0);

	// Diffuse
	vec3 diffuseColor = albedo * (1.0 - metallic);
	vec3 iblDiffuseVec = (iblRotation * vec4(normal, 0.0)).xyz;
	vec3 irradiance = texture(s_texture16, iblDiffuseVec).rgb;
	color += diffuseColor * irradiance * (1.0 - E);

	// Specular
	vec3 R = reflect(-fragToEye, normal);
	R = GetSpecularDominantDirection(normal, R, perceptualRoughness);
	vec3 iblSpecVec = (iblRotation * vec4(R, 0.0)).xyz;
	float lod = RoughnessToLod(perceptualRoughness, float(graphicConstants.iblMaxReflectionLod));
	vec3 preFilteredColor = textureLod(s_texture17, iblSpecVec, lod).rgb;
	color += E * preFilteredColor * energyComp;

	return color * skyIntensity;
}

// ---------------------------------------------------------------------------
// Per-volume IBL evaluation (local volumes only)
// ---------------------------------------------------------------------------

vec3 EvalVolumeDiffuse(int vol, vec3 normal, vec3 albedo, float metallic, vec3 E)
{
	vec3 diffuseColor = albedo * (1.0 - metallic);
	vec3 iblSamplerVec = normal; // Local volumes have identity rotation.
	vec3 irradiance;
	if (vol == 0)
		irradiance = texture(s_texture7, iblSamplerVec).rgb;
	else
		irradiance = texture(s_texture11, iblSamplerVec).rgb;
	return diffuseColor * irradiance * (1.0 - E);
}

vec3 EvalVolumeSpecular(int vol, vec3 normal, vec3 fragToEye, float perceptualRoughness, vec3 E, vec3 energyComp, vec3 worldPos)
{
	vec3 R = reflect(-fragToEye, normal);
	R = GetSpecularDominantDirection(normal, R, perceptualRoughness);

	if (IsVolumePccEnabled(vol))
	{
		R = GetParallaxCorrectedReflection(R, worldPos,
			GetVolumeInverseTransform(vol), GetVolumeWorldTransform(vol),
			GetVolumeMin(vol), GetVolumeMax(vol));
	}

	vec3 iblSamplerVec = R; // Local volumes have identity rotation.
	float lod = RoughnessToLod(perceptualRoughness, float(graphicConstants.iblMaxReflectionLod));
	vec3 preFilteredColor;
	if (vol == 0)
		preFilteredColor = textureLod(s_texture15, iblSamplerVec, lod).rgb;
	else
		preFilteredColor = textureLod(s_texture12, iblSamplerVec, lod).rgb;

	vec3 specular = E * preFilteredColor;
	specular *= energyComp;
	return specular;
}

vec3 EvalVolume(int vol, vec3 normal, vec3 fragToEye, vec3 albedo, float metallic, float perceptualRoughness, vec3 E, vec3 energyComp, vec3 worldPos)
{
	vec3 Fd = EvalVolumeDiffuse(vol, normal, albedo, metallic, E);
	vec3 Fr = EvalVolumeSpecular(vol, normal, fragToEye, perceptualRoughness, E, energyComp, worldPos);
	return (Fd + Fr) * GetVolumeIntensity(vol);
}

// ---------------------------------------------------------------------------
// Combined IBL: sky (global fallback) + up to 2 local volumes
// Interior volumes: fade region blends with other volumes, not sky.
// ---------------------------------------------------------------------------

vec3 IBLPBR(vec3 normal, vec3 fragToEye, vec3 albedo, float metallic, float perceptualRoughness, vec2 dfg, vec3 energyComp, vec3 worldPos)
{
	if (!IsIBLInUse())
	{
		return vec3(0.0);
	}

	vec3 f0 = BaseReflectivityPBR(vec3(0.04), albedo, metallic);
	vec3 E = SpecularDFG(dfg, f0);

	// Compute raw local volume weights (0 outside, fades to 1 inside).
	float w0 = ComputeVolumeBlendFactor(0, worldPos);
	float w1 = ComputeVolumeBlendFactor(1, worldPos);
	bool int0 = IsVolumeInterior(0);
	bool int1 = IsVolumeInterior(1);

	// Sky blocking: interior volumes block sky across their entire boundary,
	// non-interior volumes only block sky proportional to their raw weight.
	float skyBlock0 = int0 ? (w0 > 0.0 ? 1.0 : 0.0) : w0;
	float skyBlock1 = int1 ? (w1 > 0.0 ? 1.0 : 0.0) : w1;
	float totalBlock = skyBlock0 + skyBlock1;
	if (totalBlock > 1.0)
	{
		skyBlock0 /= totalBlock;
		skyBlock1 /= totalBlock;
		totalBlock = 1.0;
	}
	float wSky = max(1.0 - totalBlock, 0.0);

	// Distribute the non-sky portion between volumes using raw weights.
	float volBudget = 1.0 - wSky;
	float finalW0 = 0.0;
	float finalW1 = 0.0;

	float rawSum = w0 + w1;
	if (rawSum > 0.0 && volBudget > 0.0)
	{
		finalW0 = (w0 / rawSum) * volBudget;
		finalW1 = (w1 / rawSum) * volBudget;
	}

	vec3 color = vec3(0.0);

	if (finalW0 > 0.0)
	{
		color += EvalVolume(0, normal, fragToEye, albedo, metallic, perceptualRoughness, E, energyComp, worldPos) * finalW0;
	}

	if (finalW1 > 0.0)
	{
		color += EvalVolume(1, normal, fragToEye, albedo, metallic, perceptualRoughness, E, energyComp, worldPos) * finalW1;
	}

	if (wSky > 0.0)
	{
		color += EvalSky(normal, fragToEye, albedo, metallic, perceptualRoughness, E, energyComp) * wSky;
	}

	return color;
}

#endif

	-->
	</source>
</shader>