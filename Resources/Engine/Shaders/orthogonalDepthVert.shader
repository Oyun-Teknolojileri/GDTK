<shader>
	<type name = "vertexShader" />
	<include name = "skinning.shader" />
	<include name = "cameraDataInc.shader" />
	<include name = "drawDataInc.shader" />
	<uniform name = "model" />
	<source>
	<!--
		#version 300 es
		precision highp float;
		precision lowp int;

		// Fixed Attributes.
		layout (location = 0) in vec3 vPosition;
		layout (location = 1) in vec3 vNormal;
		layout (location = 2) in vec2 vTexture;
		layout (location = 3) in vec3 vBiTan;

		uniform mat4 model;
		out vec2 v_texture;
		out float z;

		void main()
		{
			v_texture = vTexture;
			vec4 skinnedVPos = vec4(vPosition, 1.0);
			
			if(isSkinned > 0u)
			{
				skin(skinnedVPos, skinnedVPos);
			}

			gl_Position = camera.projectionView * model * skinnedVPos;
			z = gl_Position.z / gl_Position.w;
			z = (gl_DepthRange.diff * z + gl_DepthRange.near + gl_DepthRange.far) * 0.5;

			gl_Position.z = 0.0; // Pancake the objects fall behind the view frustum.
		}
	-->
	</source>
</shader>