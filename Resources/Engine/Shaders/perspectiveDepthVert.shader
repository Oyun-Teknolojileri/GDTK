<shader>
	<type name = "vertexShader" />
	<include name = "skinning.shader" />
	<include name = "cameraDataInc.shader" />
	<define name = "DrawAlphaMasked" val="0,1" />
	<include name = "perDrawDataInc.shader" />
	<include name = "instanceDataInc.shader" />
	<define name = "INSTANCE_TABLE" val = "0,1" />
	<source>
	<!--
	
	precision highp float;
	precision highp int;

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

			vec4 worldPos = INSTANCE_MODEL_MATRIX * skinnedVPos;

			// Precompute inverse far plane on CPU if possible.
			v_pos       = (camera.view * worldPos) * (1.0 / camera.farPlane);
			gl_Position = camera.projectionView * worldPos;
	}
	-->
	</source>
</shader>