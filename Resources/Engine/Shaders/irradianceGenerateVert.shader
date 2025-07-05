<shader>
	<type name = "vertexShader" />
	<include name = "drawDataInc.shader" />
	<include name = "cameraDataInc.shader" />
	<uniform name = "model" />
	<source>
	<!--
		#version 300 es
		precision highp float;

		layout (location = 0) in vec3 vPosition;
		layout (location = 1) in vec3 vNormal;
		layout (location = 2) in vec2 vTexture;
		uniform mat4 model;

		out vec3 v_pos;

		void main()
		{
		    v_pos = vPosition;  
		    gl_Position =  camera.projectionView * model * vec4(v_pos, 1.0);
		}
	-->
	</source>
</shader>