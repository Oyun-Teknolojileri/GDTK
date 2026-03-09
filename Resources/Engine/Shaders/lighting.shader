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

float Attenuation(float distance, float radius)
{
	float attenuation = 1.0 / (1.0 + 0.09 * distance + 0.032 * distance * distance);
	attenuation *= 1.0 - smoothstep(0.0, radius, distance);
	return attenuation;
}

// Adhoc filter shrink. Each cascade further away from the camera should
// reduce the filter size because each pixel coverage enlarges in distant cascades.
float filterShrinkCoeff[4] = float[]( 1.0, 0.5, 0.25, 0.125 );

float CalculateDirectionalShadow
(
	vec3 pos,
	vec3 viewCamPos,
	mat4 lightProjView,
	vec2 shadowAtlasCoord,
	float shadowAtlasResRatio,
	int shadowAtlasLayer,
	float PCFRadius,
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

	float shadow = PCFFilterShadow2D
	(
		s_texture8,
		sampleCoord,
		startCoord,
		endCoord,
		PCFRadius / SHADOW_ATLAS_SIZE,
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
	float PCFRadius,
	float lightBleedReduction,
	float shadowBias,
  float lightDistance
)
{
	vec4 fragPosForLight = lightProjView * vec4(pos, 1.0);
	vec3 projCoord = fragPosForLight.xyz / fragPosForLight.w;
	projCoord = projCoord * 0.5 + 0.5;

	vec3 lightToFrag = pos - lightPos;
	float currFragDepth = lightDistance / shadowCameraFar;

	vec2 startCoord = shadowAtlasCoord;
	vec3 coord = vec3(startCoord + shadowAtlasResRatio * projCoord.xy, shadowAtlasLayer);

	return PCFFilterShadow2D
	(
		s_texture8,
		coord,
		startCoord,
		startCoord + shadowAtlasResRatio,
		PCFRadius / SHADOW_ATLAS_SIZE,
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
	float PCFRadius,
	float lightBleedReduction,
	float shadowBias,
	float precomputedDist
)
{
	vec3 lightToFrag = pos - lightPos;
	float currFragDepth = precomputedDist / shadowCameraFar;

	return PCFFilterOmni
	(
		s_texture8,
		shadowAtlasCoord,
		shadowAtlasResRatio,
		shadowAtlasLayer,
		lightToFrag,
		PCFRadius / SHADOW_ATLAS_SIZE,
		currFragDepth,
		lightBleedReduction,
		shadowBias
	);
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
	float roughness
)
{
	vec3 irradiance = vec3(0.0);

	for (int i = 0; i < GetActiveDirectionalLightCount(); i++)
	{
		DirectionalLightData light = directionalLightArray[i];

		float resRatio = light.shadowResolution / graphicConstants.shadowAtlasSize;

		vec3 lightDir = normalize(-light.direction);
		vec3 Lo = PBR(normal, fragToEye, albedo, metallic, roughness, lightDir, light.color * light.intensity);

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

			float rad = light.pcfRadius * filterShrinkCoeff[cascadeOfThisPixel];

			shadow = CalculateDirectionalShadow
			(
				fragPos,
				viewCamPos,
				directionalLightPVMArray[i].projectionViewMatrices[cascadeOfThisPixel],
				coord / graphicConstants.shadowAtlasSize,
				resRatio,
				layer,
				rad,
				light.bleedingReduction,
				light.shadowBias
			);
		}

		irradiance += Lo * shadow * cascadeMultiplier;
	}

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
		float attenuation = Attenuation(lightDistance, light.radius);

		vec3 lightDir = fragToLight / lightDistance;
		vec3 Lo = PBR(normal, fragToEye, albedo, metallic, roughness, lightDir, light.color * light.intensity);

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
				light.pcfRadius,
				light.bleedingReduction,
				light.shadowBias,
				lightDistance
			);
		}

		irradiance += Lo * shadow * attenuation;
	}

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
		float attenuation = Attenuation(lightDistance, light.radius);

		// Spotlight cone falloff
		vec3 lightDirNorm = fragToLight / lightDistance;
		float theta = dot(-lightDirNorm, light.direction);
		float epsilon = light.outerAngle - light.innerAngle;
		float intensity = clamp((theta - light.outerAngle) / epsilon, 0.0, 1.0);

		vec3 Lo = PBR(normal, fragToEye, albedo, metallic, roughness, lightDirNorm, light.color * light.intensity);

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
				light.pcfRadius,
				light.bleedingReduction,
				light.shadowBias,
				lightDistance
			);
		}

		irradiance += Lo * shadow * intensity * attenuation;
	}

	return irradiance;
}

#endif
	-->
	</source>
</shader>