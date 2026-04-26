<shader>
  <type name = "includeShader" />
  <source>
  <!--

#ifndef SSAO_BLUR_PASS_DATA
#define SSAO_BLUR_PASS_DATA

layout(std140) uniform SsaoBlurPassData
{
  vec4 texelSizeAndPad; // .xy = 1.0 / textureSize
} ssaoBlur;

#endif
  -->
  </source>
</shader>
