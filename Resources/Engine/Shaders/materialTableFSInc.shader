<shader>
	<type name = "includeShader" />
	<include name = "vulkanCompatInc.shader" />
	<texture slot = "22" name = "s_materialTableFS" />
	<source>
	<!--
  #ifndef MATERIAL_TABLE_FS_INC
  #define MATERIAL_TABLE_FS_INC

  // Fragment-shader-safe material table — Phase 2b step 7.
  // Same table as the vertex shader's s_materialTable (slot 15), bound at a different
  // slot (22) to avoid conflict with fragment-stage IBL cubemaps at slots 7/10-12/15-17.
  // The C++ side binds the same texture to both slots.
  //
  // NOTE: The Material struct is duplicated from materialCacheInc.shader to break the
  // circular include (materialCacheInc.shader includes this file for LoadMaterialFS).
  // Keep the two definitions in sync. Guards prevent double-definition.

  #ifndef MATERIAL_STRUCT_DEFINED
  #define MATERIAL_STRUCT_DEFINED
  struct Material
  {
    vec3 color;
    float alpha;
    vec3 emissiveColor;
    float alphaMaskThreshold;
    float metallic;
    float roughness;
    int useAlphaMask;
    int diffuseTextureInUse;
    int emissiveTextureInUse;
    int normalMapInUse;
    int metallicRoughnessTextureInUse;
  };
  #endif // MATERIAL_STRUCT_DEFINED

  TK_SAMPLER_BINDING(22) uniform sampler2D s_materialTableFS;
  #define TK_MATERIAL_TABLE_FS_TEX_WIDTH 1024

  ivec2 matTableFSTexel(int linear)
  {
    return ivec2(linear % TK_MATERIAL_TABLE_FS_TEX_WIDTH, linear / TK_MATERIAL_TABLE_FS_TEX_WIDTH);
  }

  Material LoadMaterialFS(int idx)
  {
    int o = idx * 4;
    vec4 d0 = texelFetch(s_materialTableFS, matTableFSTexel(o),   0);
    vec4 d1 = texelFetch(s_materialTableFS, matTableFSTexel(o+1), 0);
    vec4 d2 = texelFetch(s_materialTableFS, matTableFSTexel(o+2), 0);
    vec4 d3 = texelFetch(s_materialTableFS, matTableFSTexel(o+3), 0);

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

  #endif // MATERIAL_TABLE_FS_INC
	-->
	</source>
</shader>
