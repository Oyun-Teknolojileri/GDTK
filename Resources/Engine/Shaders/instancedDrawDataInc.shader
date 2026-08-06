<shader>
	<type name = "includeShader" />
	<include name = "vulkanCompatInc.shader" />
	<source>
	<!--
  #ifndef INSTANCED_DRAW_DATA_INC
  #define INSTANCED_DRAW_DATA_INC

  // InstancedDrawData — Phase 2b step 7 mini-UBO (slot 2, TK_INSTANCED=1 only).
  //
  // Replaces the full 70-texel PerDrawData UBO on the instanced path with a lean
  // 12-Vec4 block carrying only the fields that are NOT in any global table or the
  // instance texture:
  //   - global0/global1 (draw-command globals not in tables)
  //   - viewportSizeAndPad
  //   - renderObjectIndices (indices into material/env-volume/skeleton tables)
  //   - iblRotation / iblSecondaryRotation (mat4 × 2)
  //
  // Legacy path (TK_INSTANCED=0) stays on the full PerDrawData UBO, byte-identical.
  // The two blocks share the same slot-2 binding — GL/Vulkan allow different blocks
  // on the same binding point for different program variants.

  // Field names match PerDrawData's naming convention (underscore prefix) so that
  // existing call sites like perDraw._viewportSizeAndPad keep compiling via the
  // `#define perDraw instancedDrawData` alias without mass renaming.
  struct InstancedDrawData
  {
    vec4  _global0;              // x: iblInUse, y: aoInUse, z: skyIntensity, w: pad
    vec4  _global1;              // x: pointLightCount, y: spotLightCount, z: dirLightCount, w: pad
    vec4  _viewportSizeAndPad;   // xy: viewportSize
    ivec4 _renderObjectIndices;  // x: materialIndex, y: envIndex, z: secEnvIndex, w: skeletonIndex
    mat4  _iblRotation;          // sky IBL rotation (or identity for local volumes)
    mat4  _iblSecondaryRotation; // secondary IBL rotation (currently identity-only)
    ivec4 _activePointLightIndices[6]; // 24 ints packed
    ivec4 _activeSpotLightIndices[6];  // 24 ints packed
    ivec4 _lightCounts;          // x: pointCount, y: spotCount
  };

  TK_UBO_BINDING(2) uniform InstancedDrawDataBlock
  {
    InstancedDrawData instancedDrawData;
  };

  #endif // INSTANCED_DRAW_DATA_INC
	-->
	</source>
</shader>
