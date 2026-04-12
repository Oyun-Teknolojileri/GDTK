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

uniform samplerCube s_texture7; 	// Volume 0 Diffuse Map
uniform samplerCube s_texture15; 	// Volume 0 Pre-Filtered Specular Map
uniform sampler2D s_texture10;		// IBL BRDF Lut

uniform samplerCube s_texture11;	// Volume 1 Diffuse Map
uniform samplerCube s_texture12;	// Volume 1 Pre-Filtered Specular Map

uniform mat4 iblRotation;
uniform mat4 iblSecondaryRotation;

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
// Per-volume IBL evaluation
// ---------------------------------------------------------------------------

vec3 EvalVolumeDiffuse(int vol, vec3 normal, vec3 albedo, float metallic, vec3 E, mat4 rotation)
{
	vec3 diffuseColor = albedo * (1.0 - metallic);
	vec3 iblSamplerVec = (rotation * vec4(normal, 0.0)).xyz;
	vec3 irradiance;
	if (vol == 0)
		irradiance = texture(s_texture7, iblSamplerVec).rgb;
	else
		irradiance = texture(s_texture11, iblSamplerVec).rgb;
	return diffuseColor * irradiance * (1.0 - E);
}

vec3 EvalVolumeSpecular(int vol, vec3 normal, vec3 fragToEye, float perceptualRoughness, vec3 E, vec3 energyComp, vec3 worldPos, mat4 rotation)
{
	vec3 R = reflect(-fragToEye, normal);
	R = GetSpecularDominantDirection(normal, R, perceptualRoughness);

	if (IsVolumePccEnabled(vol))
	{
		R = GetParallaxCorrectedReflection(R, worldPos,
			GetVolumeInverseTransform(vol), GetVolumeWorldTransform(vol),
			GetVolumeMin(vol), GetVolumeMax(vol));
	}

	vec3 iblSamplerVec = (rotation * vec4(R, 0.0)).xyz;
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

vec3 EvalVolume(int vol, vec3 normal, vec3 fragToEye, vec3 albedo, float metallic, float perceptualRoughness, vec3 E, vec3 energyComp, vec3 worldPos, mat4 rotation)
{
	vec3 Fd = vec3(0.0);
	if (IsVolumeDiffuseEnabled(vol))
	{
		Fd = EvalVolumeDiffuse(vol, normal, albedo, metallic, E, rotation);
	}
	vec3 Fr = vec3(0.0);
	if (IsVolumeSpecularEnabled(vol))
	{
		Fr = EvalVolumeSpecular(vol, normal, fragToEye, perceptualRoughness, E, energyComp, worldPos, rotation);
	}
	return (Fd + Fr) * GetVolumeIntensity(vol);
}

// ---------------------------------------------------------------------------
// Combined IBL evaluation with per-pixel dual volume blending
// ---------------------------------------------------------------------------

vec3 IBLPBR(vec3 normal, vec3 fragToEye, vec3 albedo, float metallic, float perceptualRoughness, vec2 dfg, vec3 energyComp, vec3 worldPos)
{
	if (!IsIBLInUse())
	{
		return vec3(0.0);
	}

	vec3 f0 = BaseReflectivityPBR(vec3(0.04), albedo, metallic);
	vec3 E = SpecularDFG(dfg, f0);

	float w0 = ComputeVolumeBlendFactor(0, worldPos);
	float w1 = ComputeVolumeBlendFactor(1, worldPos);

	// Sky volumes (fadeDist <= 0) fill in the remaining weight left by local volumes.
	bool isSky0 = (GetVolumeFadeDistance(0) <= 0.0);
	bool isSky1 = (GetVolumeFadeDistance(1) <= 0.0);

	if (isSky0 && !isSky1)
	{
		// vol0 is sky, vol1 is local: sky fills what vol1 doesn't cover.
		w0 = max(1.0 - w1, 0.0);
	}
	else if (isSky1 && !isSky0)
	{
		// vol1 is sky, vol0 is local: sky fills what vol0 doesn't cover.
		w1 = max(1.0 - w0, 0.0);
	}

	vec3 color = vec3(0.0);

	if (w0 > 0.0)
	{
		color += EvalVolume(0, normal, fragToEye, albedo, metallic, perceptualRoughness, E, energyComp, worldPos, iblRotation) * w0;
	}

	if (w1 > 0.0)
	{
		color += EvalVolume(1, normal, fragToEye, albedo, metallic, perceptualRoughness, E, energyComp, worldPos, iblSecondaryRotation) * w1;
	}

	// Normalize if two local volumes overlap (weights sum > 1).
	float totalWeight = w0 + w1;
	if (totalWeight > 1.0)
	{
		color /= totalWeight;
	}

	return color;
}

#endif

	-->
	</source>
</shader>