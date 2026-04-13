<shader>
	<type name = "includeShader" />
	<uniform name = "drawCommand" size = "24" />
	<source>
	<!--

	#ifndef DRAW_DATA
	#define DRAW_DATA

	// DrawCommand
	//////////////////////////////////////////

	uniform vec4 drawCommand[24];

	// --- Global accessors ---

	bool IsIBLInUse()
	{
		return bool(drawCommand[0].x > 0.5);
	}

	bool IsAmbientOcculusionInUse()
	{
		return bool(drawCommand[0].y > 0.5);
	}

	float GetSkyIntensity()
	{
		return drawCommand[0].z;
	}

	float GetSkyIblMode()
	{
		return drawCommand[0].w;
	}

	bool IsSkyDiffuseEnabled()
	{
		float m = GetSkyIblMode();
		return (m < 0.5 || m > 1.5);
	}

	bool IsSkySpecularEnabled()
	{
		return (GetSkyIblMode() < 1.5);
	}

	int GetActivePointLightCount()
	{
		return int(drawCommand[1].x);
	}

	int GetActiveSpotLightCount()
	{
		return int(drawCommand[1].y);
	}

	int GetActiveDirectionalLightCount()
	{
		return int(drawCommand[1].z);
	}

	// --- Per-volume accessors ---
	// Volume 0 starts at index 2, volume 1 starts at index 13. Stride = 11.

	int VolumeBase(int vol)
	{
		return 2 + vol * 11;
	}

	float GetVolumeIntensity(int vol)
	{
		return drawCommand[VolumeBase(vol)].x;
	}

	float GetVolumeFadeDistance(int vol)
	{
		return drawCommand[VolumeBase(vol)].y;
	}

	float GetVolumeIblMode(int vol)
	{
		return drawCommand[VolumeBase(vol)].z;
	}

	bool IsVolumePccEnabled(int vol)
	{
		return bool(drawCommand[VolumeBase(vol)].w > 0.5);
	}

	bool IsVolumeDiffuseEnabled(int vol)
	{
		float m = GetVolumeIblMode(vol);
		return (m < 0.5 || m > 1.5);
	}

	bool IsVolumeSpecularEnabled(int vol)
	{
		return (GetVolumeIblMode(vol) < 1.5);
	}

	vec3 GetVolumeMin(int vol)
	{
		return drawCommand[VolumeBase(vol) + 1].xyz;
	}

	vec3 GetVolumeMax(int vol)
	{
		return drawCommand[VolumeBase(vol) + 2].xyz;
	}

	mat4 GetVolumeInverseTransform(int vol)
	{
		int b = VolumeBase(vol) + 3;
		return mat4(drawCommand[b], drawCommand[b+1], drawCommand[b+2], drawCommand[b+3]);
	}

	mat4 GetVolumeWorldTransform(int vol)
	{
		int b = VolumeBase(vol) + 7;
		return mat4(drawCommand[b], drawCommand[b+1], drawCommand[b+2], drawCommand[b+3]);
	}

	// Compute per-pixel blend factor for a given volume.
	// Returns 1.0 inside, fades to 0.0 at the edge, 0.0 outside.
	float ComputeVolumeBlendFactor(int vol, vec3 worldPos)
	{
		float fadeDist = GetVolumeFadeDistance(vol);
		if (fadeDist <= 0.0)
		{
			// Boundless volume (e.g. sky). Intensity > 0 means always active.
			return (GetVolumeIntensity(vol) > 0.0) ? 1.0 : 0.0;
		}

		vec3 localPos = (GetVolumeInverseTransform(vol) * vec4(worldPos, 1.0)).xyz;

		vec3 vMin = GetVolumeMin(vol);
		vec3 vMax = GetVolumeMax(vol);

		vec3 distToMin = localPos - vMin;
		vec3 distToMax = vMax - localPos;
		vec3 minDist = min(distToMin, distToMax);
		float edgeDist = min(minDist.x, min(minDist.y, minDist.z));

		return clamp(edgeDist / fadeDist, 0.0, 1.0);
	}

	// Defines
	//////////////////////////////////////////

	#define MAX_CASCADE_COUNT 4

	#define DIRECTIONAL_LIGHT_CACHE_ITEM_COUNT 12
	#define MAX_DIRECTIONAL_LIGHT_PER_OBJECT 8
	#define POINT_LIGHT_CACHE_ITEM_COUNT 32
	#define MAX_POINT_LIGHT_PER_OBJECT 24
	#define SPOT_LIGHT_CACHE_ITEM_COUNT 32
	#define MAX_SPOT_LIGHT_PER_OBJECT 24

	// Graphic Constants Data
	//////////////////////////////////////////

	struct GraphicConstatsDataLayout
	{
		float shadowDistance;
		float shadowAtlasSize;
		int iblMaxReflectionLod;
		int cascadeCount;
		vec4 cascadeDistances;
	};

	layout(std140) uniform GraphicConstatsData
	{
		GraphicConstatsDataLayout graphicConstants;
	};

	// Directional Light Data
	//////////////////////////////////////////

	#define COMMON_LIGHT_DATA												\
	vec3 color;																\
	float intensity;														\
	vec3 position;															\
	int castShadow;															\
	float shadowBias;														\
	float bleedingReduction;												\
	float padx;																\
	float pady;																\
	vec2 shadowAtlasCoord;													\
	float shadowAtlasResRatio;												\
	int shadowAtlasLayer;
	
	struct DirectionalLightData
	{
		COMMON_LIGHT_DATA
    
		// Directional light specific fields
		vec3 direction;
		float pad0;
	};

	layout(std140) uniform DirectionalLightBuffer
	{
		DirectionalLightData directionalLightArray[DIRECTIONAL_LIGHT_CACHE_ITEM_COUNT];
	};

	struct DirectionalLightPVMData
	{
		mat4 projectionViewMatrices[MAX_CASCADE_COUNT];
	};

	layout(std140) uniform DirectionalLightPVMBuffer
	{
		DirectionalLightPVMData directionalLightPVMArray[DIRECTIONAL_LIGHT_CACHE_ITEM_COUNT];
	};

	struct PointLightData
	{
		COMMON_LIGHT_DATA

		// Point light specific fields
		float radius;
	};

	layout(std140) uniform PointLightCache
	{
		PointLightData pointLightArray[POINT_LIGHT_CACHE_ITEM_COUNT];
	};

	struct SpotLightData
	{
		COMMON_LIGHT_DATA

		// Spot light specific fields
		vec3 direction;
		float radius;

		float outerAngle;
		float innerAngle;
	
		mat4 projectionViewMatrix;
	};

	layout(std140) uniform SpotLightCache
	{
		SpotLightData spotLightArray[SPOT_LIGHT_CACHE_ITEM_COUNT];
	};

	#endif // DRAW_DATA

	-->
	</source>
</shader>