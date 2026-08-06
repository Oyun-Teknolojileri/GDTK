<shader>
	<type name = "includeShader" />
	<include name = "vulkanCompatInc.shader" />
	<include name = "materialTableInc.shader" />
	<include name = "envVolumeTableInc.shader" />
	<include name = "pointLightTableInc.shader" />
	<include name = "spotLightTableInc.shader" />
	<texture slot = "14" name = "s_instanceData" />
	<source>
	<!--
  #ifndef INSTANCE_DATA_INC
  #define INSTANCE_DATA_INC

  // Instance data transport — Phase 2b lean record.
  //
  // This include fetches per-instance records out of an `RGBA32F` instance data texture via
  // `texelFetch` (VTF) — the GL/WebGL backing of the `InstanceDataBuffer` RHI abstraction
  // (see `rendering-roadmap.md` §Phase 2b + §5). It is the GL/WebGL counterpart of the SSBO
  // read that native GLES 3.2 + Vulkan will use through the SAME `LoadInstance(id)` interface;
  // only this include changes when the transport swaps, never the shader body that calls it.
  //
  // 2b scope: lean `InstanceRecord` (~80 B, 5 texel): model (4 texels) + packed uint row
  // (lightListIndex, animKeyIndex, flags, _pad — 1 texel). The fragment shader reads shared
  // data from global tables (material/env/light-index/anim) + perDraw UBO (RenderObject
  // indices through step 6, then mini-UBO in step 7). The 2a throwaway 70-texel mirror of
  // PerDrawUboLayout is retired.
  //
  // Mirroring rule (roadmap §5): field order/offset must stay byte-identical to the C++
  // `InstanceRecord` (`Renderer.h`). The C++ static_asserts
  // (`sizeof == 5*16`, `alignof >= 16`, `offsetof(model) == 0`) anchor this GLSL layout.

  // Instance-id spelling differs across GLSL dialects.
#ifdef VULKAN
  #define TK_INSTANCE_ID int(gl_InstanceIndex)
#else
  #define TK_INSTANCE_ID int(gl_InstanceID)
#endif

  // Texel stride per instance, in RGBA32F texels. MUST match the C++ value
  // `InstanceRecordStride` (== sizeof(InstanceRecord) / 16 == 5). Asserted in Renderer.h.
  #define TK_INSTANCE_STRIDE 5

  // Row width of the instance data texture. MUST match `TextureBuffer::Resize`'s
  // `maxTextureWidth` (TextureBuffer.h, currently 1024) — the 2D wrap below depends on it.
  #define TK_INSTANCE_TEX_WIDTH 1024

  // Instance flags (mirrored in Renderer.h as InstanceFlags namespace).
  #define TK_INSTFLAG_UNIFORM_SCALE  1
  #define TK_INSTFLAG_IS_SKINNED     2
  #define TK_INSTFLAG_ENV_OVERRIDE   4

  // Sampler is declared UNGATED so both the TK_INSTANCED=0 and =1 program variants carry the
  // resource declaration.
  TK_SAMPLER_BINDING(14) uniform sampler2D s_instanceData;

  // Lean per-instance record — Phase 2b (see Renderer.h InstanceRecord).
  //   4 texels for model (mat4) + 1 texel for the packed row (vec4) = 5 RGBA32F texels = 80 bytes.
  struct InstanceRecord
  {
    mat4 model;     // texel 0-3: world matrix (always per-instance)
    vec4 packedRow; // texel 4: x=lightListIndex, y=animKeyIndex, z=flags, w=pad
  };

  // Map a linear texel index to the 2D coordinate under the fixed-width texture layout.
  ivec2 instTexel(int linear)
  {
    return ivec2(linear % TK_INSTANCE_TEX_WIDTH, linear / TK_INSTANCE_TEX_WIDTH);
  }

  // Fetch primitives at an absolute linear texel offset.
  vec4 instFetchVec4(int linear) { return texelFetch(s_instanceData, instTexel(linear), 0); }
  mat4 instFetchMat4(int linear)
  {
    return mat4(instFetchVec4(linear), instFetchVec4(linear + 1),
                instFetchVec4(linear + 2), instFetchVec4(linear + 3));
  }

  // Fetch one instance record. `id` is the caller-composed instance id
  // (gl_InstanceID/baseInstance in 2b; firstInstance for the Phase 8 indirect path).
  InstanceRecord LoadInstance(int id)
  {
    InstanceRecord r;
    int o = id * TK_INSTANCE_STRIDE;

    r.model     = instFetchMat4(o);  o += 4;
    r.packedRow = instFetchVec4(o); // o += 1

    return r;
  }

  #endif // INSTANCE_DATA_INC
	-->
	</source>
</shader>
