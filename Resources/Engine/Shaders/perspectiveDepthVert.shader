<shader>
	<type name = "vertexShader" />
	<include name = "skinning.shader" />
	<include name = "cameraDataInc.shader" />
	<uniform name = "model" />
	<source>
	<!--
	#version 300 es
	precision highp float;
	precision lowp int;

	// Fixed Attributes.
	layout (location = 0) in vec3 vPosition;
	layout (location = 2) in vec2 vTexture;

	out vec4 v_pos;
	out vec2 v_texture;

	uniform mat4 model;

	void main()
	{
			v_texture = vTexture;

			vec4 skinnedVPos = vec4(vPosition, 1.0);

			if (isSkinned)
			{
					skin(skinnedVPos, skinnedVPos);
			}

			vec4 worldPos = model * skinnedVPos;

			// Precompute inverse far plane on CPU if possible.
			v_pos       = (camera.view * worldPos) * (1.0 / camera.farPlane);
			gl_Position = camera.projectionView * worldPos;
	}
	-->
	</source>
</shader>