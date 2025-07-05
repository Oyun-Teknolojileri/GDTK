<shader>
	<type name = "includeShader" />
	<uniform name = "materialCache" size = "4" />
	<source>
	<!--
	
	#ifndef MATERIAL_CACHE
	#define MATERIAL_CACHE

	// Material Cache
	//////////////////////////////////////////

	uniform vec4 materialCache[4];

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

		vec4 colorAlpha         = materialCache[0];
		vec4 emissiveThreshold  = materialCache[1];
		vec4 metallicRoughness  = materialCache[2];
		vec4 textureFlags       = materialCache[3];

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

	#endif // MATERIAL_CACHE

	-->
	</source>
</shader>