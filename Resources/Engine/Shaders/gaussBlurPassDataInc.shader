<shader>
  <type name = "includeShader" />
  <include name = "vulkanCompatInc.shader" />
  <uniform slot = "7" name = "GaussBlurPassData" />
  <source>
  <!--

#ifndef GAUSS_BLUR_PASS_DATA
#define GAUSS_BLUR_PASS_DATA

// Pass-specific UBO consumed by gausBlurVert.shader and gausBlurFrag.shader.
// Mirrors `GaussBlurPassDataLayout` in Renderer.h byte-for-byte (std140).
//
// Fields:
//   blurScaleAndLayer.xyz : BlurScale (per-tap UV step + axis)
//   blurScaleAndLayer.w   : BlurLayer (used only when TextureArray==1)
//   blurClampMinMax.xy    : BlurClampMin (used only when BlurClampEnabled==1)
//   blurClampMinMax.zw    : BlurClampMax (used only when BlurClampEnabled==1)
TK_UBO_BINDING(7) uniform GaussBlurPassData
{
  vec4 blurScaleAndLayer;
  vec4 blurClampMinMax;
} gauss;

#endif // GAUSS_BLUR_PASS_DATA
  -->
  </source>
</shader>
