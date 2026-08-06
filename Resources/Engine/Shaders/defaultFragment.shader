<shader>
	<type name = "fragmentShader" />
	<include name = "vulkanCompatInc.shader" />
	<include name = "lighting.shader" />
	<include name = "ibl.shader" />
	<include name = "AO.shader" />
	<include name = "cameraDataInc.shader" />
	<include name = "materialCacheInc.shader" />
	<define name = "TK_INSTANCED" val="0,1" />
	<define name = "DrawAlphaMasked" val="0,1" />
	<define name = "ShadowPCF" val="0,4,9,16" />
	<define name = "highlightCascades" val="0,1" />
	<define name = "ShadingMode" val="0,1,2,3,4,5" />
	<texture slot = "0" name = "s_diffuseColor" />
	<texture slot = "1" name = "s_emissiveColor" />
	<texture slot = "4" name = "s_metallicRoughness" />
	<texture slot = "9" name = "s_normalMap" />
	<source>
	<!--
	
	precision highp float;
	precision lowp int;
	precision mediump sampler2D;
	precision mediump samplerCube;
	precision highp sampler2DArray;

	TK_SAMPLER_BINDING(0) uniform sampler2D s_diffuseColor; // color
	TK_SAMPLER_BINDING(1) uniform sampler2D s_emissiveColor; // emissive
	TK_SAMPLER_BINDING(4) uniform sampler2D s_metallicRoughness; // metallic-roughness
	TK_SAMPLER_BINDING(9) uniform sampler2D s_normalMap; // normal

	#define SHADE_LIGHTING_ONLY 1
	#define SHADE_ALBEDO_ONLY 2
	#define SHADE_NORMAL_ONLY 3
	#define SHADE_METALLIC_ONLY 4
	#define SHADE_ROUGHNESS_ONLY 5

	TK_LOC(0) in vec3 v_worldPos;
	TK_LOC(1) in vec3 v_worldNormal;
	TK_LOC(2) in vec2 v_texture;
	TK_LOC(3) in float v_viewDepth;
	TK_LOC(4) in mat3 TBN;

	layout (location = 0) out vec4 fragColor;

	void main()
	{
		Material material = GetMaterial();

		vec4 color;
		if(material.diffuseTextureInUse > 0)
		{
			color = texture(s_diffuseColor, v_texture);
		}
		else
		{
			color = vec4(material.color, material.alpha);
		}

		vec3 emissive;
		if(material.emissiveTextureInUse > 0)
		{
			emissive = texture(s_emissiveColor, v_texture).xyz;
		}
		else
		{
			emissive = material.emissiveColor;
		}

		float metallic, roughness;
		if (material.metallicRoughnessTextureInUse > 0)
		{
			// Texture Holds Occulussion Roughness Metallic values in rgb.
			// Occulusion is not supported at the moment. ( No Value )
			vec3 orm = texture(s_metallicRoughness, v_texture).rgb;
			metallic = orm.b;
			roughness = orm.g;
		}
		else
		{
			metallic = material.metallic;
			roughness = material.roughness;
		}
		metallic = clamp(metallic, 0.0, 1.0);
		float perceptualRoughness = clamp(roughness, 0.045, 1.0);

	#if DrawAlphaMasked
		if(color.a <= material.alphaMaskThreshold)
		{
			discard;
		}
	#endif

	#if ShadingMode == SHADE_ALBEDO_ONLY
		fragColor = color;
		return;
	#endif

	#if ShadingMode == SHADE_METALLIC_ONLY
		fragColor = vec4(metallic, metallic, metallic, 1.0);
		return;
	#endif

	#if ShadingMode == SHADE_ROUGHNESS_ONLY
		fragColor = vec4(perceptualRoughness, perceptualRoughness, perceptualRoughness, 1.0);
		return;
	#endif

	#if ShadingMode == SHADE_LIGHTING_ONLY
		color.xyz = vec3(1.0);
	#endif

		vec3 n;
		if (material.normalMapInUse > 0)
		{
			n = texture(s_normalMap, v_texture).xyz;
			n = n * 2.0 - 1.0;
			n = TBN * n;
			n = normalize(n);
		}
		else
		{
			n = normalize(v_worldNormal);
		}

	#if ShadingMode == SHADE_NORMAL_ONLY
		fragColor = vec4(n * 0.5 + 0.5, 1.0);
		return;
	#endif

		perceptualRoughness = specularAntiAliasing(perceptualRoughness, n);
		roughness = perceptualRoughnessToRoughness(perceptualRoughness);

		vec3 e = normalize(camera.position - v_worldPos);

		// Compute energy compensation for multiscattering
		vec3 f0 = BaseReflectivityPBR(vec3(0.04), color.xyz, metallic);
		float NdotV = max(dot(n, e), 0.0);
		vec2 dfg = texture(s_brdfLut, vec2(NdotV, perceptualRoughness)).rg;
		vec3 energyComp = EnergyCompensation(dfg, f0);

		vec3 irradiance = PBRLighting(v_worldPos, v_viewDepth, n, e, camera.position, color.xyz, metallic, roughness, energyComp);

		float ambientOcclusion = AmbientOcclusion();
		irradiance += IBLPBR(n, e, color.xyz, metallic, perceptualRoughness, dfg, energyComp, v_worldPos) * ambientOcclusion;

		fragColor = vec4(irradiance, color.a) + vec4(emissive, 0.0);
	}
	-->
	</source>
</shader>