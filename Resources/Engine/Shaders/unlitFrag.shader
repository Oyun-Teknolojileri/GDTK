<shader>
	<type name = "fragmentShader" />
	<include name = "vulkanCompatInc.shader" />
	<include name = "materialCacheInc.shader" />
	<include name = "drawDataInc.shader" />
	<define name = "DrawAlphaMasked" val="0,1" />
	<texture slot = "0" name = "s_texture0" />
	<source>
	<!--
	
	precision highp float;
	precision lowp int;

	TK_LOC(2) in vec2 v_texture;
	layout(location = 0) out vec4 fragColor;
	TK_SAMPLER_BINDING(0) uniform sampler2D s_texture0;

	void main()
	{
		Material material = GetMaterial();
	
		vec4 color;
		if (material.diffuseTextureInUse > 0)
		{
			color = texture(s_texture0, v_texture);
		}
		else
		{
			color = vec4(material.color, material.alpha);
		}

#if DrawAlphaMasked
		if (material.useAlphaMask > 0)
		{
			if (color.a <= material.alphaMaskThreshold)
			{
				discard;
			}
		}
#endif
		
		fragColor = color;
	}
	-->
	</source>
</shader>