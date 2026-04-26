<shader>
	<type name = "includeShader" />
	<source>
	<!--
#ifndef PER_DRAW_DATA
#define PER_DRAW_DATA

// PerDrawData UBO — slot 6 (Vulkan binding 14 after shaderc remap)
//
// Mirror of `PerDrawUboLayout` in Renderer.h. Add or reorder fields ONLY in lockstep — shader
// and C++ are byte-identical std140 layouts.
//
// Bare uniforms (`uniform mat4 model;` etc.) are deprecated. Any shader that needs per-draw
// data should `<include name="perDrawDataInc.shader"/>` and read these fields. Texture
// samplers (`uniform sampler2D s_textureN;`) stay outside the UBO — they cannot live in a
// uniform block.
//////////////////////////////////////////

struct DrawCommandLayout
{
	vec4 global0;     // x: iblInUse, y: aoInUse, z: skyIntensity, w: pad
	vec4 global1;     // x: pointLightCount, y: spotLightCount, z: directionalLightCount, w: pad

	vec4 vol0Params;  // x: intensity, y: fadeDistance, z: interior, w: pccEnabled
	vec4 vol0Min;
	vec4 vol0Max;
	vec4 vol0InvT0;
	vec4 vol0InvT1;
	vec4 vol0InvT2;
	vec4 vol0InvT3;
	vec4 vol0WldT0;
	vec4 vol0WldT1;
	vec4 vol0WldT2;
	vec4 vol0WldT3;

	vec4 vol1Params;
	vec4 vol1Min;
	vec4 vol1Max;
	vec4 vol1InvT0;
	vec4 vol1InvT1;
	vec4 vol1InvT2;
	vec4 vol1InvT3;
	vec4 vol1WldT0;
	vec4 vol1WldT1;
	vec4 vol1WldT2;
	vec4 vol1WldT3;
};

struct MaterialDataLayout
{
	vec4 colorAlpha;        // rgb=color, a=alpha
	vec4 emissiveThreshold; // rgb=emissive, a=alphaMaskThreshold
	vec4 metallicRoughness; // x=metallic, y=roughness, z=useAlphaMask, w=diffuseInUse
	vec4 textureFlags;      // x=emissiveInUse, y=normalInUse, z=metallicRoughInUse, w=pad
};

layout(std140) uniform PerDrawData
{
	mat4 _model;
	mat4 _modelWithoutTranslate;
	mat4 _inverseModel;
	mat4 _inverseTransposeModel;
	mat4 _iblRotation;
	mat4 _iblSecondaryRotation;

	vec4 _viewportSizeAndPad;          // .xy = viewportSize

	DrawCommandLayout _drawCommand;
	MaterialDataLayout _materialData;

	ivec4 _activePointLightIndices[6]; // 24 ints packed
	ivec4 _activeSpotLightIndices[6];
	ivec4 _lightCounts;                // .x = pointCount, .y = spotCount

	vec4 _keyFrameData;
	vec4 _blendFrameData;
	vec4 _skinParams;
	vec4 _animBlendFactorAndPad;       // .x = animationBlendFactor
} perDraw;

// --- Public accessors — keep call sites identical to the old bare-uniform names ---

#define model                   perDraw._model
#define modelWithoutTranslate   perDraw._modelWithoutTranslate
#define inverseModel            perDraw._inverseModel
#define inverseTransposeModel   perDraw._inverseTransposeModel
#define iblRotation             perDraw._iblRotation
#define iblSecondaryRotation    perDraw._iblSecondaryRotation

#define viewportSize            perDraw._viewportSizeAndPad.xy

#define skinParams              perDraw._skinParams
#define keyFrameData            perDraw._keyFrameData
#define blendFrameData          perDraw._blendFrameData
#define blendFactor             perDraw._animBlendFactorAndPad.x

int activePointLightIndex(int i) { return perDraw._activePointLightIndices[i >> 2][i & 3]; }
int activeSpotLightIndex(int i)  { return perDraw._activeSpotLightIndices[i >> 2][i & 3]; }

int activePointLightCount()      { return perDraw._lightCounts.x; }
int activeSpotLightCount()       { return perDraw._lightCounts.y; }

// --- DrawCommand accessors (mirror drawDataInc.shader's vec4-array decoders) ---

bool IsIBLInUse()                { return perDraw._drawCommand.global0.x > 0.5; }
bool IsAmbientOcculusionInUse()  { return perDraw._drawCommand.global0.y > 0.5; }
float GetSkyIntensity()          { return perDraw._drawCommand.global0.z; }

int GetActivePointLightCountUbo()       { return int(perDraw._drawCommand.global1.x); }
int GetActiveSpotLightCountUbo()        { return int(perDraw._drawCommand.global1.y); }
int GetActiveDirectionalLightCountUbo() { return int(perDraw._drawCommand.global1.z); }

// Per-volume — vol == 0 -> primary, vol == 1 -> secondary.
float GetVolumeIntensityUbo(int vol)   { return vol == 0 ? perDraw._drawCommand.vol0Params.x : perDraw._drawCommand.vol1Params.x; }
float GetVolumeFadeDistanceUbo(int vol){ return vol == 0 ? perDraw._drawCommand.vol0Params.y : perDraw._drawCommand.vol1Params.y; }
bool  IsVolumeInteriorUbo(int vol)     { return (vol == 0 ? perDraw._drawCommand.vol0Params.z : perDraw._drawCommand.vol1Params.z) > 0.5; }
bool  IsVolumePccEnabledUbo(int vol)   { return (vol == 0 ? perDraw._drawCommand.vol0Params.w : perDraw._drawCommand.vol1Params.w) > 0.5; }

vec3  GetVolumeMinUbo(int vol)         { return (vol == 0 ? perDraw._drawCommand.vol0Min : perDraw._drawCommand.vol1Min).xyz; }
vec3  GetVolumeMaxUbo(int vol)         { return (vol == 0 ? perDraw._drawCommand.vol0Max : perDraw._drawCommand.vol1Max).xyz; }

mat4 GetVolumeInverseTransformUbo(int vol)
{
	if (vol == 0)
		return mat4(perDraw._drawCommand.vol0InvT0, perDraw._drawCommand.vol0InvT1, perDraw._drawCommand.vol0InvT2, perDraw._drawCommand.vol0InvT3);
	else
		return mat4(perDraw._drawCommand.vol1InvT0, perDraw._drawCommand.vol1InvT1, perDraw._drawCommand.vol1InvT2, perDraw._drawCommand.vol1InvT3);
}

mat4 GetVolumeWorldTransformUbo(int vol)
{
	if (vol == 0)
		return mat4(perDraw._drawCommand.vol0WldT0, perDraw._drawCommand.vol0WldT1, perDraw._drawCommand.vol0WldT2, perDraw._drawCommand.vol0WldT3);
	else
		return mat4(perDraw._drawCommand.vol1WldT0, perDraw._drawCommand.vol1WldT1, perDraw._drawCommand.vol1WldT2, perDraw._drawCommand.vol1WldT3);
}

#endif // PER_DRAW_DATA
	-->
	</source>
</shader>
