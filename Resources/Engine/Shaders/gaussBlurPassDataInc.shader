<shader>
  <type name = "includeShader" />
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
layout(std140) uniform GaussBlurPassData
{
  vec4 blurScaleAndLayer;
  vec4 blurClampMinMax;
} gauss;

#endif // GAUSS_BLUR_PASS_DATA
  -->
  </source>
</shader>
