<shader>
	<type name = "includeShader" />
	<include name = "vulkanCompatInc.shader" />
	<texture slot = "14" name = "s_instanceData" />
	<source>
	<!--
  #ifndef INSTANCE_DATA_INC
  #define INSTANCE_DATA_INC

  // Instance data transport — Phase 2a proof.
  //
  // This include fetches per-instance records out of an `RGBA32F` instance data texture via
  // `texelFetch` (VTF) — the GL/WebGL backing of the `InstanceDataBuffer` RHI abstraction
  // (see `rendering-roadmap.md` §Phase 2a + §5). It is the GL/WebGL counterpart of the SSBO
  // read that native GLES 3.2 + Vulkan will use through the SAME `LoadInstance(id)` interface;
  // only this include changes when the transport swaps, never the shader body that calls it.
  //
  // 2a scope: `InstanceRecord` is a *throwaway, byte-for-byte mirror* of `PerDrawUboLayout`
  // (`ToolKit/Render/Renderer.h`). The 2a vertex shader consumes only `_model` and
  // `_inverseTransposeModel` (both `mat4`, pure float); the fragment shader stays on the
  // per-draw UBO, so within one frame the UBO and the instance texture hold identical bytes
  // and the fragment reads bytes identical to legacy. This isolates transport correctness
  // (texelFetch mat4 reconstruction) — exactly what 2a exists to catch. The lean split +
  // `RenderObject` + global tables replace this record in 2b.
  //
  // Mirroring rule (roadmap §5): field order/offset must stay byte-identical to the C++
  // `InstanceRecord2a` (= `PerDrawUboLayout`). The C++ static_asserts
  // (`sizeof == 70*16`, `alignof >= 16`, `offsetof(model) == 0`) anchor this GLSL layout; if
  // either side changes, both must change in lockstep.

  // Instance-id spelling differs across GLSL dialects. Vulkan/SPIR-V exposes gl_InstanceIndex
  // (which already includes the base instance); GL ES 3.00 exposes gl_InstanceID (which does
  // not). TK_INSTANCE_ID is the caller's spelling; the desktop-GL gl_InstanceID-excludes-base
  // gotcha is absorbed here.
#ifdef VULKAN
  #define TK_INSTANCE_ID gl_InstanceIndex
#else
  #define TK_INSTANCE_ID gl_InstanceID
