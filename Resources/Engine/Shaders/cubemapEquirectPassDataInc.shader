<shader>
  <type name = "includeShader" />
  <include name = "vulkanCompatInc.shader" />
  <uniform slot = "5" name = "CubemapEquirectPassData" />
  <source>
  <!--

#ifndef CUBEMAP_EQUIRECT_PASS_DATA
#define CUBEMAP_EQUIRECT_PASS_DATA

TK_UBO_BINDING(5) uniform CubemapEquirectPassData
{
  vec4  exposureAndPad;
  ivec4 lodLevelAndPad;
} cubemapEquirect;

#endif
  -->
  </source>
</shader>
