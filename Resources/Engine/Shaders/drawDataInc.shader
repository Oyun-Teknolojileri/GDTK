<shader>
	<type name = "includeShader" />
	<include name = "materialCacheInc.shader" />
	<uniform name = "drawCommand" size = "2" />
	<source>
	<!--
	
	#ifndef DRAW_DATA
	#define DRAW_DATA

	// DrawCommand
	//////////////////////////////////////////

	uniform vec4 drawCommand[2];

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

	// Defines
	//////////////////////////////////////////
	
	#define MAX_CASCADE_COUNT 4
	#define SHADOW_ATLAS_SIZE 2048.0

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
	float pcfRadius;														\
	int pcfSamples;															\
	vec2 shadowAtlasCoord;													\
	float shadowResolution;													\
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

		float innerAngle;
		float outerAngle;
	
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