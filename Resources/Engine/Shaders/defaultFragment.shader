<shader>	
	<type name = "fragmentShader" />
	<include name = "lighting.shader" />
	<include name = "ibl.shader" />
	<include name = "AO.shader" />
	<include name = "cameraDataInc.shader" />
	<include name = "drawDataInc.shader" />
	<define name = "DrawAlphaMasked" val="0,1" />
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

	uniform int LightingOnly;

	in vec3 v_pos;
	in vec3 v_normal;
	in vec2 v_texture;
	in float v_viewPosDepth;
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

	#if DrawAlphaMasked
		if(color.a <= material.alphaMaskThreshold)
		{
			discard;
		}
	#endif

		if (LightingOnly == 1)
		{
			color.xyz = vec3(1.0);
		}

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
			n = normalize(v_normal);
		}
		vec3 e = normalize(camera.position - v_pos);

		vec2 metallicRoughness;
		if (material.metallicRoughnessTextureInUse > 0)
		{
			metallicRoughness = texture(s_texture4, v_texture).rg;
		}
		else
		{
			metallicRoughness = vec2(material.metallic, material.roughness);
		}

		vec3 irradiance = PBRLighting(v_pos, v_viewPosDepth, n, e, camera.position, color.xyz, metallicRoughness.x, metallicRoughness.y);

		float ambientOcclusion = AmbientOcclusion();
		irradiance += IBLPBR(n, e, color.xyz, metallicRoughness.x, metallicRoughness.y) * ambientOcclusion;

		fragColor = vec4(irradiance, color.a) + vec4(emissive, 0.0f);
	}
	-->
	</source>
</shader>