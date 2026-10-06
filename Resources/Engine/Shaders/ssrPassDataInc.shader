<shader>
  <type name = "includeShader" />
  <include name = "vulkanCompatInc.shader" />
  <uniform slot = "7" name = "SsrPassData" />
  <source>
  <!--

#ifndef SSR_PASS_DATA
#define SSR_PASS_DATA

// Screen space reflection pass UBO. Mirrors `SsrPassDataLayout` in SsrPass.h byte for byte.
TK_UBO_BINDING(7) uniform SsrPassData
{
  mat4 view;              // World -> view. The g buffer stores world space normals.
  mat4 inverseProjection; // Clip -> view, to rebuild the view position from linear depth.
  vec4 projParams;        // (P00, P11, P20, P21): view position -> UV without a matrix multiply.
  vec4 params;            // x: intensity, y: maxDistance, z: thickness, w: roughness cutoff
  vec4 screenParams;      // xy: 1 / size (texel size), zw: size in pixels
  vec4 flags;             // x: debug view, y: march step count, z: debug view mode
  vec4 hizParams;         // x: depth pyramid level count, y: pixels a level 1 tile covers
  mat4 prevReprojection;  // View space -> the previous frame's clip, for reusing last frame's result
  vec4 temporal;          // x: how much of the accumulated history to keep, y: history is usable
} ssrPass;

#endif
  -->
  </source>
</shader>
