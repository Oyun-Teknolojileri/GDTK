<shader>
	<type name = "includeShader" />
	<include name = "vulkanCompatInc.shader" />
	<include name = "pbrCommon.shader" />
	<include name = "drawDataInc.shader" />
	<include name = "perDrawDataInc.shader" />
	<texture slot = "7"  name = "s_irradianceMap"  viewType = "cube" />
	<texture slot = "10" name = "s_brdfLut" />
	<texture slot = "11" name = "s_secondaryIrradiance" viewType = "cube" />
	<texture slot = "12" name = "s_secondarySpecular" viewType = "cube" />
	<texture slot = "15" name = "s_iblSpecular" viewType = "cube" />
	<texture slot = "16" name = "s_skyIrradiance" viewType = "cube" />
	<texture slot = "17" name = "s_skySpecular" viewType = "cube" />
	<source>
	<!--

#ifndef IBL_SHADER
#define IBL_SHADER

// Local volume 0
TK_SAMPLER_BINDING(7)  uniform samplerCube s_irradianceMap; 	// Diffuse Map
TK_SAMPLER_BINDING(15) uniform samplerCube s_iblSpecular; 	// Pre-Filtered Specular Map
TK_SAMPLER_BINDING(10) uniform sampler2D s_brdfLut;		// IBL BRDF Lut

// Local volume 1
TK_SAMPLER_BINDING(11) uniform samplerCube s_secondaryIrradiance;	// Diffuse Map
TK_SAMPLER_BINDING(12) uniform samplerCube s_secondarySpecular;	// Pre-Filtered Specular Map

// Sky (global fallback)
TK_SAMPLER_BINDING(16) uniform samplerCube s_skyIrradiance;	// Sky Diffuse Map
TK_SAMPLER_BINDING(17) uniform samplerCube s_skySpecular;	// Sky Pre-Filtered Specular Map

// Sky rotation backed by perDraw._iblRotation (PerDrawData UBO, slot 6). A local volume
// carries its own orientation in the volume transform, so it never touches this matrix.
// `perDraw._iblSecondaryRotation` stays reserved for per-volume rotations and identity-only;
// nothing reads it today.

// ---------------------------------------------------------------------------
// Filament-style IBL helpers
// ---------------------------------------------------------------------------

vec3 GetParallaxCorrectedReflection(vec3 R, vec3 worldPos, mat4 inverseVolTransform, vec3 volMin, vec3 volMax)
{
	vec3 localPos = (inverseVolTransform * vec4(worldPos, 1.0)).xyz;
	vec3 localDir = (inverseVolTransform * vec4(R, 0.0)).xyz;

	vec3 invLocalDir = 1.0 / (localDir + 0.000001);
	vec3 t0 = (volMin - localPos) * invLocalDir;
	vec3 t1 = (volMax - localPos) * invLocalDir;
	vec3 tMaxPlane = max(t0, t1);
	float dist = min(min(tMaxPlane.x, tMaxPlane.y), tMaxPlane.z);

	vec3 intersectLocal = localPos + localDir * dist;

	// Return local-space direction. Cubemap is rendered aligned to the entity's local axes.
	return normalize(intersectLocal);
}

