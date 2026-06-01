<shader>
  <type name = "includeShader" />
  <include name = "vulkanCompatInc.shader" />
  <uniform slot = "7" name = "DofPassData" />
  <source>
  <!--

#ifndef DOF_PASS_DATA
#define DOF_PASS_DATA

TK_UBO_BINDING(7) uniform DofPassData
{
  vec4 pixelSizeAndPad; // .xy = 1/width, 1/height
  vec4 focusAndBlur;    // .x = focusPoint, .y = focusScale, .z = blurSize, .w = radiusScale
} dof;

#endif
  -->
  </source>
</shader>
