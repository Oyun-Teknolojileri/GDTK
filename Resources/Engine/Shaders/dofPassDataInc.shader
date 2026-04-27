<shader>
  <type name = "includeShader" />
  <include name = "vulkanCompatInc.shader" />
  <source>
  <!--

#ifndef DOF_PASS_DATA
#define DOF_PASS_DATA

TK_UBO_BINDING(5) uniform DofPassData
{
  vec4 pixelSizeAndPad; // .xy = 1/width, 1/height
  vec4 focusAndBlur;    // .x = focusPoint, .y = focusScale, .z = blurSize, .w = radiusScale
} dof;

#endif
  -->
  </source>
</shader>
