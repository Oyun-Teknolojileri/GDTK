<shader>
	<type name = "fragmentShader" />
	<include name = "vulkanCompatInc.shader" />
	<include name = "pbrPrecompute.shader" />
	<include name = "preFilterEnvMapPassDataInc.shader" />
	<texture slot = "6" name = "s_cubeMap" viewType = "cube" />
	<source>
	<!--
		
		precision highp float;

		TK_SAMPLER_BINDING(6) uniform samplerCube s_cubeMap;

		in vec3 v_pos;
		out vec4 fragColor;

		const float FLT_MAX = 3.402823466e+38;

		void main()
		{		
			// Per output texel. At the coarse mips the GGX lobe is wider than the whole face, so the
			// same count is doing far more work than the result can resolve. Lowering it is not the
			// lever it looks like: those mips are a fraction of a percent of the total sample count
			// (mip 0 of a 512 face is half of it) and they read a fully cache resident source. The
			// lever is the source level picked below, which is why the input cubemap carries a mip
			// chain. See Hdri::GenerateIrradianceCaches.
			const uint SAMPLE_COUNT = 256u;

			vec3 N = normalize(v_pos);
			
			// Make the simplifying assumption that V equals R equals the normal 
			vec3 R = N;
			vec3 V = R;

			// Convert perceptual preFilterEnvMap.params.y to alpha for Filament's D_GGX
			float alpha = preFilterEnvMap.params.y * preFilterEnvMap.params.y;

			// Solid angle of one output texel at THIS mip. params.x is the mip face size, so the
			// footprint shrinks as the mip coarsens and the source level below tracks it.
			float saTexel = 4.0 * PI / (6.0 * preFilterEnvMap.params.x * preFilterEnvMap.params.x);

			vec3 prefilteredColor = vec3(0.0);
			float totalWeight = 0.0;

			for(uint i = 0u; i < SAMPLE_COUNT; ++i)
			{
				vec2 Xi = Hammersley(i, SAMPLE_COUNT);
				vec3 H = ImportanceSampleGGX(Xi, N, preFilterEnvMap.params.y);
				vec3 L = normalize(2.0 * dot(V, H) * H - V);

				float NdotL = max(dot(N, L), 0.0);
				if(NdotL > 0.0)
				{
					float NdotH = max(dot(N, H), 0.0);
					float HdotV = max(dot(H, V), 0.0);

					// Use Filament-style D_GGX for PDF calculation
					float D = distribution(alpha, NdotH, H);
					float pdf = D * NdotH / (4.0 * HdotV) + 0.0001; 

					// Solid angle this one sample stands for. Filtering the source down to that
					// footprint is what removes the sampling noise; keeping the raw level 0 taps was
					// what left the mottled patches in the irradiance caches. Half a mip of bias
					// trades a little sharpness for a noticeable drop in variance. A level past the
					// end of the source chain is clamped by textureLod itself.
					float saSample = 1.0 / (float(SAMPLE_COUNT) * pdf + 0.0001);
					float mipLevel = preFilterEnvMap.params.y == 0.0 ? 0.0
					               : max(0.5 * log2(saSample / saTexel) + 0.5, 0.0);

					vec3 texel = textureLod(s_cubeMap, L, mipLevel).rgb;
					texel = clamp(texel, vec3(0.0), vec3(FLT_MAX));
					prefilteredColor += texel * NdotL;
					
					totalWeight += NdotL;
				}
			}

			prefilteredColor = prefilteredColor / totalWeight;

			fragColor = vec4(prefilteredColor, 1.0);
		} 
	-->
	</source>
</shader>