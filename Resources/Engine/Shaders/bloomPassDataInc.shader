<shader>
  <type name = "includeShader" />
  <include name = "vulkanCompatInc.shader" />
  <uniform slot = "7" name = "BloomPassData" />
  <source>
  <!--

#ifndef BLOOM_PASS_DATA
#define BLOOM_PASS_DATA

// Pass-specific UBO consumed by bloomDownsample.shader and bloomUpsample.shader.
// Mirrors `BloomPassDataLayout` in Renderer.h byte-for-byte (std140).
//
// Fields:
//   downsampleParams.xy : srcResolution (px, downsample only)
//   downsampleParams.z  : threshold (downsample only, prefilter pass)
//   upsampleParams.x    : filterRadius (upsample only)
//   upsampleParams.y    : intensity    (upsample only)
//   passIndxAndPad.x    : passIndx (downsample switch: 0=filter, 1=karis, >=2=plain)
TK_UBO_BINDING(7) uniform BloomPassData
{
  vec4  downsampleParams;
  vec4  upsampleParams;
  ivec4 passIndxAndPad;
} bloom;

#endif // BLOOM_PASS_DATA
  -->
  </source>
</shader>
