<shader>
  <type name = "includeShader" />
  <include name = "vulkanCompatInc.shader" />
  <uniform slot = "7" name = "SsrPassData" />
  <source>
  <!--

#ifndef SSR_PASS_DATA
#define SSR_PASS_DATA

// Screen space reflection pass UBO. Mirrors `SsrPassDataLayout` in SsrPass.h byte for byte.
TK_UBO_BINDING(7) uniform SsrPassData
{
  mat4 view;               // World -> view. The g buffer stores world space normals.
  mat4 inverseProjection;  // Clip -> view, to rebuild the view position from linear depth.
  mat4 inverseView;        // View -> world, for the reprojection of the previous frame.
  mat4 prevViewProjection; // Previous frame's view projection, to find where this surface was.
  mat4 envRotation;        // World -> environment space (the sky cubemap's orientation, inverted).
  vec4 projParams;         // (P00, P11, P20, P21): view position -> UV without a matrix multiply.
  vec4 params;             // x: intensity, y: maxDistance, z: thickness, w: roughness cutoff
  vec4 screenParams;       // xy: 1 / size (texel size), zw: size in pixels
  vec4 flags;              // x: debug view, y: march step count, z: debug view mode, w: frame index
  vec4 fades;              // x: border fade, y: screen length fade start, z: hit distance fade start
  vec4 temporal;           // x/y: unused, z: brdf bias, w: reset the history
  vec4 velocity;           // x: 1 when the object motion targets are bound, y: screen height
  vec4 environment;        // x: intensity, y: highest mip the cubemap carries, z: 1 when bound
  vec4 denoiser;           // x: max samples, y: disocclusion threshold, z: history clip weight, w: initial variance
  vec4 quality;            // x: spatial filter taps, y: resolution scale the target is traced at
} ssrPass;

#endif
  -->
  </source>
</shader>
