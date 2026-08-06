<shader>
	<type name = "includeShader" />
	<include name = "vulkanCompatInc.shader" />
	<texture slot = "20" name = "s_spotLightTable" />
	<source>
	<!--
  #ifndef SPOT_LIGHT_TABLE_INC
  #define SPOT_LIGHT_TABLE_INC

  // Spot-light data buffer — Phase 2b step 5.
  // Persistent, id-indexed storage replacing the LRU SpotLightCache (slot 5) on the instanced path.
  // Each row = 10 RGBA32F texels: 4-texel CommonData prefix + dirAndRadius(1) + anglesAndPad(1) + pvm(4).
  // This include is ONLY pulled in by instanceDataInc.shader (vertex shader),
  // so the sampler + LoadSpotLight are never seen by the fragment shader
  // (which stays on the legacy LRU-cache path through step 6).

  #define SPOT_LIGHT_STRIDE 10

  struct SpotLightTableData
  {
    vec4 common0;       // color.rgb, intensity
    vec4 common1;       // position.xyz, castShadow (as float)
    vec4 common2;       // shadowBias, bleedingReduction, pad0, pad1
    vec4 common3;       // shadowAtlasCoord.xy, shadowAtlasResRatio, shadowAtlasLayer (as float)
    vec4 dirAndRadius;  // direction.xyz, radius
    vec4 anglesAndPad;  // outerAngle, innerAngle, pad, pad
    mat4 pvm;           // projectionViewMatrix (4 texels)
  };

  TK_SAMPLER_BINDING(20) uniform sampler2D s_spotLightTable;
  #define TK_SPOT_LIGHT_TABLE_TEX_WIDTH 1024

  ivec2 spLightTexel(int linear)
  {
    return ivec2(linear % TK_SPOT_LIGHT_TABLE_TEX_WIDTH, linear / TK_SPOT_LIGHT_TABLE_TEX_WIDTH);
  }

  SpotLightTableData LoadSpotLight(int idx)
  {
    int o = idx * SPOT_LIGHT_STRIDE;
    SpotLightTableData l;
    l.common0      = texelFetch(s_spotLightTable, spLightTexel(o),   0);
    l.common1      = texelFetch(s_spotLightTable, spLightTexel(o+1), 0);
    l.common2      = texelFetch(s_spotLightTable, spLightTexel(o+2), 0);
    l.common3      = texelFetch(s_spotLightTable, spLightTexel(o+3), 0);
    l.dirAndRadius = texelFetch(s_spotLightTable, spLightTexel(o+4), 0);
    l.anglesAndPad = texelFetch(s_spotLightTable, spLightTexel(o+5), 0);
    l.pvm          = mat4(texelFetch(s_spotLightTable, spLightTexel(o+6), 0),
                          texelFetch(s_spotLightTable, spLightTexel(o+7), 0),
                          texelFetch(s_spotLightTable, spLightTexel(o+8), 0),
                          texelFetch(s_spotLightTable, spLightTexel(o+9), 0));
    return l;
  }

  #endif // SPOT_LIGHT_TABLE_INC
	-->
	</source>
</shader>
