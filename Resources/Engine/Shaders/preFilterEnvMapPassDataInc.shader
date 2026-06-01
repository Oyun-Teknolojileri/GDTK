<shader>
  <type name = "includeShader" />
  <include name = "vulkanCompatInc.shader" />
  <uniform slot = "7" name = "PreFilterEnvMapPassData" />
  <source>
  <!--

#ifndef PRE_FILTER_ENV_MAP_PASS_DATA
#define PRE_FILTER_ENV_MAP_PASS_DATA

TK_UBO_BINDING(7) uniform PreFilterEnvMapPassData
{
  vec4 params;
} preFilterEnvMap;

#endif
  -->
  </source>
</shader>
