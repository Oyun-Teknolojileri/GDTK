<shader>
	<type name = "includeShader" />
	<include name = "vulkanCompatInc.shader" />
	<uniform slot = "2" name = "PerDrawData" />
	<source>
	<!--
#ifndef PER_DRAW_DATA
#define PER_DRAW_DATA

// PerDrawData UBO — slot 2 (Vulkan binding 34 after UboBindingFor remap).
//
// Mirror of `PerDrawUboLayout` in Renderer.h. Add or reorder fields ONLY in lockstep — shader
// and C++ are byte-identical std140 layouts.
//
// Migrated shaders read these fields directly: `perDraw._model * localPos`,
// `perDraw._inverseTransposeModel`, etc. No accessor macros — those would clash with bare
// `uniform <type> <name>` declarations in not-yet-migrated includes (e.g. skinning.shader's
// `uniform vec4 skinParams;` is hit if we `#define skinParams perDraw._skinParams`).
// Migrate the include first, then call sites here can be tightened.
//
// Texture samplers stay outside the UBO — they cannot live in a uniform block.
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

TK_UBO_BINDING(2) uniform PerDrawData
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

#endif // PER_DRAW_DATA
	-->
	</source>
</shader>
