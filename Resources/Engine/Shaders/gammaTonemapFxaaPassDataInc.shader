<shader>
  <type name = "includeShader" />
  <include name = "vulkanCompatInc.shader" />
  <uniform slot = "5" name = "GammaTonemapFxaaPassData" />
  <source>
  <!--

#ifndef GAMMA_TONEMAP_FXAA_PASS_DATA
#define GAMMA_TONEMAP_FXAA_PASS_DATA

// Pass-specific UBO consumed by gammaTonemapFxaa.shader and its gamma/tonemap/fxaa includes.
// Mirrors `GammaTonemapFxaaPassDataLayout` in Renderer.h byte-for-byte (std140).
//
// Fields:
//   enableFlags.x : enableFxaa             (0/1)
//   enableFlags.y : enableTonemapping      (0/1)
//   enableFlags.z : enableGammaCorrection  (0/1)
//   screenSizeAndPad.xy : screenSize (px)
//   tonemapParams.x : useAcesTonemapper (0 = Reinhard, 1 = ACES)
//   tonemapParams.y : gamma value
TK_UBO_BINDING(5) uniform GammaTonemapFxaaPassData
{
  ivec4 enableFlags;
  vec4  screenSizeAndPad;
  vec4  tonemapParams;
} gtf;

#endif // GAMMA_TONEMAP_FXAA_PASS_DATA
  -->
  </source>
</shader>
