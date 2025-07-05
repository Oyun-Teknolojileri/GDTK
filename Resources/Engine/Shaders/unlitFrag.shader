<shader>
	<type name = "fragmentShader" />
	<include name = "drawDataInc.shader" />
	<define name = "DrawAlphaMasked" val="0,1" />
	<source>
	<!--
	#version 300 es
	precision highp float;
	precision lowp int;

	in vec2 v_texture;
	out vec4 fragColor;
	uniform sampler2D s_texture0;

	void main()
	{
		Material material = GetMaterial();
	
		vec4 color;
		if (material.diffuseTextureInUse > 0)
		{
			color = texture(s_texture0, v_texture);
		}
		else
		{
			color = vec4(material.color, material.alpha);
		}

		if (material.useAlphaMask > 0)
		{
			if (color.a <= material.alphaMaskThreshold)
			{
				discard;
			}
		}
		
		fragColor = color;
	}
	-->
	</source>
</shader>