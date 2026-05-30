<shader>
  <type name = "includeShader" />
  <include name = "vulkanCompatInc.shader" />
  <uniform slot = "5" name = "SsaoBlurPassData" />
  <source>
  <!--

#ifndef SSAO_BLUR_PASS_DATA
#define SSAO_BLUR_PASS_DATA

TK_UBO_BINDING(5) uniform SsaoBlurPassData
{
  vec4 texelSizeAndPad; // .xy = 1.0 / textureSize
} ssaoBlur;

#endif
  -->
  </source>
</shader>
