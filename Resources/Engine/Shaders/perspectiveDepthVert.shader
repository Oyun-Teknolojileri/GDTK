<shader>
	<type name = "vertexShader" />
	<include name = "skinning.shader" />
	<include name = "cameraDataInc.shader" />
	<define name = "DrawAlphaMasked" val="0,1" />
	<include name = "perDrawDataInc.shader" />
	<source>
	<!--
	#version 300 es
	precision highp float;
	precision lowp int;

	// Fixed Attributes.
	layout (location = 0) in vec3 vPosition;

	out vec4 v_pos;

#if DrawAlphaMasked
	layout (location = 2) in vec2 vTexture;
	out vec2 v_texture;
#endif

	void main()
	{
		#if DrawAlphaMasked
			v_texture = vTexture;
		#endif

			vec4 skinnedVPos = vec4(vPosition, 1.0);

			if (isSkinned)
			{
					skin(skinnedVPos, skinnedVPos);
			}

			vec4 worldPos = perDraw._model * skinnedVPos;

			// Precompute inverse far plane on CPU if possible.
			v_pos       = (camera.view * worldPos) * (1.0 / camera.farPlane);
			gl_Position = camera.projectionView * worldPos;
	}
	-->
	</source>
</shader>