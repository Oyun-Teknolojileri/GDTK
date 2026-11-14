<shader>
  <type name = "includeShader" />
  <include name = "vulkanCompatInc.shader" />
  <uniform slot = "7" name = "BloomPassData" />
  <source>
  <!--

#ifndef BLOOM_PASS_DATA
#define BLOOM_PASS_DATA

// Pass-specific UBO consumed by bloomGlow.shader and bloomComposite.shader.
// Mirrors `BloomPassDataLayout` in BloomPass.h byte-for-byte (std140).
//
// Fields:
//   params.x     : threshold (glow only)
//   params.y     : HDR luminance ceiling (glow only)
//   composite.x  : intensity of the glow added to the frame (composite only)
//   composite.y  : 1 / level count, so the sum is an average rather than a pile (composite only)
//   composite.zw : 1 / glow size in pixels, the texel size of level zero (composite only)
//   weightsA.xyzw: weight of mip level 0, 1, 2, 3 (composite only)
//   weightsB.xy  : weight of mip level 4, 5 (composite only)
TK_UBO_BINDING(7) uniform BloomPassData
{
  vec4 params;
  vec4 composite;
  vec4 weightsA;
  vec4 weightsB;
} bloom;

#endif // BLOOM_PASS_DATA
  -->
  </source>
</shader>
