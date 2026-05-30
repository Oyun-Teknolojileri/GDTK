<shader>
	<type name = "fragmentShader" />
	<include name = "pbrPrecompute.shader" />
	<include name="vulkanCompatInc.shader" />
	<source>
	<!--
		
		precision highp float;

		TK_LOC(2) in vec2 v_texture;
		out vec4 fragColor;

		// Google Filament's GGX visibility term for IBL
		// https://google.github.io/filament/Filament.html
		float GDFG(float NoV, float NoL, float a)
		{
			float a2 = a * a;
			float GGXL = NoV * sqrt((-NoL * a2 + NoL) * NoL + a2);
			float GGXV = NoL * sqrt((-NoV * a2 + NoV) * NoV + a2);
			return (2.0 * NoL) / (GGXV + GGXL);
		}

		vec2 DFG(float NoV, float a)
		{
			const uint SAMPLE_COUNT = 1024u;

			vec3 V;
			V.x = sqrt(1.0 - NoV * NoV);
			V.y = 0.0;
			V.z = NoV;

			vec2 r = vec2(0.0);

			vec3 N = vec3(0.0, 0.0, 1.0);

			for(uint i = 0u; i < SAMPLE_COUNT; ++i)
			{
				vec2 Xi = Hammersley(i, SAMPLE_COUNT);
				vec3 H  = ImportanceSampleGGX(Xi, N, a);
				vec3 L  = 2.0 * dot(V, H) * H - V;

				float VoH = clamp(dot(V, H), 0.0, 1.0);
				float NoL = clamp(L.z, 0.0, 1.0);
				float NoH = clamp(H.z, 0.0, 1.0);

				if(NoL > 0.0)
				{
					float G = GDFG(NoV, NoL, a);
					float Gv = G * VoH / NoH;
					float Fc = pow(1.0 - VoH, 5.0);
					r.x += Gv * (1.0 - Fc);
					r.y += Gv * Fc;
				}
			}

			return r * (1.0 / float(SAMPLE_COUNT));
		}

		void main() 
		{
			vec2 integratedBRDF = DFG(v_texture.x, v_texture.y);
			fragColor = vec4(integratedBRDF, 0.0, 0.0);
		}
	-->
	</source>
</shader>