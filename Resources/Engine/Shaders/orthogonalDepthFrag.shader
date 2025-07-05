<shader>
	<type name = "fragmentShader" />
	<include name = "VSM.shader" />
	<include name = "drawDataInc.shader" />
	<define name = "DrawAlphaMasked" val="0,1" />
	<define name = "EVSM4" val="0,1" />
	<source>
	<!--
	#version 300 es
	precision highp float;
	precision lowp int;

	in vec2 v_texture;
	in float z;
	out vec4 fragColor;

	uniform sampler2D s_texture0;

	void main()
	{
		Material material = GetMaterial();
	
	#if DrawAlphaMasked
		float alpha = 1.0;
		if (material.diffuseTextureInUse == 1)
		{
			alpha = texture(s_texture0, v_texture).a;
		}
		else
		{
			alpha = material.alpha;
		}

		if (alpha <= material.alphaMaskThreshold)
		{
			discard;
		}
	#endif

		gl_FragDepth = clamp(z, 0.0, 1.0);
		vec2 exponents = EvsmExponents;
		vec2 vsmDepth = WarpDepth(gl_FragDepth, exponents);

	#if EVSM4
		fragColor = vec4(vsmDepth.xy, vsmDepth.xy * vsmDepth.xy);
	#else
		fragColor = vec4(vsmDepth.xy, vsmDepth.xy * vsmDepth.xy).xzxz;
	#endif
	}
	-->
	</source>
</shader>