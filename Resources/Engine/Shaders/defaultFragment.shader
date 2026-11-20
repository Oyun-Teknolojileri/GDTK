<shader>
	<type name = "fragmentShader" />
	<include name = "vulkanCompatInc.shader" />
	<include name = "lighting.shader" />
	<include name = "ibl.shader" />
	<include name = "AO.shader" />
	<include name = "cameraDataInc.shader" />
	<include name = "materialCacheInc.shader" />
	<define name = "DrawAlphaMasked" val="0,1" />
	<define name = "ShadowPCF" val="0,4,9,16" />
	<define name = "CascadeBlend" val="0,1" />
	<define name = "highlightCascades" val="0,1" />
	<define name = "ShadingMode" val="0,1,2,3,4,5" />
	<texture slot = "0" name = "s_diffuseColor" />
	<texture slot = "1" name = "s_emissiveColor" />
	<texture slot = "4" name = "s_metallicRoughness" />
	<texture slot = "9" name = "s_normalMap" />
	<source>
	<!--
	
	precision highp float;
	precision highp sampler2D;
	precision highp sampler2DArray;
	precision highp samplerCube;
	precision highp int;

	TK_SAMPLER_BINDING(0) uniform sampler2D s_diffuseColor; // color
	TK_SAMPLER_BINDING(1) uniform sampler2D s_emissiveColor; // emissive
	TK_SAMPLER_BINDING(4) uniform sampler2D s_metallicRoughness; // metallic-roughness
	TK_SAMPLER_BINDING(9) uniform sampler2D s_normalMap; // normal

	#define SHADE_LIGHTING_ONLY 1
	#define SHADE_ALBEDO_ONLY 2
	#define SHADE_NORMAL_ONLY 3
	#define SHADE_METALLIC_ONLY 4
	#define SHADE_ROUGHNESS_ONLY 5

	TK_LOC(0) in vec3 v_worldPos;
	TK_LOC(1) in vec3 v_worldNormal;
	TK_LOC(2) in vec2 v_texture;
	TK_LOC(3) in float v_viewDepth;
	TK_LOC(4) in mat3 TBN;

	layout (location = 0) out vec4 fragColor;

	void main()
	{
		Material material = GetMaterial();

		vec4 color;
		if(material.diffuseTextureInUse > 0)
		{
			color = texture(s_diffuseColor, v_texture);
		}
		else
		{
			color = vec4(material.color, material.alpha);
		}

		vec3 emissive;
		if(material.emissiveTextureInUse > 0)
		{
			emissive = texture(s_emissiveColor, v_texture).xyz;
		}
		else
		{
			emissive = material.emissiveColor;
		}

		float metallic, roughness;
		if (material.metallicRoughnessTextureInUse > 0)
		{
			// Texture Holds Occulussion Roughness Metallic values in rgb.
			// Occulusion is not supported at the moment. ( No Value )
			vec3 orm = texture(s_metallicRoughness, v_texture).rgb;
			metallic = orm.b;
			roughness = orm.g;
		}
		else
		{
			metallic = material.metallic;
			roughness = material.roughness;
		}
		metallic = clamp(metallic, 0.0, 1.0);
		float perceptualRoughness = clamp(roughness, 0.045, 1.0);

	#if DrawAlphaMasked
		if(color.a <= material.alphaMaskThreshold)
		{
			discard;
		}
	#endif

	#if ShadingMode == SHADE_ALBEDO_ONLY
		fragColor = color;
		return;
	#endif

	#if ShadingMode == SHADE_METALLIC_ONLY
		fragColor = vec4(metallic, metallic, metallic, 1.0);
		return;
	#endif

	#if ShadingMode == SHADE_ROUGHNESS_ONLY
		fragColor = vec4(perceptualRoughness, perceptualRoughness, perceptualRoughness, 1.0);
		return;
	#endif

	#if ShadingMode == SHADE_LIGHTING_ONLY
		// The albedo is replaced by the 18 percent grey card, the linear reference the tone curve is
		// exposed for: white washed the frame out and left the reflection with no contrast to read
		// against, and a darker grey still keeps the light response the mode exists to show.
		color.xyz = vec3(0.18);
	#endif

		vec3 n;
		if (material.normalMapInUse > 0)
		{
			n = texture(s_normalMap, v_texture).xyz;
			n = n * 2.0 - 1.0;
			n = TBN * n;
			n = normalize(n);
		}
		else
		{
			n = normalize(v_worldNormal);
		}

	#if ShadingMode == SHADE_NORMAL_ONLY
		fragColor = vec4(n * 0.5 + 0.5, 1.0);
		return;
	#endif

		perceptualRoughness = specularAntiAliasing(perceptualRoughness,
		                                           n,
		                                           graphicConstants.specularAntiAliasing.x,
		                                           graphicConstants.specularAntiAliasing.y);
		roughness = perceptualRoughnessToRoughness(perceptualRoughness);

		vec3 e = normalize(camera.position - v_worldPos);

		// Compute energy compensation for multiscattering
		vec3 f0 = BaseReflectivityPBR(vec3(0.04), color.xyz, metallic);
		float NdotV = max(dot(n, e), 0.0);
		vec2 dfg = texture(s_brdfLut, vec2(NdotV, perceptualRoughness)).rg;
		vec3 energyComp = EnergyCompensation(dfg, f0);

		vec3 irradiance = PBRLighting(v_worldPos, v_viewDepth, n, e, camera.position, color.xyz, metallic, roughness, energyComp);

		float ambientOcclusion = AmbientOcclusion();

		vec3 iblDiffuse;
		vec3 skyIrradianceRef;
		vec3 iblSpecular = IBLPBR(n,
		                          e,
		                          color.xyz,
		                          metallic,
		                          perceptualRoughness,
		                          dfg,
		                          energyComp,
		                          v_worldPos,
		                          iblDiffuse,
		                          skyIrradianceRef);

		// IBLPBR returns the cubemap specular term and hands the sky's diffuse back
		// separately: a local volume carries no diffuse any more, so the probe volume
		// below replaces exactly the sky ambient and nothing else.
		vec3 ibl = iblSpecular * ambientOcclusion + iblDiffuse * ambientOcclusion;

		// Baked probe volume. Where it covers the pixel it is the whole diffuse term: the bake
		// already contains the sky and the scene's light with the visibility the probe saw, so
		// the sky's diffuse is replaced rather than corrected.
		float probeWeight = 0.0;
		vec3 probe = ProbeIrradiance(v_worldPos, n, probeWeight);
		if (probeWeight > 0.0)
		{
			vec3 E = SpecularDFG(dfg, f0);
			float aoStrength = perDraw._drawCommand.probeParams.z;
			vec3 probeDiffuse = probe * color.xyz * (1.0 - metallic) * (1.0 - E) *
			                    mix(1.0, ambientOcclusion, aoStrength);

			// Specular occlusion from the probe. The cubemap term here is the *unoccluded* sky, while
			// the probe holds the same irradiance as the bake measured it through the geometry, so
			// their ratio is how much of the sky this location really sees. Reflections damped by it
			// stay part of the lighting instead of floating over a room the probe painted dark, which
			// is the usual treatment where a volume carries no reflection of its own.
			float skyLuminance   = dot(skyIrradianceRef, vec3(0.2126, 0.7152, 0.0722));
			float probeLuminance = dot(probe, vec3(0.2126, 0.7152, 0.0722));
			float specularOcc    = skyLuminance > 1e-4 ?
			                           clamp(probeLuminance / skyLuminance, 0.0, 1.0) :
			                           1.0;

			// Only the broad, ambient end of the reflection is damped this way: a mirror inside the
			// volume still shows what it can see, because its reflection is not ambient.
			specularOcc = mix(1.0, specularOcc, perceptualRoughness);

			vec3 probeSpecular = iblSpecular * mix(1.0, specularOcc, probeWeight);

			ibl = probeSpecular * ambientOcclusion +
			      mix(iblDiffuse * ambientOcclusion, probeDiffuse, probeWeight);
		}

		irradiance += ibl;

		fragColor = vec4(irradiance, color.a) + vec4(emissive, 0.0);
	}
	-->
	</source>
</shader>