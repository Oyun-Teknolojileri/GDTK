<shader>
	<type name = "fragmentShader" />
	<include name = "vulkanCompatInc.shader" />
	<define name = "TextureArray" val = "0,1" />
	<define name = "KernelSize" val = "7,3,5" />
	<define name = "BlurClampEnabled" val = "0,1" />
	<include name = "gaussBlurPassDataInc.shader" />
	<texture slot = "0" name = "s_texture0" />
	<texture slot = "1" name = "s_texture1" />
	<source>
	<!--
		#version 300 es
		precision highp float;
		precision highp sampler2DArray;
		TK_SAMPLER_BINDING(0) uniform sampler2D s_texture0;
		TK_SAMPLER_BINDING(1) uniform sampler2DArray s_texture1;
	TK_LOC(2) in vec3 v_texture;
		out vec4 fragColor;

#if BlurClampEnabled == 1
		#define CLAMP_UV(uv) clamp(uv, gauss.blurClampMinMax.xy, gauss.blurClampMinMax.zw)
#else
		#define CLAMP_UV(uv) (uv)
#endif
		
		void main()
		{
			vec4 color = vec4(0.0);

		#if TextureArray == 1

			#if KernelSize == 3
				color += texture(s_texture1, vec3(CLAMP_UV(v_texture.xy + vec2(-1.0) * gauss.blurScaleAndLayer.xy), v_texture.z)) * (1.0 / 4.0);
				color += texture(s_texture1, vec3(CLAMP_UV(v_texture.xy + vec2(0.0) * gauss.blurScaleAndLayer.xy), v_texture.z)) * (2.0 / 4.0);
				color += texture(s_texture1, vec3(CLAMP_UV(v_texture.xy + vec2(1.0) * gauss.blurScaleAndLayer.xy), v_texture.z)) * (1.0 / 4.0);
			#elif KernelSize == 5
				color += texture(s_texture1, vec3(CLAMP_UV(v_texture.xy + vec2(-2.0) * gauss.blurScaleAndLayer.xy), v_texture.z)) * (1.0 / 16.0);
				color += texture(s_texture1, vec3(CLAMP_UV(v_texture.xy + vec2(-1.0) * gauss.blurScaleAndLayer.xy), v_texture.z)) * (4.0 / 16.0);
				color += texture(s_texture1, vec3(CLAMP_UV(v_texture.xy + vec2(0.0) * gauss.blurScaleAndLayer.xy), v_texture.z)) * (6.0 / 16.0);
				color += texture(s_texture1, vec3(CLAMP_UV(v_texture.xy + vec2(1.0) * gauss.blurScaleAndLayer.xy), v_texture.z)) * (4.0 / 16.0);
				color += texture(s_texture1, vec3(CLAMP_UV(v_texture.xy + vec2(2.0) * gauss.blurScaleAndLayer.xy), v_texture.z)) * (1.0 / 16.0);
			#else
				color += texture(s_texture1, vec3(CLAMP_UV(v_texture.xy + vec2(-3.0) * gauss.blurScaleAndLayer.xy), v_texture.z)) * (1.0 / 64.0);
				color += texture(s_texture1, vec3(CLAMP_UV(v_texture.xy + vec2(-2.0) * gauss.blurScaleAndLayer.xy), v_texture.z)) * (6.0 / 64.0);
				color += texture(s_texture1, vec3(CLAMP_UV(v_texture.xy + vec2(-1.0) * gauss.blurScaleAndLayer.xy), v_texture.z)) * (15.0 / 64.0);
				color += texture(s_texture1, vec3(CLAMP_UV(v_texture.xy + vec2(0.0) * gauss.blurScaleAndLayer.xy), v_texture.z)) * (20.0 / 64.0);
				color += texture(s_texture1, vec3(CLAMP_UV(v_texture.xy + vec2(1.0) * gauss.blurScaleAndLayer.xy), v_texture.z)) * (15.0 / 64.0);
				color += texture(s_texture1, vec3(CLAMP_UV(v_texture.xy + vec2(2.0) * gauss.blurScaleAndLayer.xy), v_texture.z)) * (6.0 / 64.0);
				color += texture(s_texture1, vec3(CLAMP_UV(v_texture.xy + vec2(3.0) * gauss.blurScaleAndLayer.xy), v_texture.z)) * (1.0 / 64.0);
			#endif

		#else

			#if KernelSize == 3
				color += texture(s_texture0, CLAMP_UV(v_texture.xy + vec2(-1.0) * gauss.blurScaleAndLayer.xy)) * (1.0 / 4.0);
				color += texture(s_texture0, CLAMP_UV(v_texture.xy + vec2(0.0) * gauss.blurScaleAndLayer.xy)) * (2.0 / 4.0);
				color += texture(s_texture0, CLAMP_UV(v_texture.xy + vec2(1.0) * gauss.blurScaleAndLayer.xy)) * (1.0 / 4.0);
			#elif KernelSize == 5
				color += texture(s_texture0, CLAMP_UV(v_texture.xy + vec2(-2.0) * gauss.blurScaleAndLayer.xy)) * (1.0 / 16.0);
				color += texture(s_texture0, CLAMP_UV(v_texture.xy + vec2(-1.0) * gauss.blurScaleAndLayer.xy)) * (4.0 / 16.0);
				color += texture(s_texture0, CLAMP_UV(v_texture.xy + vec2(0.0) * gauss.blurScaleAndLayer.xy)) * (6.0 / 16.0);
				color += texture(s_texture0, CLAMP_UV(v_texture.xy + vec2(1.0) * gauss.blurScaleAndLayer.xy)) * (4.0 / 16.0);
				color += texture(s_texture0, CLAMP_UV(v_texture.xy + vec2(2.0) * gauss.blurScaleAndLayer.xy)) * (1.0 / 16.0);
			#else
				color += texture(s_texture0, CLAMP_UV(v_texture.xy + vec2(-3.0) * gauss.blurScaleAndLayer.xy)) * (1.0 / 64.0);
				color += texture(s_texture0, CLAMP_UV(v_texture.xy + vec2(-2.0) * gauss.blurScaleAndLayer.xy)) * (6.0 / 64.0);
				color += texture(s_texture0, CLAMP_UV(v_texture.xy + vec2(-1.0) * gauss.blurScaleAndLayer.xy)) * (15.0 / 64.0);
				color += texture(s_texture0, CLAMP_UV(v_texture.xy + vec2(0.0) * gauss.blurScaleAndLayer.xy)) * (20.0 / 64.0);
				color += texture(s_texture0, CLAMP_UV(v_texture.xy + vec2(1.0) * gauss.blurScaleAndLayer.xy)) * (15.0 / 64.0);
				color += texture(s_texture0, CLAMP_UV(v_texture.xy + vec2(2.0) * gauss.blurScaleAndLayer.xy)) * (6.0 / 64.0);
				color += texture(s_texture0, CLAMP_UV(v_texture.xy + vec2(3.0) * gauss.blurScaleAndLayer.xy)) * (1.0 / 64.0);
			#endif

		#endif

		  fragColor = color;
		}
	-->
	</source>
</shader>