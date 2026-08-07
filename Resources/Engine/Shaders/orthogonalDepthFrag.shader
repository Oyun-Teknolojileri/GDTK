<shader>
	<type name = "fragmentShader" />
	<include name = "vulkanCompatInc.shader" />
	<include name = "VSMCommon.shader" />
	<include name = "materialCacheInc.shader" />
	<define name = "DrawAlphaMasked" val="0,1" />
	<define name = "Pancake" val="0,1" />
	<texture slot = "0" name = "s_diffuseColor" />
	<source>
	<!--
	
	precision highp float;
	precision lowp int;

#if DrawAlphaMasked
	in vec2 v_texture;
#endif

#if Pancake
	in float z;
#endif

	out vec2 fragColor;
	TK_SAMPLER_BINDING(0) uniform sampler2D s_diffuseColor;

	void main()
	{
	#if DrawAlphaMasked
		Material material = GetMaterial();

		float alpha;
		if (material.diffuseTextureInUse == 1)
		{
			alpha = texture(s_diffuseColor, v_texture).a;
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

	#if Pancake
		float depth = clamp(z, 0.0, 1.0);
		gl_FragDepth = depth;
	#else
		float depth = gl_FragCoord.z;
	#endif

		float vsmDepth = WarpDepth(depth);

		// Analytic variance from depth gradient across the texel (Filament style)
		float dzdx = dFdx(depth);
		float dzdy = dFdy(depth);
		float linearVariance = 0.25 * (dzdx * dzdx + dzdy * dzdy);
		float analyticVariance = VsmExponent * VsmExponent * vsmDepth * vsmDepth * linearVariance;
		float moment2 = min(vsmDepth * vsmDepth + analyticVariance, VsmMaxMoment);

		fragColor = vec2(vsmDepth, moment2);
	}
	-->
	</source>
</shader>
