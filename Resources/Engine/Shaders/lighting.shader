<shader>
	<type name = "includeShader" />
	<include name = "pbrCommon.shader" />
	<include name = "shadow.shader" />
	<define name = "highlightCascades" val="0,1" />
	<source>
	<!--
#ifndef LIGHTING_SHADER
#define LIGHTING_SHADER

uniform sampler2DArray s_texture8; // Shadow atlas

/// Deferred rendering uniforms
uniform sampler2D s_texture13; // Light data
uniform int activePointLightIndexes[MAX_POINT_LIGHT_PER_OBJECT];
uniform int activeSpotLightIndexes[MAX_SPOT_LIGHT_PER_OBJECT];

const float shadowFadeOutDistanceNorm = 0.9;

// ---------------------------------------------------------------------------
// Filament-style attenuation
// ---------------------------------------------------------------------------

// Physically-based square falloff with smooth window function
// Filament: getSquareFalloffAttenuation + getDistanceAttenuation
float DistanceAttenuation(float distanceSq, float falloff)
{
	// falloff = 1.0 / (radius * radius)
	float factor = distanceSq * falloff;
	float smoothFactor = clamp(1.0 - factor * factor, 0.0, 1.0);
	// Smooth window: (1 - (d²/r²)²)²
	// Divided by distanceSq for inverse-square law
	// Clamp to avoid division by zero for very close lights
	return (smoothFactor * smoothFactor) / max(distanceSq, 1e-2);
}

// Filament-style spot light angular attenuation
// cosInner and cosOuter are pre-computed cosine values
float SpotAngleAttenuation(float cosAngle, float cosInner, float cosOuter)
{
	float scale = 1.0 / max(cosInner - cosOuter, 1e-4);
	float offset = -cosOuter * scale;
	float attenuation = clamp(cosAngle * scale + offset, 0.0, 1.0);
	return attenuation * attenuation;
}

float CalculateDirectionalShadow
(
	vec3 pos,
	vec3 viewCamPos,
	mat4 lightProjView,
	vec2 shadowAtlasCoord,
	float shadowAtlasResRatio,
	int shadowAtlasLayer,
	float lightBleedReduction,
	float shadowBias
)
{
	vec4 fragPosForLight = lightProjView * vec4(pos, 1.0);
	vec3 projCoord = fragPosForLight.xyz * 0.5 + 0.5;

	vec2 startCoord = shadowAtlasCoord;
	vec2 endCoord = shadowAtlasCoord + shadowAtlasResRatio;

	vec2 uvInAtlas = startCoord + shadowAtlasResRatio * projCoord.xy;
	vec3 sampleCoord = vec3(uvInAtlas, shadowAtlasLayer);

	float texelSize = 1.0 / SHADOW_ATLAS_SIZE;

	float shadow = PCFFilterShadow2D
	(
		s_texture8,
		sampleCoord,
		startCoord,
		endCoord,
		texelSize,
		projCoord.z,
		lightBleedReduction,
		shadowBias
	);

	// Fade shadow out after min shadow fade out distance
	vec3 camToPos = pos - viewCamPos;
	float camDist = dot(camToPos, camToPos);
	float fadeDist = graphicConstants.shadowDistance * shadowFadeOutDistanceNorm;
	float fadeRange = graphicConstants.shadowDistance * (1.0 - shadowFadeOutDistanceNorm);
	float fade = (sqrt(camDist) - fadeDist) / fadeRange;
	fade = clamp(fade, 0.0, 1.0);
	fade = fade * fade;
	return clamp(shadow + fade, 0.0, 1.0);
}

float CalculateSpotShadow
(
	vec3 pos,
	vec3 lightPos,
	mat4 lightProjView,
	float shadowCameraFar,
	vec2 shadowAtlasCoord,
	float shadowAtlasResRatio,
	int shadowAtlasLayer,
	float lightBleedReduction,
	float shadowBias,
	float lightDistance
)
{
	vec4 fragPosForLight = lightProjView * vec4(pos, 1.0);
	vec3 projCoord = fragPosForLight.xyz / fragPosForLight.w;
	projCoord = projCoord * 0.5 + 0.5;

	float currFragDepth = lightDistance / shadowCameraFar;

	vec2 startCoord = shadowAtlasCoord;
	vec3 coord = vec3(startCoord + shadowAtlasResRatio * projCoord.xy, shadowAtlasLayer);

	float texelSize = 1.0 / SHADOW_ATLAS_SIZE;

	return PCFFilterShadow2D
	(
		s_texture8,
		coord,
		startCoord,
		startCoord + shadowAtlasResRatio,
		texelSize,
		currFragDepth,
		lightBleedReduction,
		shadowBias
	);
}

