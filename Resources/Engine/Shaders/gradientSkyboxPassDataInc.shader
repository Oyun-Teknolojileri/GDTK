<shader>
  <type name = "includeShader" />
  <include name = "vulkanCompatInc.shader" />
  <source>
  <!--

#ifndef GRADIENT_SKYBOX_PASS_DATA
#define GRADIENT_SKYBOX_PASS_DATA

TK_UBO_BINDING(5) uniform GradientSkyboxPassData
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
