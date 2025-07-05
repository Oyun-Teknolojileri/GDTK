<shader>
	<type name = "fragmentShader" />
	<include name = "drawDataInc.shader" />
	<source>
	<!--
	#version 300 es
	precision highp float;
	precision lowp int;

	in vec3 v_pos;
	in vec2 v_texture;

	uniform sampler2D s_texture0;
	uniform vec3 lightPos;
	uniform float far;

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

		float depth = length(lightPos - v_pos) / far;
		fragColor = vec4(depth, depth, depth, 1.0);
	}
	-->
	</source>
</shader>