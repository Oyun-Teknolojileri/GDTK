<shader>
	<type name = "fragmentShader" />
	<include name = "vulkanCompatInc.shader" />
	<include name = "materialCacheInc.shader" />
	<include name = "drawDataInc.shader" />
	<include name = "normalEncodingInc.shader" />
	<define name = "DrawAlphaMasked" val="0,1" />
	<texture slot = "0" name = "s_diffuseColor" />
	<texture slot = "9" name = "s_normalMap" />
	<texture slot = "10" name = "s_metallicRoughness" />
	<source>
	<!--
	
	precision highp float;
	precision highp int;

	TK_LOC(0) in float v_linearDepth;
	TK_LOC(1) in vec3 v_normal;
	TK_LOC(2) in vec2 v_texture;
	TK_LOC(3) in mat3 TBN;

	layout (location = 0) out vec4 fragNormalDepth;

	TK_SAMPLER_BINDING(0) uniform sampler2D s_diffuseColor; // color
	TK_SAMPLER_BINDING(9) uniform sampler2D s_normalMap; // normal
TK_SAMPLER_BINDING(10) uniform sampler2D s_metallicRoughness; // g: roughness, b: metalness (glTF)

	void main()
	{
		Material material = GetMaterial();
	
		vec4 color;
		if (material.diffuseTextureInUse > 0)
		{
			color = texture(s_diffuseColor, v_texture).rgba;
		}
		else
		{
			color = vec4(material.color, material.alpha);
		}

	#if DrawAlphaMasked
		if (color.a <= material.alphaMaskThreshold)
		{
			discard;
		}
	#endif

		vec3 normal;
		if (material.normalMapInUse == 1)
		{
			normal = texture(s_normalMap, v_texture).xyz;
			normal = normal * 2.0 - 1.0;
			normal = TBN * normal;
			normal = normalize(normal);
		}
		else
		{
			normal = normalize(v_normal);
		}

		vec2 encodedNormal = encodeNormal(normal);

		// Roughness, per pixel. A material that carries a metallic roughness texture has to be sampled here:
		// material.roughness is the scalar factor that multiplies that texture (glTF's roughnessFactor), so
		// writing the factor alone gives every pixel of the surface the same roughness - and a factor of 1.0,
		// which is what a textured material usually carries, puts the whole surface above SSR's roughness
		// cutoff and keeps it out of the reflections entirely.
		//
		// The renderer binds the texture by this sampler's name (GpuProgram resolves s_metallicRoughness), so
		// declaring it is what makes the fetch possible; the branch keeps a material without the texture from
		// reading whatever was bound last.
		float roughness = material.roughness;
		if (material.metallicRoughnessTextureInUse == 1)
		{
			roughness *= texture(s_metallicRoughness, v_texture).g;
		}

		// .a carried a constant 1.0 before; SSR reads the per pixel roughness from it to decide how
		// much of the screen space reflection it may take over from the environment / sky.
		fragNormalDepth = vec4(encodedNormal, v_linearDepth, roughness);
	}
	-->
	</source>
</shader>