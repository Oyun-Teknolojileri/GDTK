<shader>
	<type name = "includeShader" />
	<include name = "vulkanCompatInc.shader" />
	<texture slot = "19" name = "s_pointLightTable" />
	<source>
	<!--
  #ifndef POINT_LIGHT_TABLE_INC
  #define POINT_LIGHT_TABLE_INC

  // Point-light data buffer — Phase 2b step 5.
  // Persistent, id-indexed storage replacing the LRU PointLightCache (slot 4) on the instanced path.
  // Each row = 5 RGBA32F texels: 4-texel CommonData prefix + 1-texel radius.
  // This include is ONLY pulled in by instanceDataInc.shader (vertex shader),
  // so the sampler + LoadPointLight are never seen by the fragment shader
  // (which stays on the legacy LRU-cache path through step 6).

  #define POINT_LIGHT_STRIDE 5

  struct PointLightTableData
  {
    vec4 common0;     // color.rgb, intensity
    vec4 common1;     // position.xyz, castShadow (as float)
    vec4 common2;     // shadowBias, bleedingReduction, pad0, pad1
    vec4 common3;     // shadowAtlasCoord.xy, shadowAtlasResRatio, shadowAtlasLayer (as float)
    vec4 radiusAndPad;// radius, 0, 0, 0
  };

  TK_SAMPLER_BINDING(19) uniform sampler2D s_pointLightTable;
  #define TK_POINT_LIGHT_TABLE_TEX_WIDTH 1024

  ivec2 ptLightTexel(int linear)
  {
    return ivec2(linear % TK_POINT_LIGHT_TABLE_TEX_WIDTH, linear / TK_POINT_LIGHT_TABLE_TEX_WIDTH);
  }

  PointLightTableData LoadPointLight(int idx)
  {
    int o = idx * POINT_LIGHT_STRIDE;
    PointLightTableData l;
    l.common0      = texelFetch(s_pointLightTable, ptLightTexel(o),   0);
    l.common1      = texelFetch(s_pointLightTable, ptLightTexel(o+1), 0);
    l.common2      = texelFetch(s_pointLightTable, ptLightTexel(o+2), 0);
    l.common3      = texelFetch(s_pointLightTable, ptLightTexel(o+3), 0);
    l.radiusAndPad = texelFetch(s_pointLightTable, ptLightTexel(o+4), 0);
    return l;
  }

  #endif // POINT_LIGHT_TABLE_INC
	-->
	</source>
</shader>
