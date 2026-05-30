<shader>
	<type name = "vertexShader" />
	<include name = "cameraDataInc.shader" />
	<include name = "drawDataInc.shader" />
	<include name = "perDrawDataInc.shader" />
	<include name="vulkanCompatInc.shader" />
	<source>
	<!--
		
		precision highp float;

		layout (location = 0) in vec3 vPosition;
		layout (location = 1) in vec3 vNormal;
		layout (location = 2) in vec2 vTexture;

		TK_LOC(0) out vec3 v_pos;

		void main()
		{
		    v_pos = vPosition;
		    gl_Position =  camera.projectionView * perDraw._model * vec4(v_pos, 1.0);
		}
	-->
	</source>
</shader>