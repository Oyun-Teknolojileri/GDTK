<shader>
	<type name = "fragmentShader" />
	<include name = "vulkanCompatInc.shader" />
	<source>
	<!--

	precision highp float;

	TK_LOC(0) in vec3 v_color;

	layout (location = 0) out vec4 fragColor;

	void main()
	{
		// Unlit: the colour is the light the probe holds, not a shade of the marker. The editor
		// draws over a frame the scene path has already tonemapped and gamma encoded, so the
		// probe's linear irradiance is encoded here instead of being written raw.
		fragColor = vec4(pow(clamp(v_color, 0.0, 1.0), vec3(1.0 / 2.2)), 1.0);
	}

	-->
	</source>
</shader>
