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

// ---------------------------------------------------------------------------
// Premultiplied-alpha volume accumulation (Godot-style)
//
// Each volume contributes vec4(rgb * blend, blend) into an accumulator.
// - Exterior volumes: at the fade region, probe color is mixed towards sky
//   so that the probe smoothly reveals sky at its boundary.
// - Interior volumes: no sky mix; inside the boundary the probe is fully
//   opaque, fading only at the edge for volume-to-volume transitions.
// After accumulation, dividing rgb by alpha yields the weighted average.
// If total alpha < 1, sky fills the remainder automatically.
// ---------------------------------------------------------------------------

void AccumulateVolume(int vol, vec3 normal, vec3 fragToEye, vec3 albedo, float metallic,
	float perceptualRoughness, vec3 E, vec3 energyComp, vec3 worldPos, vec3 skyColor,
	inout vec4 accum)
{
	float blend = ComputeVolumeBlendFactor(vol, worldPos);
	if (blend <= 0.0)
	{
		return;
	}

	vec3 Fd = EvalVolumeDiffuse(vol, normal, albedo, metallic, E);
	vec3 Fr = EvalVolumeSpecular(vol, normal, fragToEye, perceptualRoughness, E, energyComp, worldPos);
	vec3 volumeColor = (Fd + Fr) * GetVolumeIntensity(vol);

	// Exterior volumes blend towards sky at the boundary edges.
	// Interior volumes keep their own color across the full boundary.
	if (!IsVolumeInterior(vol))
	{
		volumeColor = mix(skyColor, volumeColor, blend);
	}

	accum += vec4(volumeColor * blend, blend);
}

// ---------------------------------------------------------------------------
// Combined IBL: premultiplied accumulation of local volumes + sky fallback
// ---------------------------------------------------------------------------

vec3 IBLPBR(vec3 normal, vec3 fragToEye, vec3 albedo, float metallic, float perceptualRoughness, vec2 dfg, vec3 energyComp, vec3 worldPos)
{
	if (!IsIBLInUse())
	{
		return vec3(0.0);
	}

	vec3 f0 = BaseReflectivityPBR(vec3(0.04), albedo, metallic);
	vec3 E = SpecularDFG(dfg, f0);

	// Evaluate sky once (used as fallback and for exterior volume edge blending).
	vec3 skyColor = EvalSky(normal, fragToEye, albedo, metallic, perceptualRoughness, E, energyComp);

	// Accumulate local volumes with premultiplied alpha.
	vec4 accum = vec4(0.0);
	AccumulateVolume(0, normal, fragToEye, albedo, metallic, perceptualRoughness, E, energyComp, worldPos, skyColor, accum);
	AccumulateVolume(1, normal, fragToEye, albedo, metallic, perceptualRoughness, E, energyComp, worldPos, skyColor, accum);

	// Final compositing: weighted average of volumes, sky fills the remainder.
	vec3 color;
	if (accum.a > 0.0)
	{
		vec3 volumeContrib = accum.rgb / accum.a;
		float volumeAlpha = min(accum.a, 1.0);
		color = mix(skyColor, volumeContrib, volumeAlpha);
	}
	else
	{
		color = skyColor;
	}

	return color;
}

#endif

	-->
	</source>
</shader>