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
} ssrPass;

#endif
  -->
  </source>
</shader>
