<shader>
  <type name = "includeShader" />
  <source>
  <!--

#ifndef GRADIENT_SKYBOX_PASS_DATA
#define GRADIENT_SKYBOX_PASS_DATA

layout(std140) uniform GradientSkyboxPassData
{
  vec4 topColor;        // .xyz
  vec4 middleColor;     // .xyz
  vec4 bottomColor;     // .xyz
  vec4 exponentAndPad;  // .x = exponent
} gradientSky;

#endif
  -->
  </source>
</shader>
