<shader>
  <type name = "includeShader" />
  <include name = "vulkanCompatInc.shader" />
  <uniform slot = "7" name = "SsrPassData" />
  <source>
  <!--

#ifndef SSR_PASS_DATA
#define SSR_PASS_DATA

// Debug views, in the order the engine settings window offers them. They live here because both phases
// read them: the trace phase produces most of them, the resolve phase reads the IBL specular one.
#define SSR_DEBUG_REFLECTION     0
#define SSR_DEBUG_CONFIDENCE     1
#define SSR_DEBUG_MIP            2
#define SSR_DEBUG_DEPTH          3
#define SSR_DEBUG_HIT_UV         4
#define SSR_DEBUG_LENGTH         5
#define SSR_DEBUG_HIT_ERROR      6
#define SSR_DEBUG_IBL_SPECULAR   7

// Screen space reflection pass UBO. Mirrors `SsrPassDataLayout` in SsrPass.h byte for byte.
TK_UBO_BINDING(7) uniform SsrPassData
{
  mat4 view;              // World -> view. The g buffer stores world space normals.
  mat4 inverseProjection; // Clip -> view, to rebuild the view position from linear depth.
  vec4 projParams;        // (P00, P11, P20, P21): view position -> UV without a matrix multiply.
  vec4 params;            // x: intensity, y: maxDistance, z: thickness, w: roughness cutoff
  vec4 screenParams;      // xy: 1 / size (texel size), zw: size in pixels
  vec4 flags;             // x: debug view, y: march step count, z: debug view mode
} ssrPass;

#endif
  -->
  </source>
</shader>
