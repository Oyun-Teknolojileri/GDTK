<shader>
	<type name = "includeShader" />
	<include name = "perDrawDataInc.shader" />
	<source>
	<!--
	#ifndef MATERIAL_CACHE
	#define MATERIAL_CACHE

	// Material accessors — backing storage is now `perDraw._materialData` (PerDrawData UBO,
	// slot 6). `MaterialDataLayout` in perDrawDataInc.shader matches MaterialCacheItem::Data
	// in Material.h byte-for-byte.
	//////////////////////////////////////////

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

	// Material Utility
	//////////////////////////////////////////

	Material GetMaterial()
	{
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
	}

	bool IsNormalMapInUse()
	{
		return perDraw._materialData.textureFlags.y > 0.5;
	}

	#endif // MATERIAL_CACHE

	-->
	</source>
</shader>
