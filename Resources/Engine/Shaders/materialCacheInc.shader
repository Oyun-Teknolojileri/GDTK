<shader>
	<type name = "includeShader" />
	<include name = "perDrawDataInc.shader" />
	<include name = "materialTableFSInc.shader" />
	<source>
	<!--
	#ifndef MATERIAL_CACHE
	#define MATERIAL_CACHE

	// Material accessors — backing storage is now `perDraw._materialData` (PerDrawData UBO,
	// slot 2). `MaterialDataLayout` in perDrawDataInc.shader matches MaterialCacheItem::Data
	// in Material.h byte-for-byte.
	//
	// Phase 2b step 3: instanced path (TK_INSTANCED=1) reads from the global material table
	// (s_materialTable, slot 15) via LoadMaterial(idx); legacy path stays on perDraw UBO.
	//////////////////////////////////////////

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

	// Material Utility
	//////////////////////////////////////////

	Material GetMaterial()
	{
	#if TK_INSTANCED
		// Phase 2b step 7: instanced path reads from the global material table.
		return LoadMaterialFS(perDraw._renderObjectIndices.x);
	#else
		Material material;

		vec4 colorAlpha         = perDraw._materialData.colorAlpha;
		vec4 emissiveThreshold  = perDraw._materialData.emissiveThreshold;
		vec4 metallicRoughness  = perDraw._materialData.metallicRoughness;
		vec4 textureFlags       = perDraw._materialData.textureFlags;

		material.color = colorAlpha.rgb;
		material.alpha = colorAlpha.a;

		material.emissiveColor = emissiveThreshold.rgb;
		material.alphaMaskThreshold = emissiveThreshold.a;

		material.metallic = metallicRoughness.x;
		material.roughness = metallicRoughness.y;
		material.useAlphaMask = int(metallicRoughness.z);
		material.diffuseTextureInUse = int(metallicRoughness.w);

		material.emissiveTextureInUse = int(textureFlags.x);
		material.normalMapInUse = int(textureFlags.y);
		material.metallicRoughnessTextureInUse = int(textureFlags.z);

		return material;
	#endif
	}

	bool IsNormalMapInUse()
	{
	#if TK_INSTANCED
		// Phase 2b step 7: instanced path reads from the global material table.
		Material m = LoadMaterialFS(perDraw._renderObjectIndices.x);
		return m.normalMapInUse > 0;
	#else
		return perDraw._materialData.textureFlags.y > 0.5;
	#endif
	}

	#endif // MATERIAL_CACHE

	-->
	</source>
</shader>
