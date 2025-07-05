<shader>
	<type name = "vertexShader" />
	<include name = "cameraDataInc.shader" />
	<include name = "drawDataInc.shader" />
	<uniform name = "modelWithoutTranslate" />
	<source>
	<!--
		#version 300 es
		precision lowp float;

		// Fixed Attributes.
		layout (location = 0) in vec3 vPosition;
		layout (location = 1) in vec3 vNormal;
		layout (location = 2) in vec2 vTexture;

		uniform mat4 modelWithoutTranslate;
		out vec3 v_pos;

		void main()
		{
			v_pos = (modelWithoutTranslate * vec4(vPosition, 1.0)).xyz;

			vec4 clipPos = camera.projectionViewNoTranslate * vec4(vPosition, 1.0);

			// Make z equals w. So after perspective division the depth
			// value will always 1(maximum depth value).
			gl_Position = clipPos.xyww;
		}
	-->
	</source>
</shader>