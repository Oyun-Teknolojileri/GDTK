<shader>
  <type name = "includeShader" />
  <include name = "vulkanCompatInc.shader" />
  <uniform slot = "7" name = "SsaoCalcPassData" />
  <source>
  <!--

#ifndef SSAO_CALC_PASS_DATA
#define SSAO_CALC_PASS_DATA

// Sized for max kernel (32). Calling shader's loop iterates 0..KERNEL_SIZE-1 so unused tail
// entries are harmless. Mat4 normalToView decoded via mat3() in the shader (std140 mat3 padding
// would otherwise cost the same 48 bytes with worse ergonomics).
TK_UBO_BINDING(7) uniform SsaoCalcPassData
{
  mat4 normalToView;
  vec4 samples[32];          // .xyz = hemisphere sample dir, .w = unused
  vec4 projParams;           // (P00, P11, P20, P21)
  mat4 inverseProjection;
  vec4 radiusBiasAndPad;     // .x = radius, .y = bias
} ssaoCalc;

#endif
  -->
  </source>
</shader>
