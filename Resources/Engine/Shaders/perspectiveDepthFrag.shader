<shader>
	<type name = "fragmentShader" />
	<include name = "VSMCommon.shader" />
	<include name = "materialCacheInc.shader" />
	<define name = "DrawAlphaMasked" val="0,1" />
	<define name = "EVSM4" val="0,1" />
	<source>
	<!--
    #version 300 es
    precision highp float;
    precision lowp int;

    in vec4 v_pos;

#if DrawAlphaMasked
    in vec2 v_texture;
#endif

    out vec4 fragColor;

    uniform sampler2D s_texture0;

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
        vec2 vsmDepth = WarpDepth(depth, EvsmExponents);

    #if EVSM4
        fragColor = vec4(vsmDepth.xy, vsmDepth.xy * vsmDepth.xy);
    #else
        fragColor = vec4(vsmDepth.x, vsmDepth.x * vsmDepth.x, 0.0, 0.0);
    #endif
    }
	-->
	</source>
</shader>