<shader>
	<type name = "fragmentShader" />
	<include name = "drawDataInc.shader" />
	<source>
	<!--
	#version 300 es
	precision highp float;
	precision lowp int;

	in float v_depth;
	in vec2 v_texture;

	uniform sampler2D s_texture0;

	out vec4 fragColor;

	void main()
	{
		Material material = GetMaterial();
	
		float alpha = 1.0;
		if (material.diffuseTextureInUse == 1)
		{
			alpha = texture(s_texture0, v_texture).a;
		}
		else
		{
			alpha = material.alpha;
		}

		if (alpha < 0.1)
		{
			discard;
		}

		fragColor = vec4(v_depth, v_depth, v_depth, 1.0);
	}
	-->
	</source>
</shader>