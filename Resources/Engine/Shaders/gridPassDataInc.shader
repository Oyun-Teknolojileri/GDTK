<shader>
  <type name = "includeShader" />
  <include name = "vulkanCompatInc.shader" />
  <uniform slot = "7" name = "GridPassData" />
  <source>
  <!--

#ifndef GRID_PASS_DATA
#define GRID_PASS_DATA

TK_UBO_BINDING(7) uniform GridPassData
{
  vec4  cellAndLine;
  vec4  horizontalAxisColor;
  vec4  verticalAxisColor;
  ivec4 is2DAndPad;
} gridData;

#endif
  -->
  </source>
</shader>
