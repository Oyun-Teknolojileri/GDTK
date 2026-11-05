<shader>
  <type name = "includeShader" />
  <include name = "vulkanCompatInc.shader" />
  <uniform slot = "7" name = "TaaPassData" />
  <source>
  <!--

#ifndef TAA_PASS_DATA
#define TAA_PASS_DATA

// Temporal anti-aliasing pass UBO. Mirrors `TaaPassDataLayout` in TaaPass.h byte for byte.
TK_UBO_BINDING(7) uniform TaaPassData
{
  mat4 reprojection;      // previous clip space * inverse of this frame's: current pixel -> last frame's pixel
  mat4 skyReprojection;   // the same for the far plane, built from the translation free views
  vec4 screenParams;      // xy: 1 / size (texel size), zw: size in pixels
  vec4 depthParams;       // x: window depth -> NDC z scale, y: bias (2, -1 on GL; 1, 0 on a zero to one range)
  vec4 params;            // x: weight of the current frame, y: drop the history, z: depth tolerance in NDC
} taa;

#endif
  -->
  </source>
</shader>
