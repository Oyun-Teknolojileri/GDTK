<shader>
	<type name = "vertexShader" />
	<include name = "cameraDataInc.shader" />
	<include name = "drawDataInc.shader" />
	<uniform name = "model" />
	<source>
	<!--
		#version 300 es
		precision highp float;

		// Fixed Attributes.
		layout (location = 0) in vec3 vPosition;
		layout (location = 1) in vec3 vNormal;
		layout (location = 2) in vec2 vTexture;

		uniform mat4 model;

		out vec3 v_pos;

		void main()
		{
		  v_pos = vPosition;
		  gl_Position = camera.projectionView * model * vec4(vPosition, 1.0);
		}
	-->
	</source>
</shader>