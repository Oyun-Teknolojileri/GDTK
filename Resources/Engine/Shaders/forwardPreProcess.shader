<shader>
	<type name = "fragmentShader" />
	<include name = "vulkanCompatInc.shader" />
	<include name = "materialCacheInc.shader" />
	<include name = "drawDataInc.shader" />
	<include name = "normalEncodingInc.shader" />
	<define name = "DrawAlphaMasked" val="0,1" />
	<source>
	<!--
	#version 300 es
	precision highp float;
	precision lowp int;

	in float v_linearDepth;
	in vec3 v_normal;
	in vec2 v_texture;
	in mat3 TBN;

	layout (location = 0) out vec4 fragNormalDepth;

	TK_SAMPLER_BINDING(0) uniform sampler2D s_texture0; // color
	TK_SAMPLER_BINDING(9) uniform sampler2D s_texture9; // normal

	void main()
	{
		Material material = GetMaterial();
	
		vec4 color;
		if (material.diffuseTextureInUse > 0)
		{
			color = texture(s_texture0, v_texture).rgba;
		}
		else
		{
			color = vec4(material.color, material.alpha);
		}

	#if DrawAlphaMasked
		if (color.a <= material.alphaMaskThreshold)
		{
			discard;
		}
	#endif

		vec3 normal;
		if (material.normalMapInUse == 1)
		{
			normal = texture(s_texture9, v_texture).xyz;
			normal = normal * 2.0 - 1.0;
			normal = TBN * normal;
			normal = normalize(normal);
		}
		else
		{
			normal = normalize(v_normal);
		}

		vec2 encodedNormal = encodeNormal(normal);
		fragNormalDepth = vec4(encodedNormal, v_linearDepth, 1.0);
	}
	-->
	</source>
</shader>