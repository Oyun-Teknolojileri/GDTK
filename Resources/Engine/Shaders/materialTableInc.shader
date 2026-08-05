<shader>
	<type name = "includeShader" />
	<include name = "vulkanCompatInc.shader" />
	<include name = "materialCacheInc.shader" />
	<texture slot = "15" name = "s_materialTable" />
	<source>
	<!--
  #ifndef MATERIAL_TABLE_INC
  #define MATERIAL_TABLE_INC

  // Material table accessor — Phase 2b step 3.
  // This include is ONLY pulled in by instanceDataInc.shader (vertex shader),
  // so the sampler + LoadMaterial are never seen by the fragment shader
  // (which stays on the legacy perDraw-UBO path through step 6).

  TK_SAMPLER_BINDING(15) uniform sampler2D s_materialTable;
  #define TK_MATERIAL_TABLE_TEX_WIDTH 1024

  ivec2 matTableTexel(int linear)
  {
    return ivec2(linear % TK_MATERIAL_TABLE_TEX_WIDTH, linear / TK_MATERIAL_TABLE_TEX_WIDTH);
  }

  // Fetch one material row (4 RGBA32F texels) from the global table.
  Material LoadMaterial(int idx)
  {
    int o = idx * 4;
    vec4 d0 = texelFetch(s_materialTable, matTableTexel(o),   0);
    vec4 d1 = texelFetch(s_materialTable, matTableTexel(o+1), 0);
    vec4 d2 = texelFetch(s_materialTable, matTableTexel(o+2), 0);
    vec4 d3 = texelFetch(s_materialTable, matTableTexel(o+3), 0);

    Material m;

    m.color = d0.rgb;
    m.alpha = d0.a;

    m.emissiveColor = d1.rgb;
    m.alphaMaskThreshold = d1.a;

    m.metallic = d2.x;
    m.roughness = d2.y;
    m.useAlphaMask = int(d2.z);
    m.diffuseTextureInUse = int(d2.w);

    m.emissiveTextureInUse = int(d3.x);
    m.normalMapInUse = int(d3.y);
    m.metallicRoughnessTextureInUse = int(d3.z);

    return m;
  }

  #endif // MATERIAL_TABLE_INC
	-->
	</source>
</shader>
