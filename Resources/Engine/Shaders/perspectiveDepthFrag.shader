<shader>
	<type name = "fragmentShader" />
	<include name = "VSMCommon.shader" />
	<include name = "materialCacheInc.shader" />
	<define name = "DrawAlphaMasked" val="0,1" />
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
        float vsmDepth = WarpDepth(depth);

        fragColor = vec2(vsmDepth, vsmDepth * vsmDepth);
    }
	-->
	</source>
</shader>
