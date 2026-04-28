<shader>
	<type name = "fragmentShader" />
	<include name = "vulkanCompatInc.shader" />
	<texture slot = "6" name = "s_texture6" />
	<source>
	<!--
		#version 300 es
		precision lowp float;
		precision lowp sampler2D;

		out vec4 fragColor;
		in vec3 v_pos;

		TK_SAMPLER_BINDING(6) uniform samplerCube s_texture6;

		void main()
		{
			vec3 color = texture(s_texture6, v_pos).rgb;

			fragColor = vec4(color, 1.0);
		}
	-->
	</source>
</shader>