#endif

  // Texel stride per instance, in RGBA32F texels. MUST match the C++ value
  // `InstanceRecord2aStride` (== sizeof(PerDrawUboLayout) / 16 == 70). Asserted in Renderer.h.
  #define TK_INSTANCE_STRIDE 70u

  // Row width of the instance data texture. MUST match `TextureBuffer::Resize`'s
  // `maxTextureWidth` (TextureBuffer.h, currently 1024) — the 2D wrap below depends on it.
  // These two `1024`s are a cross-file mirroring invariant.
  #define TK_INSTANCE_TEX_WIDTH 1024u

  // Sampler is declared UNGATED so both the TK_INSTANCED=0 and =1 program variants carry the
  // resource declaration, keeping this GLSL in lockstep with the `<texture slot="14">` metadata
  // above and avoiding a "declared resource absent from SPIR-V" Vulkan validation error.
  TK_SAMPLER_BINDING(14) uniform sampler2D s_instanceData;

  // 2a throwaway mirror of PerDrawUboLayout (Renderer.h:151). Field order is byte-exact:
  //   6 mat4 (24 texels) + 1 vec4 + DrawCommand (24 vec4) + MaterialData (4 vec4)
  //   + 6 ivec4 + 6 ivec4 + 1 ivec4 + 4 vec4  == 70 RGBA32F texels == 1120 bytes.
  // ivec4 light-index fields are stored as float bits in the RGBA32F texture, so they are
  // reconstructed with floatBitsToInt (roadmap §Phase 2a, Risk 1). They are unused by the 2a
  // vertex path but carried so the record is a faithful transport mirror.
  struct InstanceRecord
  {
    mat4 model;
    mat4 modelWithoutTranslate;
    mat4 inverseModel;
    mat4 inverseTransposeModel;
    mat4 iblRotation;
    mat4 iblSecondaryRotation;
    vec4 viewportSizeAndPad;
    vec4 drawCommand[24];               // DrawCommand — 24 vec4, transported opaquely in 2a
    vec4 materialData[4];               // MaterialCacheItem::Data — 4 vec4
    ivec4 activePointLightIndices[6];
    ivec4 activeSpotLightIndices[6];
    ivec4 lightCounts;
    vec4 keyFrameData;
    vec4 blendFrameData;
    vec4 skinParams;
    vec4 animBlendFactorAndPad;
  };

  // Map a linear texel index to the 2D coordinate under the fixed-width texture layout.
  ivec2 instTexel(uint linear)
  {
    return ivec2(linear % TK_INSTANCE_TEX_WIDTH, linear / TK_INSTANCE_TEX_WIDTH);
  }

  // Fetch primitives at an absolute linear texel offset.
  vec4  instFetchVec4 (uint linear) { return texelFetch(s_instanceData, instTexel(linear), 0); }
  ivec4 instFetchIVec4(uint linear) { return floatBitsToInt(texelFetch(s_instanceData, instTexel(linear), 0)); }
  mat4  instFetchMat4 (uint linear)
  {
    return mat4(instFetchVec4(linear), instFetchVec4(linear + 1u),
                instFetchVec4(linear + 2u), instFetchVec4(linear + 3u));
  }

  // Fetch one instance record. `id` is the caller-composed instance id
  // (gl_InstanceID/baseInstance in 2a; firstInstance for the Phase 8 indirect path). baseInstance
  // is always 0 in 2a, so the call site passes `TK_INSTANCE_ID` directly; the per-draw base
  // uniform is intentionally NOT declared here (a loose `uniform uint` has no Vulkan/SPIR-V
  // counterpart and the engine hosts all per-draw values in the PerDrawData UBO) — it lands in
  // Phase 8 via the indirect draw's firstInstance, designed in through this explicit-id API.
  InstanceRecord LoadInstance(uint id)
  {
    InstanceRecord r;
    uint o = id * TK_INSTANCE_STRIDE;

    r.model                   = instFetchMat4(o);  o += 4u;
    r.modelWithoutTranslate   = instFetchMat4(o);  o += 4u;
    r.inverseModel            = instFetchMat4(o);  o += 4u;
    r.inverseTransposeModel   = instFetchMat4(o);  o += 4u;
    r.iblRotation             = instFetchMat4(o);  o += 4u;
    r.iblSecondaryRotation    = instFetchMat4(o);  o += 4u;

    r.viewportSizeAndPad      = instFetchVec4(o);  o += 1u;

    for (int i = 0; i < 24; i++) { r.drawCommand[i] = instFetchVec4(o + uint(i)); }  o += 24u;
    for (int i = 0; i < 4;  i++) { r.materialData[i] = instFetchVec4(o + uint(i)); } o += 4u;

    for (int i = 0; i < 6;  i++) { r.activePointLightIndices[i] = instFetchIVec4(o + uint(i)); } o += 6u;
    for (int i = 0; i < 6;  i++) { r.activeSpotLightIndices[i]  = instFetchIVec4(o + uint(i)); } o += 6u;

    r.lightCounts             = instFetchIVec4(o); o += 1u;
    r.keyFrameData            = instFetchVec4(o);  o += 1u;
    r.blendFrameData          = instFetchVec4(o);  o += 1u;
    r.skinParams              = instFetchVec4(o);  o += 1u;
    r.animBlendFactorAndPad   = instFetchVec4(o);  o += 1u;

    return r;
  }

  #endif // INSTANCE_DATA_INC
	-->
	</source>
</shader>
