<shader>
  <type name = "includeShader" />
  <include name = "vulkanCompatInc.shader" />
  <uniform slot = "7" name = "CubemapEquirectPassData" />
  <source>
  <!--

#ifndef CUBEMAP_EQUIRECT_PASS_DATA
#define CUBEMAP_EQUIRECT_PASS_DATA

TK_UBO_BINDING(7) uniform CubemapEquirectPassData
{
  vec4  exposureAndPad;
  ivec4 lodLevelAndPad;
} cubemapEquirect;

#endif
  -->
  </source>
</shader>
