<shader>
	<type name = "fragmentShader" />
	<include name = "vulkanCompatInc.shader" />
	<include name = "VSMCommon.shader" />
	<include name = "materialCacheInc.shader" />
	<define name = "DrawAlphaMasked" val="0,1" />
	<texture slot = "0" name = "s_texture0" />
	<source>
	<!--
    #version 300 es
    precision highp float;
    precision lowp int;

    in vec4 v_pos;

#if DrawAlphaMasked
    in vec2 v_texture;
#endif

    out vec2 fragColor;

    TK_SAMPLER_BINDING(0) uniform sampler2D s_texture0;

    void main()
    {
    #if DrawAlphaMasked
        Material material = GetMaterial();

        float alpha;
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

        float depth = length(v_pos.xyz);
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
