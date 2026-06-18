<shader>
	<type name = "fragmentShader" />
	<include name = "vulkanCompatInc.shader" />
	<define name = "ALPHA_TO_ONE" val="0" />
	<texture slot = "0" name = "s_diffuseColor" />
	<source>
	<!--

		precision highp float;

		TK_LOC(0) in vec3 v_pos;
		TK_LOC(2) in vec2 v_texture;

		TK_SAMPLER_BINDING(0) uniform sampler2D s_diffuseColor;

		out vec4 fragColor;

		void main()
		{
			vec4 src = texture(s_diffuseColor, v_texture).rgba;
		#if ALPHA_TO_ONE
			src.a = 1.0;
		#endif
			fragColor = src;
		}
	-->
	</source>
</shader>