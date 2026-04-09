<shader>
	<type name = "includeShader" />
	<uniform name = "drawCommand" size = "12" />
	<source>
	<!--

	#ifndef DRAW_DATA
	#define DRAW_DATA

	// DrawCommand
	//////////////////////////////////////////

	uniform vec4 drawCommand[12];

	float GetIBLIntensity()
	{
		return drawCommand[0].x;
	}

	bool IsIBLInUse()
	{
		return bool(drawCommand[0].y > 0.5);
	}

	bool IsAmbientOcculusionInUse()
	{
		return bool(drawCommand[0].z > 0.5);
	}

	float GetSecondaryIBLIntensity()
	{
		return drawCommand[0].w;
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

	float GetIBLFadeDistance()
	{
		return drawCommand[1].w;
	}

	vec3 GetPrimaryVolumeMin()
	{
		return drawCommand[2].xyz;
	}

	bool IsParallaxCorrectedCubemapEnabled()
	{
		return bool(drawCommand[2].w > 0.5);
	}

	vec3 GetPrimaryVolumeMax()
	{
		return drawCommand[3].xyz;
	}

	mat4 GetIblInverseVolumeTransform()
	{
		return mat4(drawCommand[4], drawCommand[5], drawCommand[6], drawCommand[7]);
	}

	mat4 GetIblVolumeTransform()
	{
		return mat4(drawCommand[8], drawCommand[9], drawCommand[10], drawCommand[11]);
	}

	// Compute per-pixel IBL blend factor from fragment world position.
	// Returns 1.0 at volume center (fully primary), 0.0 at volume edge (fully secondary).
	// Uses OBB: transforms worldPos to volume local space before distance check.
	float ComputeIBLBlendFactor(vec3 worldPos)
	{
		float fadeDist = GetIBLFadeDistance();
		if (fadeDist <= 0.0)
		{
			return 1.0;
		}

		// Transform world position to volume local space.
		vec3 localPos = (GetIblInverseVolumeTransform() * vec4(worldPos, 1.0)).xyz;

		vec3 vMin = GetPrimaryVolumeMin();
		vec3 vMax = GetPrimaryVolumeMax();

		// Distance from each face of the OBB (in local space).
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