// Level of the prefiltered chain a roughness reads from. Linear in roughness, because the chain is
// baked the same way (each level holds `mip / (mipMaps - 1)`, see Renderer::GenerateSpecularEnvMap):
// read and write have to use one rule or the reflection shows a roughness the material does not have.
// The perceptual `r * (2 - r)` curve this used to apply spends up to twice the mips on the low end
// (`r * (2 - r)` is `r` plus `r * (1 - r)`, positive for every roughness in between), which is exactly
// where a wet floor lives, so it stretched those reflections well before their roughness asked for it.
float RoughnessToLod(float roughness, float maxLod)
{
	return maxLod * roughness;
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
// Sky IBL evaluation (returns raw sky color, no weighting)
// ---------------------------------------------------------------------------

vec3 EvalSky(vec3 normal, vec3 fragToEye, vec3 albedo, float metallic, float perceptualRoughness, vec3 E, vec3 energyComp)
{
	vec3 color = vec3(0.0);

	float skyIntensity = GetSkyIntensity();
	if (skyIntensity <= 0.0)
	{
		return color;
	}

	// Diffuse
	vec3 diffuseColor = albedo * (1.0 - metallic);
	vec3 iblDiffuseVec = (perDraw._iblRotation * vec4(normal, 0.0)).xyz;
	vec3 irradiance = texture(s_skyIrradiance, iblDiffuseVec).rgb;
	color += diffuseColor * irradiance * (1.0 - E);

	// Specular
	vec3 R = reflect(-fragToEye, normal);
	R = GetSpecularDominantDirection(normal, R, perceptualRoughness);
	vec3 iblSpecVec = (perDraw._iblRotation * vec4(R, 0.0)).xyz;
	float lod = RoughnessToLod(perceptualRoughness, float(graphicConstants.iblMaxReflectionLod));
	vec3 preFilteredColor = textureLod(s_skySpecular, iblSpecVec, lod).rgb;
	color += E * preFilteredColor * energyComp;

	return color * skyIntensity;
}

// ---------------------------------------------------------------------------
// Per-volume IBL evaluation (local volumes only)
// ---------------------------------------------------------------------------

vec3 EvalVolumeDiffuse(int vol, vec3 normal, vec3 albedo, float metallic, vec3 E)
{
	vec3 diffuseColor = albedo * (1.0 - metallic);
	// Cubemap is rendered aligned to entity local axes, rotate world normal to local space.
	vec3 iblSamplerVec = (GetVolumeInverseTransform(vol) * vec4(normal, 0.0)).xyz;
	vec3 irradiance;
	if (vol == 0)
		irradiance = texture(s_irradianceMap, iblSamplerVec).rgb;
	else
		irradiance = texture(s_secondaryIrradiance, iblSamplerVec).rgb;
	return diffuseColor * irradiance * (1.0 - E);
}

vec3 EvalVolumeSpecular(int vol, vec3 normal, vec3 fragToEye, float perceptualRoughness, vec3 E, vec3 energyComp, vec3 worldPos)
{
	vec3 R = reflect(-fragToEye, normal);
	R = GetSpecularDominantDirection(normal, R, perceptualRoughness);

	vec3 iblSamplerVec;
	if (IsVolumePccEnabled(vol))
	{
		// PCC returns a local-space direction.
		iblSamplerVec = GetParallaxCorrectedReflection(R, worldPos,
			GetVolumeInverseTransform(vol), GetVolumeMin(vol), GetVolumeMax(vol));
	}
	else
	{
		// Cubemap is rendered aligned to entity local axes, rotate world R to local space.
		iblSamplerVec = (GetVolumeInverseTransform(vol) * vec4(R, 0.0)).xyz;
	}

	float lod = RoughnessToLod(perceptualRoughness, float(graphicConstants.iblMaxReflectionLod));
	vec3 preFilteredColor;
	if (vol == 0)
		preFilteredColor = textureLod(s_iblSpecular, iblSamplerVec, lod).rgb;
	else
		preFilteredColor = textureLod(s_secondarySpecular, iblSamplerVec, lod).rgb;

	vec3 specular = E * preFilteredColor;
	specular *= energyComp;
	return specular;
}

// ---------------------------------------------------------------------------
// Premultiplied-alpha volume accumulation with the sky as a weighted participant.
//
// Each volume contributes weight * vec4(volumeColor, 1), where `blend` acts as
// both its weight in the volume-to-volume average and the amount of sky it lets
// through. The sky then takes only the weight the strongest volume did not cover
// (skyWeight = 1 - max(blend)). Consequences:
// - A pixel fully covered by any volume (blend == 1) gets no sky at all, so the
//   old partial sky leak cannot happen.
// - A pixel inside a fade band (0 < blend < 1) blends smoothly towards the sky
//   instead of stepping to it, because the volume weight cancels out of the
//   average only when nothing is left uncovered.
// - Exterior and interior volumes therefore weigh the same in the average; the
//   slot order of the two volumes does not change the result.
//
// `Interior` volumes never reveal sky: their presence at a pixel pins skyWeight
// to 0, so an interior probe stays fully opaque up to its own box face and only
// blends against the other volume. Such a box is expected to sit inside geometry.
// ---------------------------------------------------------------------------

struct VolumeAccum
{
	vec3 color;     // sum(volumeColor * blend)
	float weight;   // sum(blend)
	float maxBlend; // max(blend)
	bool interior;  // true when any contributing volume is flagged interior
};

void AccumulateVolume(int vol, vec3 normal, vec3 fragToEye, vec3 albedo, float metallic,
	float perceptualRoughness, vec3 E, vec3 energyComp, vec3 worldPos,
	inout VolumeAccum accum)
{
	float blend = ComputeVolumeBlendFactor(vol, worldPos);
	if (blend <= 0.0)
	{
		return;
	}

	vec3 Fd = EvalVolumeDiffuse(vol, normal, albedo, metallic, E);
	vec3 Fr = EvalVolumeSpecular(vol, normal, fragToEye, perceptualRoughness, E, energyComp, worldPos);
	vec3 volumeColor = (Fd + Fr) * GetVolumeIntensity(vol);

	accum.color += volumeColor * blend;
	accum.weight += blend;
	accum.maxBlend = max(accum.maxBlend, blend);
	accum.interior = accum.interior || IsVolumeInterior(vol);
}

// ---------------------------------------------------------------------------
// Combined IBL: weighted average of the local volumes plus the sky that fills
// whatever weight no volume covered.
// ---------------------------------------------------------------------------

vec3 IBLPBR(vec3 normal, vec3 fragToEye, vec3 albedo, float metallic, float perceptualRoughness, vec2 dfg, vec3 energyComp, vec3 worldPos)
{
	if (!IsIBLInUse())
	{
		return vec3(0.0);
	}

	vec3 f0 = BaseReflectivityPBR(vec3(0.04), albedo, metallic);
	vec3 E = SpecularDFG(dfg, f0);

	// Evaluate sky once: it is both the outside fallback and the boundary blend target.
	vec3 skyColor = EvalSky(normal, fragToEye, albedo, metallic, perceptualRoughness, E, energyComp);

	VolumeAccum accum;
	accum.color    = vec3(0.0);
	accum.weight   = 0.0;
	accum.maxBlend = 0.0;
	accum.interior = false;

	AccumulateVolume(0, normal, fragToEye, albedo, metallic, perceptualRoughness, E, energyComp, worldPos, accum);
	AccumulateVolume(1, normal, fragToEye, albedo, metallic, perceptualRoughness, E, energyComp, worldPos, accum);

	// Outside every volume: pure sky.
	if (accum.weight <= 0.0)
	{
		return skyColor;
	}

	// Sky fills only the weight that no volume covered. Interior volumes keep the
	// sky out of the pixel entirely, exterior ones fade into it over their fade
	// distance.
	float skyWeight = accum.interior ? 0.0 : (1.0 - accum.maxBlend);

	return (accum.color + skyColor * skyWeight) / (accum.weight + skyWeight);
}

#endif

	-->
	</source>
</shader>