<shader>
	<type name = "fragmentShader" />
	<include name = "drawDataInc.shader" />
	<define name = "DrawAlphaMasked" val="0,1" />
	<source>
	<!--
	#version 300 es
	precision highp float;
	precision lowp int;

	in vec3 v_viewDepth;
	in vec3 v_normal;
	in vec2 v_texture;
	in mat3 TBN;

	layout (location = 0) out vec3 fragViewDepth;
	layout (location = 1) out vec3 fragNormal;

	uniform sampler2D s_texture0; // color
	uniform sampler2D s_texture9; // normal

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

		fragViewDepth = v_viewDepth;

		if (material.normalMapInUse == 1)
		{
			fragNormal = texture(s_texture9, v_texture).xyz;
			fragNormal = fragNormal * 2.0 - 1.0;
			fragNormal = TBN * fragNormal;
			fragNormal = normalize(fragNormal);
		}
		else
		{
			fragNormal = normalize(v_normal);
		}
	}
	-->
	</source>
</shader>