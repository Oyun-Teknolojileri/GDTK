<shader>
  <type name = "includeShader" />
  <include name = "vulkanCompatInc.shader" />
  <uniform slot = "7" name = "PreFilterEnvMapPassData" />
  <source>
  <!--

#ifndef PRE_FILTER_ENV_MAP_PASS_DATA
#define PRE_FILTER_ENV_MAP_PASS_DATA

// Pass-specific UBO consumed by preFilterEnvMapFrag.shader.
// Mirrors `PreFilterEnvMapPassDataLayout` in Renderer.h byte-for-byte (std140).
//
// Fields:
//   params.x : resPerFace, the face size of the mip being written
//   params.y : roughness of the mip being written
TK_UBO_BINDING(7) uniform PreFilterEnvMapPassData
{
  vec4 params;
} preFilterEnvMap;

#endif
  -->
  </source>
</shader>
