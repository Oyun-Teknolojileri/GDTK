<shader>
	<type name = "includeShader" />
	<include name = "textureUtil.shader" />
	<include name = "shadow.shader" />
	<include name = "pbr.shader" />
	<include name = "drawDataInc.shader" />
	<define name = "highlightCascades" val = "0,1" />
	<define name = "ShadowSampleCount" val="1,9,25,49" />
	<uniform name = "activePointLightIndexes" size = "32" />
	<uniform name = "activeSpotLightIndexes" size = "32" />
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

bool EpsilonEqual(float a, float b, float eps)
{
	return abs(a - b) < eps;
}

float CalculateDirectionalShadow
(
	vec3 pos, 
	vec3 viewCamPos, 
	mat4 lightProjView, 
	vec2 shadowAtlasCoord, // Shadow map start location in the layer in uv space.
	float shadowAtlasResRatio, // shadowAtlasResRatio = shadow map resolution / shadow atlas resolution.
	int shadowAtlasLayer,
	float PCFRadius, 
	float lightBleedReduction, 
	float shadowBias
)
{
	vec4 fragPosForLight = lightProjView * vec4(pos, 1.0);
	vec3 projCoord = fragPosForLight.xyz;
	projCoord = projCoord * 0.5 + 0.5;

	// Get depth of the current fragment according to lights view
	float currFragDepth = projCoord.z;

	vec2 startCoord = shadowAtlasCoord; // Start coordinate of he shadow map in the atlas.
	vec2 endCoord = shadowAtlasCoord + shadowAtlasResRatio; // End coordinate of the shadow map in the atlas.

	// Find the sample's location in the shadow atlas.
	
	// To do so, we scale the coordinates by the proportion of the shadow map in the atlas. "shadowAtlasResRatio * projCoord.xy"
	// and then offset the scaled coordinate to the beginning of the shadow map via "startCoord + shadowAtlasResRatio * projCoord.xy"
	// which gives us the final uv coordinates in xy and the index of the layer in z
	vec2 uvInAtlas = startCoord + shadowAtlasResRatio * projCoord.xy;
	vec3 sampleCoord = vec3(uvInAtlas, shadowAtlasLayer);

	float shadow = 1.0;
	shadow = PCFFilterShadow2D
	(
		s_texture8,
		sampleCoord,
		startCoord,
		endCoord,
		ShadowSampleCount,
		PCFRadius / SHADOW_ATLAS_SIZE, // Convert radius in pixel units to uv.
		projCoord.z,
		lightBleedReduction,
		shadowBias
	);

	// Fade shadow out after min shadow fade out distance
	vec3 camToPos = pos - viewCamPos;
	float fade = (length(camToPos) - (graphicConstants.shadowDistance * shadowFadeOutDistanceNorm)) / (graphicConstants.shadowDistance * (1.0 - shadowFadeOutDistanceNorm));
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
	float shadowBias
)
{
	vec4 fragPosForLight = lightProjView * vec4(pos, 1.0);
	vec3 projCoord = fragPosForLight.xyz / fragPosForLight.w;
	projCoord = projCoord * 0.5 + 0.5;

	vec3 lightToFrag = pos - lightPos;
	float currFragDepth = length(lightToFrag) / shadowCameraFar;

	vec2 startCoord = shadowAtlasCoord;
	vec3 coord = vec3(startCoord + shadowAtlasResRatio * projCoord.xy, shadowAtlasLayer);

	return PCFFilterShadow2D
	(
		s_texture8,
		coord,
		startCoord,
		startCoord + shadowAtlasResRatio,
		ShadowSampleCount,
		PCFRadius / SHADOW_ATLAS_SIZE, // Convert radius in pixel units to uv.
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
	float shadowBias
)
{
	vec3 lightToFrag = pos - lightPos;
	float currFragDepth = length(lightToFrag) / shadowCameraFar;

	return PCFFilterOmni
	(
		s_texture8,
		shadowAtlasCoord,
		shadowAtlasResRatio,
		shadowAtlasLayer,
		lightToFrag,
		ShadowSampleCount,
		PCFRadius / SHADOW_ATLAS_SIZE, // Convert radius in pixel units to uv.
		currFragDepth,
		lightBleedReduction,
		shadowBias
	);
}

// Returns 0 or 1
float RadiusCheck(float radius, float distance)
{
	float radiusCheck = clamp(radius - distance, 0.0, 1.0);
	radiusCheck = ceil(radiusCheck);
	return radiusCheck;
}

float Attenuation(float distance, float radius, float constant, float linear, float quadratic)
{
	float attenuation = 1.0 / (constant + linear * distance + quadratic * (distance * distance));

	// Decrease attenuation heavily near radius
	attenuation *= 1.0 - smoothstep(0.0, radius, distance);
	return attenuation;
}

// Adhoc filter shrink. Each cascade further away from the camera should
// reduce the filter size because each pixel coverage enlarges in distant cascades.
// MJP has a more matematically found way in his shadow sample.
float filterShrinkCoeff[4] = float[]( 1.0, 0.5, 0.25, 0.125 );

vec3 PBRLighting
(
	vec3 fragPos,				// World space fragment position.
	float viewPosDepth, // View space depth. ( Fragment's distance to camera )
	vec3 normal,				// Fragment's normal in world space. ( can be the geometry normal, or normal map normal )
	vec3 fragToEye,			// Normalized vector from fragment position to camera position in world space normalize( campos - fragPos )
	vec3 viewCamPos,		// Camera position in world space.
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
		
		// lighting
		vec3 lightDir = normalize(-light.direction);
		vec3 Lo = PBR(fragPos, normal, fragToEye, albedo, metallic, roughness, lightDir, light.color * light.intensity);

		// shadow
		float depth = abs(viewPosDepth);
		float shadow = 1.0;

		vec3 cascadeMultiplier = vec3(1.0);

		if (light.castShadow == 1)
		{
			int numCascade = graphicConstants.cascadeCount;
			int cascadeOfThisPixel = numCascade - 1;

			// Cascade selection by depth range.
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
				cascadeMultiplier = vec3(4.0f, 1.0f, 1.0f);
			}
			else if (cascadeOfThisPixel == 1)
			{
				cascadeMultiplier = vec3(1.0f, 4.0f, 1.0f);
			}
			else if (cascadeOfThisPixel == 2)
			{
				cascadeMultiplier = vec3(1.0f, 1.0f, 4.0f);
			}
			else if (cascadeOfThisPixel == 3)
			{
				cascadeMultiplier = vec3(4.0f, 4.0f, 1.0f);
			}
#endif

			int layer = 0;
			vec2 coord = vec2(0.0);
			ShadowAtlasLut(light.shadowResolution, light.shadowAtlasCoord, cascadeOfThisPixel, layer, coord);

			layer += light.shadowAtlasLayer;

			float rad = light.pcfRadius;
			rad = rad * filterShrinkCoeff[cascadeOfThisPixel];

			shadow = CalculateDirectionalShadow
			(
				fragPos, 
				viewCamPos,
				directionalLightPVMArray[i].projectionViewMatrices[cascadeOfThisPixel],
				coord / graphicConstants.shadowAtlasSize, // Convert to uv
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
		
		float resRatio = light.shadowResolution / graphicConstants.shadowAtlasSize;
		
		// radius check and attenuation
		float lightDistance = length(light.position - fragPos);
		float radiusCheck = RadiusCheck(light.radius, lightDistance);
		float attenuation = Attenuation(lightDistance, light.radius, 1.0, 0.09, 0.032);

			// lighting
		vec3 lightDir = normalize(light.position - fragPos);
		vec3 Lo = PBR(fragPos, normal, fragToEye, albedo, metallic, roughness, lightDir, light.color * light.intensity);

			// shadow
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
					light.shadowBias
				);
		}

		irradiance += Lo * shadow * attenuation * radiusCheck;
	}
	
	for (int i = 0; i < GetActiveSpotLightCount(); i++)
	{
		int ii = activeSpotLightIndexes[i];
		SpotLightData light = spotLightArray[ii];
		
		float resRatio = light.shadowResolution / graphicConstants.shadowAtlasSize;
		
		// radius check and attenuation
		vec3 fragToLight = light.position - fragPos;
		float lightDistance = length(fragToLight);
		float radiusCheck = RadiusCheck(light.radius, lightDistance);
		float attenuation = Attenuation(lightDistance, light.radius, 1.0, 0.09, 0.032);

		// Lighting angle and falloff
		float theta = dot(-normalize(fragToLight), light.direction);
		float epsilon = light.outerAngle - light.innerAngle;
		float intensity = clamp((theta - light.outerAngle) / epsilon, 0.0, 1.0);

		// lighting
		vec3 lightDir = normalize(-light.direction);
		vec3 Lo = PBR(fragPos, normal, fragToEye, albedo, metallic, roughness, lightDir, light.color * light.intensity);

		// shadow
		float shadow = 1.0;
		if (light.castShadow == 1)
		{
			shadow = CalculateSpotShadow
				(
					fragPos,
					light.position,
					light.projectionViewMatrix,
					light.radius,
					light.shadowAtlasCoord / graphicConstants.shadowAtlasSize, // Convert to uv
					resRatio,
					light.shadowAtlasLayer,
					light.pcfRadius,
					light.bleedingReduction,
					light.shadowBias
				);
		}

		irradiance += Lo * shadow * intensity * radiusCheck * attenuation;
	}
	
	return irradiance;
}

#endif

	-->
	</source>
</shader>