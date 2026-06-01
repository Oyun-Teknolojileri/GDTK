<shader>
	<type name = "includeShader" />
	<include name = "vulkanCompatInc.shader" />
	<include name = "perDrawDataInc.shader" />
	<uniform slot = "1" name = "GraphicConstatsData" />
	<uniform slot = "3" name = "DirectionalLightBuffer" />
	<uniform slot = "4" name = "PointLightCache" />
	<uniform slot = "5" name = "SpotLightCache" />
	<uniform slot = "6" name = "DirectionalLightPVMBuffer" />
	<source>
	<!--

	#ifndef DRAW_DATA
	#define DRAW_DATA

	// DrawCommand accessors — back-end is now `perDraw._drawCommand` (PerDrawData UBO, slot 2).
	// Function signatures kept identical so every call site (lighting/ibl/AO) stays untouched.
	// Volume index branches with a ternary; the GLSL compiler folds it when `vol` is constant.
	//////////////////////////////////////////

	// --- Global accessors ---

	bool IsIBLInUse()
	{
		return perDraw._drawCommand.global0.x > 0.5;
	}

	bool IsAmbientOcculusionInUse()
	{
		return perDraw._drawCommand.global0.y > 0.5;
	}

	float GetSkyIntensity()
	{
		return perDraw._drawCommand.global0.z;
	}

	int GetActivePointLightCount()
	{
		return int(perDraw._drawCommand.global1.x);
	}

	int GetActiveSpotLightCount()
	{
		return int(perDraw._drawCommand.global1.y);
	}

	int GetActiveDirectionalLightCount()
	{
		return int(perDraw._drawCommand.global1.z);
	}

	// --- Per-volume accessors ---

	float GetVolumeIntensity(int vol)
	{
		return vol == 0 ? perDraw._drawCommand.vol0Params.x : perDraw._drawCommand.vol1Params.x;
	}

	float GetVolumeFadeDistance(int vol)
	{
		return vol == 0 ? perDraw._drawCommand.vol0Params.y : perDraw._drawCommand.vol1Params.y;
	}

	bool IsVolumePccEnabled(int vol)
	{
		float v = vol == 0 ? perDraw._drawCommand.vol0Params.w : perDraw._drawCommand.vol1Params.w;
		return v > 0.5;
	}

	bool IsVolumeInterior(int vol)
	{
		float v = vol == 0 ? perDraw._drawCommand.vol0Params.z : perDraw._drawCommand.vol1Params.z;
		return v > 0.5;
	}

	vec3 GetVolumeMin(int vol)
	{
		return (vol == 0 ? perDraw._drawCommand.vol0Min : perDraw._drawCommand.vol1Min).xyz;
	}

	vec3 GetVolumeMax(int vol)
	{
		return (vol == 0 ? perDraw._drawCommand.vol0Max : perDraw._drawCommand.vol1Max).xyz;
	}

	mat4 GetVolumeInverseTransform(int vol)
	{
		if (vol == 0)
		{
			return mat4(perDraw._drawCommand.vol0InvT0,
			            perDraw._drawCommand.vol0InvT1,
			            perDraw._drawCommand.vol0InvT2,
			            perDraw._drawCommand.vol0InvT3);
		}
		return mat4(perDraw._drawCommand.vol1InvT0,
		            perDraw._drawCommand.vol1InvT1,
		            perDraw._drawCommand.vol1InvT2,
		            perDraw._drawCommand.vol1InvT3);
	}

	mat4 GetVolumeWorldTransform(int vol)
	{
		if (vol == 0)
		{
			return mat4(perDraw._drawCommand.vol0WldT0,
			            perDraw._drawCommand.vol0WldT1,
			            perDraw._drawCommand.vol0WldT2,
			            perDraw._drawCommand.vol0WldT3);
		}
		return mat4(perDraw._drawCommand.vol1WldT0,
		            perDraw._drawCommand.vol1WldT1,
		            perDraw._drawCommand.vol1WldT2,
		            perDraw._drawCommand.vol1WldT3);
	}

	// Compute per-pixel blend factor for a local volume.
	// Returns 1.0 inside, fades to 0.0 at the edge, 0.0 outside.
	float ComputeVolumeBlendFactor(int vol, vec3 worldPos)
	{
		float intensity = GetVolumeIntensity(vol);
		if (intensity <= 0.0)
		{
			return 0.0;
		}

		float fadeDist = GetVolumeFadeDistance(vol);

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

	TK_UBO_BINDING(1) uniform GraphicConstatsData
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

	TK_UBO_BINDING(3) uniform DirectionalLightBuffer
	{
		DirectionalLightData directionalLightArray[DIRECTIONAL_LIGHT_CACHE_ITEM_COUNT];
	};

	struct DirectionalLightPVMData
	{
		mat4 projectionViewMatrices[MAX_CASCADE_COUNT];
	};

	TK_UBO_BINDING(6) uniform DirectionalLightPVMBuffer
	{
		DirectionalLightPVMData directionalLightPVMArray[DIRECTIONAL_LIGHT_CACHE_ITEM_COUNT];
	};

	struct PointLightData
	{
		COMMON_LIGHT_DATA

		// Point light specific fields
		float radius;
	};

	TK_UBO_BINDING(4) uniform PointLightCache
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

	TK_UBO_BINDING(5) uniform SpotLightCache
	{
		SpotLightData spotLightArray[SPOT_LIGHT_CACHE_ITEM_COUNT];
	};

	#endif // DRAW_DATA

	-->
	</source>
</shader>
