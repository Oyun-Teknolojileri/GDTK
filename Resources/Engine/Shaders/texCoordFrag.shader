<shader>
	<type name = "fragmentShader" />
	<source>
	<!--
		#version 300 es
		precision mediump float;
		
		in vec2 v_texture;		
		out vec4 v_fragColor;
		
		void main()
		{
			v_fragColor = vec4(v_texture, 0.0, 1.0);
		}
	-->
	</source>
</shader>