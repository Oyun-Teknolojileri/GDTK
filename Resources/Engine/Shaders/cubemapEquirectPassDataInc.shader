<shader>
  <type name = "includeShader" />
  <source>
  <!--

#ifndef CUBEMAP_EQUIRECT_PASS_DATA
#define CUBEMAP_EQUIRECT_PASS_DATA

layout(std140) uniform CubemapEquirectPassData
{
  vec4  exposureAndPad;
  ivec4 lodLevelAndPad;
} cubemapEquirect;

#endif
  -->
  </source>
</shader>
