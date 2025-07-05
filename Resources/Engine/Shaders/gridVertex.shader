<shader>
	<type name = "vertexShader" />
	<include name = "cameraDataInc.shader" />
	<include name = "drawDataInc.shader" />
	<uniform name = "model" />
	<uniform name = "inverseTransposeModel" />
	<source>
	<!--
		#version 300 es
		precision highp float;
		precision mediump int;

		// Fixed Attributes.
		layout (location = 0) in vec3 vPosition;

		struct _GridData
		{
			float cellSize;
			float lineMaxPixelCount;
			vec3 horizontalAxisColor;
			vec3 verticalAxisColor;
			uint is2DViewport;
			float cullDistance;
		};

		uniform _GridData GridData;
		uniform mat4 model;
		uniform mat4 inverseTransposeModel;

		out vec2 o_gridPos;
		out vec2 o_cameraGridPos;
		out vec3 o_viewDir;
		out vec3 v_pos;
		
		void main()
		{
			o_gridPos = vec2(vPosition.x, vPosition.y);

			vec3 cameraGridPos = (inverseTransposeModel * vec4(camera.position, 1.0)).xyz;
			o_cameraGridPos = cameraGridPos.xz;

			vec4 v = vec4(o_gridPos.x, 0, o_gridPos.y, 1);
			v.y -= cameraGridPos.y;
			v = model * v;
			o_viewDir = -v.xyz;
			
		  v_pos = (model * vec4(vPosition, 1.0)).xyz;
			gl_Position = camera.projectionView * model * vec4(vPosition, 1.0);
		}
	-->
	</source>
</shader>