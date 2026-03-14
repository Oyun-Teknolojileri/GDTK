<shader>
	<type name = "fragmentShader" />
	<include name = "lighting.shader" />
	<include name = "ibl.shader" />
	<include name = "AO.shader" />
	<include name = "cameraDataInc.shader" />
	<include name = "materialCacheInc.shader" />
	<define name = "DrawAlphaMasked" val="0,1" />
	<define name = "SMFormat16Bit" val="0,1" />
	<define name = "ShadowPCF" val="0,4,9,16" />
	<define name = "highlightCascades" val="0,1" />
	<define name = "LightingOnly" val="0,1" />
	<source>
	<!--
	#version 300 es
	precision highp float;
	precision lowp int;
	precision mediump sampler2D;
	precision mediump samplerCube;
	precision highp sampler2DArray;

	uniform sampler2D s_texture0; // color
	uniform sampler2D s_texture1; // emissive
	uniform sampler2D s_texture4; // metallic-roughness
	uniform sampler2D s_texture9; // normal

	in vec3 v_worldPos;
	in vec3 v_worldNormal;
	in vec2 v_texture;
	in float v_viewDepth;
	in mat3 TBN;

	layout (location = 0) out vec4 fragColor;

	void main()
	{
		Material material = GetMaterial();

		vec4 color;
		if(material.diffuseTextureInUse > 0)
		{
			color = texture(s_texture0, v_texture);
		}
		else
		{
			color = vec4(material.color, material.alpha);
		}
	
		vec3 emissive;
		if(material.emissiveTextureInUse > 0)
		{
			emissive = texture(s_texture1, v_texture).xyz;
		}
		else
		{
			emissive = material.emissiveColor;
		}
	
		vec2 metallicRoughness;
		if (material.metallicRoughnessTextureInUse > 0)
		{
			metallicRoughness = texture(s_texture4, v_texture).rg;
		}
		else
		{
			metallicRoughness = vec2(material.metallic, material.roughness);
		}
		metallicRoughness.r = clamp(metallicRoughness.r, 0.0, 1.0);
		metallicRoughness.g = clamp(metallicRoughness.g, 0.045, 1.0);
	
	#if DrawAlphaMasked
		if(color.a <= material.alphaMaskThreshold)
		{
			discard;
		}
	#endif

	#if LightingOnly
		color.xyz = vec3(1.0);
	#endif

		vec3 n;
		if (material.normalMapInUse > 0)
		{
			n = texture(s_texture9, v_texture).xyz;
			n = n * 2.0 - 1.0;
			n = TBN * n;
			n = normalize(n);
		}
		else
		{
			n = v_worldNormal;
		}
	
		vec3 e = normalize(camera.position - v_worldPos);

		// Compute energy compensation for multiscattering
		vec3 f0 = BaseReflectivityPBR(vec3(0.04), color.xyz, metallicRoughness.x);
		float NdotV = max(dot(n, e), 0.0);
		vec2 dfg = texture(s_texture16, vec2(NdotV, metallicRoughness.y)).rg;
		vec3 energyComp = EnergyCompensation(dfg, f0);

		vec3 irradiance = PBRLighting(v_worldPos, v_viewDepth, n, e, camera.position, color.xyz, metallicRoughness.x, metallicRoughness.y, energyComp);

		float ambientOcclusion = AmbientOcclusion();
		irradiance += IBLPBR(n, e, color.xyz, metallicRoughness.x, metallicRoughness.y, dfg, energyComp) * ambientOcclusion;

		fragColor = vec4(irradiance, color.a) + vec4(emissive, 0.0);
	}
	-->
	</source>
</shader>