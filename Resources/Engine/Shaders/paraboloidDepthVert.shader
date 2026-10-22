<shader>
	<type name = "vertexShader" />
	<include name = "skinning.shader" />
	<include name = "cameraDataInc.shader" />
	<define name = "DrawAlphaMasked" val="0,1" />
	<include name = "perDrawDataInc.shader" />
	<include name = "instanceDataInc.shader" />
	<define name = "INSTANCE_TABLE" val = "0,1" />
	<define name = "PARABOLOID_RIM_INV" val = "0.98039216" />
	<source>
	<!--
	
	precision highp float;
	precision lowp int;

	// Fixed Attributes.
	layout (location = 0) in vec3 vPosition;

	out vec4 v_pos;

#if DrawAlphaMasked
	layout (location = 2) in vec2 vTexture;
	out vec2 v_texture;
#endif

	/*
	 * Point light shadow map, one hemisphere per map: the scene is projected onto a paraboloid
	 * instead of being rendered six times into a cube. The camera for this pass sits at the light
	 * and looks along -Z, so the hemisphere this map covers is exactly the half in front of it,
	 * and no uniform has to say which half it is.
	 *
	 * The projection stretches without bound towards the rim (dir.z approaches zero), and the
	 * distortion there would be visible in the map. The radii are scaled down by 1 / 1.15, so the
	 * hemisphere lands inside about 87 percent of the map and the stretched rim is never sampled.
	 */
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
			vec4 viewPos  = camera.view * worldPos;

			vec3 dir   = normalize(viewPos.xyz);
			float dist = length(viewPos.xyz);

			// dir.z is negative for the half in front of the camera, so the denominator stays positive.
			float denom = max(1.0 - dir.z, 1e-4);
			vec2 proj   = (dir.xy / denom) * PARABOLOID_RIM_INV;

			// The depth test and the stored moment both use the distance to the light, normalized.
			v_pos       = vec4(viewPos.xyz, 1.0) * (1.0 / camera.farPlane);
			gl_Position = vec4(proj, dist / camera.farPlane, 1.0);
	}
	-->
	</source>
</shader>
