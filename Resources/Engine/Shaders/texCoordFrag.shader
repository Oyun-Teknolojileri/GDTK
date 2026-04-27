<shader>
	<type name = "fragmentShader" />
	<include name="vulkanCompatInc.shader" />
	<source>
	<!--
		#version 300 es
		precision mediump float;
TK_LOC(2) in vec2 v_texture;		
		out vec4 v_fragColor;
		
		void main()
		{
			v_fragColor = vec4(v_texture, 0.0, 1.0);
		}
	-->
	</source>
</shader>