<shader>
  <type name = "includeShader" />
  <source>
  <!--

#ifndef GRID_PASS_DATA
#define GRID_PASS_DATA

layout(std140) uniform GridPassData
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