float CalculatePointShadow
(
	vec3 pos,
	vec3 lightPos,
	float shadowCameraFar,
	vec2 shadowAtlasCoord,
	float shadowAtlasResRatio,
	int shadowAtlasLayer,
	float lightBleedReduction,
	float shadowBias,
	float precomputedDist
)
{
	vec3 lightToFrag = pos - lightPos;
	float currFragDepth = precomputedDist / shadowCameraFar;

	float texelSize = 1.0 / SHADOW_ATLAS_SIZE;

	return PCFFilterOmni
	(
		s_texture8,
		shadowAtlasCoord,
		shadowAtlasResRatio,
		shadowAtlasLayer,
		lightToFrag,
		texelSize,
		currFragDepth,
		lightBleedReduction,
		shadowBias
	);
}

// ---------------------------------------------------------------------------
// Filament-style surface shading for a single light
// Combines BRDF evaluation with light color, intensity, attenuation, NoL and shadow
// ---------------------------------------------------------------------------

vec3 SurfaceShading
(
	vec3 normal,
	vec3 fragToEye,
	vec3 albedo,
	float metallic,
	float roughness,
	vec3 lightDir,
	vec3 lightColor,
	float lightIntensity,
	float attenuation,
	float shadow,
	vec3 energyCompensation
)
{
	vec3 F0 = BaseReflectivityPBR(vec3(0.04), albedo, metallic);
	vec3 diffuseColor = albedo * (1.0 - metallic);

	vec3 halfway = normalize(lightDir + fragToEye);

	float NoV = abs(dot(normal, fragToEye)) + 1e-5;
	float NoL = clamp(dot(normal, lightDir), 0.0, 1.0);
	float NoH = clamp(dot(normal, halfway), 0.0, 1.0);
	float LoH = clamp(dot(lightDir, halfway), 0.0, 1.0);

	// Specular BRDF
	vec3 h = halfway;
	float D = distribution(roughness, NoH, h);
	float V = visibility(roughness, NoV, NoL);
	vec3 F = fresnel(F0, LoH);
	vec3 Fr = (D * V) * F;

	// Diffuse BRDF
	vec3 Fd = diffuseColor * diffuse(roughness, NoV, NoL, LoH);

	// Combine: Fd + Fr * energyCompensation (Filament style)
	vec3 color = Fd + Fr * energyCompensation;

	// Apply light contribution: color * lightColor * (intensity * attenuation * NoL * shadow)
	return color * lightColor * (lightIntensity * attenuation * NoL * shadow);
}

