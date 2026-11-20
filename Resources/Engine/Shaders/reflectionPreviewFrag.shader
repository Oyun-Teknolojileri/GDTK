<shader>
	<type name = "fragmentShader" />
	<include name = "vulkanCompatInc.shader" />
	<include name = "cameraDataInc.shader" />
	<include name = "drawDataInc.shader" />
	<texture slot = "13" name = "s_iblSpecular" viewType = "cube" />
	<source>
	<!--

	precision highp float;

	TK_LOC(0) in vec3 v_worldPos;
	TK_LOC(1) in vec3 v_worldNormal;

	layout (location = 0) out vec4 fragColor;

	// The prefiltered cubemap of the environment volume the draw is assigned to. The renderer binds
	// it from the render job, so this shader only has to look it up.
	TK_SAMPLER_BINDING(13) uniform samplerCube s_iblSpecular;

	// A mirror ball for the reflection probe preview: the captured cubemap and nothing else.
	//
	// No direct light, no ambient, no emissive, no Fresnel and no tone curve: every term the PBR
	// shader would add is what made the preview show the room's lighting instead of the probe's
	// capture. Mip zero is the mirror direction the probe was captured for.
	void main()
	{
		vec3 n = normalize(v_worldNormal);
		vec3 e = normalize(camera.position - v_worldPos);
		vec3 r = reflect(-e, n);

		// The cubemap is aligned to the volume owner's local axes; the render job fills slot zero's
		// inverse transform with that entity's transform.
		vec3 dir = (GetVolumeInverseTransform(0) * vec4(r, 0.0)).xyz;

		fragColor = vec4(textureLod(s_iblSpecular, dir, 0.0).rgb, 1.0);
	}

	-->
	</source>
</shader>
