<shader>
	<type name = "vertexShader" />
	<define name = "TextureArray" val = "0,1" />
	<include name = "gaussBlurPassDataInc.shader" />
	<source>
	<!--
		#version 300 es
		precision highp float;

		// Fixed Attributes.
		layout (location = 0) in vec3 vPosition;
		layout (location = 1) in vec3 vNormal;
		layout (location = 2) in vec2 vTexture;

		vec3 v_pos;
		out vec3 v_texture;
		
		void main()
		{
		  v_pos.xy = vPosition.xy * 2.0;
		  v_pos.z = -1.0;

		#if TextureArray == 1
		  v_texture = vec3(vTexture, gauss.blurScaleAndLayer.w);
		#else
		  v_texture = vec3(vTexture, 0.0);
		#endif

		  v_texture.y = v_texture.y;
		  gl_Position = vec4(v_pos, 1.0);
		}
	-->
	</source>
</shader>