vec3 PBRLighting
(
	vec3 fragPos,
	float viewPosDepth,
	vec3 normal,
	vec3 fragToEye,
	vec3 viewCamPos,
	vec3 albedo,
	float metallic,
	float roughness,
	vec3 energyCompensation
)
{
	vec3 irradiance = vec3(0.0);

	// ----- Directional Lights -----
	for (int i = 0; i < GetActiveDirectionalLightCount(); i++)
	{
		DirectionalLightData light = directionalLightArray[i];

		float resRatio = light.shadowResolution / graphicConstants.shadowAtlasSize;

		vec3 lightDir = normalize(-light.direction);

		float shadow = 1.0;
		vec3 cascadeMultiplier = vec3(1.0);

		if (light.castShadow == 1)
		{
			int numCascade = graphicConstants.cascadeCount;
			int cascadeOfThisPixel = numCascade - 1;

			float depth = abs(viewPosDepth);
			for (int ci = 0; ci < numCascade; ci++)
			{
				if (depth < graphicConstants.cascadeDistances[ci])
				{
					cascadeOfThisPixel = ci;
					break;
				}
			}

#if highlightCascades
			if (cascadeOfThisPixel == 0)
			{
				cascadeMultiplier = vec3(4.0, 1.0, 1.0);
			}
			else if (cascadeOfThisPixel == 1)
			{
				cascadeMultiplier = vec3(1.0, 4.0, 1.0);
			}
			else if (cascadeOfThisPixel == 2)
			{
				cascadeMultiplier = vec3(1.0, 1.0, 4.0);
			}
			else if (cascadeOfThisPixel == 3)
			{
				cascadeMultiplier = vec3(4.0, 4.0, 1.0);
			}
#endif

			int layer = 0;
			vec2 coord = vec2(0.0);
			ShadowAtlasLut(light.shadowResolution, light.shadowAtlasCoord, cascadeOfThisPixel, layer, coord);

			layer += light.shadowAtlasLayer;

			shadow = CalculateDirectionalShadow
			(
				fragPos,
				viewCamPos,
				directionalLightPVMArray[i].projectionViewMatrices[cascadeOfThisPixel],
				coord / graphicConstants.shadowAtlasSize,
				resRatio,
				layer,
				light.bleedingReduction,
				light.shadowBias
			);
		}

		// Directional lights have no distance attenuation
		vec3 Lo = SurfaceShading(normal, fragToEye, albedo, metallic, roughness,
			lightDir, light.color, light.intensity, 1.0, shadow, energyCompensation);

		irradiance += Lo * cascadeMultiplier;
	}

	// ----- Point Lights -----
	for (int i = 0; i < GetActivePointLightCount(); i++)
	{
		int ii = activePointLightIndexes[i];
		PointLightData light = pointLightArray[ii];

		vec3 fragToLight = light.position - fragPos;
		float lightDistanceSq = dot(fragToLight, fragToLight);
		float lightDistance = sqrt(lightDistanceSq);

		if (lightDistance >= light.radius)
		{
			continue;
		}

		float resRatio = light.shadowResolution / graphicConstants.shadowAtlasSize;
		float falloff = 1.0 / (light.radius * light.radius);
		float attenuation = DistanceAttenuation(lightDistanceSq, falloff);

		vec3 lightDir = fragToLight / lightDistance;

		float shadow = 1.0;
		if (light.castShadow == 1)
		{
			shadow = CalculatePointShadow
			(
				fragPos,
				light.position,
				light.radius,
				light.shadowAtlasCoord,
				resRatio,
				light.shadowAtlasLayer,
				light.bleedingReduction,
				light.shadowBias,
				lightDistance
			);
		}

		vec3 Lo = SurfaceShading(normal, fragToEye, albedo, metallic, roughness,
			lightDir, light.color, light.intensity, attenuation, shadow, energyCompensation);

		irradiance += Lo;
	}

	// ----- Spot Lights -----
	for (int i = 0; i < GetActiveSpotLightCount(); i++)
	{
		int ii = activeSpotLightIndexes[i];
		SpotLightData light = spotLightArray[ii];

		vec3 fragToLight = light.position - fragPos;
		float lightDistanceSq = dot(fragToLight, fragToLight);
		float lightDistance = sqrt(lightDistanceSq);

		if (lightDistance >= light.radius)
		{
			continue;
		}

		float resRatio = light.shadowResolution / graphicConstants.shadowAtlasSize;
		float falloff = 1.0 / (light.radius * light.radius);
		float attenuation = DistanceAttenuation(lightDistanceSq, falloff);

		// Filament-style spot cone attenuation
		vec3 lightDirNorm = fragToLight / lightDistance;
		float cosAngle = dot(-lightDirNorm, light.direction);
		// light.innerAngle and light.outerAngle are pre-computed cosine values from CPU
		attenuation *= SpotAngleAttenuation(cosAngle, light.innerAngle, light.outerAngle);

		float shadow = 1.0;
		if (light.castShadow == 1)
		{
			shadow = CalculateSpotShadow
			(
				fragPos,
				light.position,
				light.projectionViewMatrix,
				light.radius,
				light.shadowAtlasCoord / graphicConstants.shadowAtlasSize,
				resRatio,
				light.shadowAtlasLayer,
				light.bleedingReduction,
				light.shadowBias,
				lightDistance
			);
		}

		vec3 Lo = SurfaceShading(normal, fragToEye, albedo, metallic, roughness,
			lightDirNorm, light.color, light.intensity, attenuation, shadow, energyCompensation);

		irradiance += Lo;
	}

	return irradiance;
}

#endif
	-->
	</source>
</shader>