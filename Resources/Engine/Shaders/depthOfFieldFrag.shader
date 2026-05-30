<shader>
	<type name = "fragmentShader" />
	<include name = "vulkanCompatInc.shader" />
	<include name = "dofPassDataInc.shader" />
	<texture slot = "0" name = "s_texture0" />
	<texture slot = "1" name = "s_texture1" />
	<source>
	<!--
		
		precision highp float;
TK_LOC(2) in vec2 v_texture;
		out vec4 fragColor;


		TK_SAMPLER_BINDING(0) uniform sampler2D s_texture0; //Image to be processed
		TK_SAMPLER_BINDING(1) uniform sampler2D s_texture1; //Packed normal+depth: B channel = linear depth

		const float GOLDEN_ANGLE = 2.39996323;

		float getBlurSize(float depth)
		{
			float focusPoint = dof.focusAndBlur.x;
			float focusScale = dof.focusAndBlur.y;
			float blurSize = dof.focusAndBlur.z;
			float coc = clamp((1.0 / max(focusPoint, 0.001) - 1.0 / max(depth, 0.001))*focusScale, -1.0, 1.0);
			return abs(coc) * blurSize;
		}

		vec3 depthOfField(vec2 texCoord)
		{
			vec3 color = texture(s_texture0, texCoord).rgb;
			float focusScale = dof.focusAndBlur.y;
			float blurSize = dof.focusAndBlur.z;
			float radiusScale = dof.focusAndBlur.w;
			if(focusScale == 0.0f){
				return color;
			}

			float centerDepth = texture(s_texture1, texCoord).b;

			float centerSize = getBlurSize(centerDepth);
			float tot = 1.0;
			float radius = radiusScale;
			for (float ang = 0.0; radius<blurSize; ang += GOLDEN_ANGLE)
			{
				vec2 tc = texCoord + vec2(cos(ang), sin(ang)) * dof.pixelSizeAndPad.xy * radius;
				vec3 sampleColor = texture(s_texture0, tc).rgb;
				float sampleDepth = texture(s_texture1, tc).b;
				float sampleSize = getBlurSize(sampleDepth);
				if (sampleDepth > centerDepth)
					sampleSize = clamp(sampleSize, 0.0, centerSize*2.0);
				float m = smoothstep(radius-0.5, radius+0.5, sampleSize);
				color += mix(color/tot, sampleColor, m);
				tot += 1.0;   radius += radiusScale/radius;
			}
			return color /= tot;
		}


		void main()
		{
			vec2 uv = vec2(v_texture.x, v_texture.y);
			fragColor = vec4(depthOfField(uv), 1.0f);
		}
	-->
	</source>
</shader>