<shader>
	<type name = "fragmentShader" />
	<include name = "vulkanCompatInc.shader" />
	<include name = "gammaTonemapFxaaPassDataInc.shader" />
	<include name = "fxaaFunctions.shader" />
	<include name = "tonemapFunctions.shader" />
	<include name = "gammaFunctions.shader" />
	<source>
	<!--
		#version 300 es
		precision highp float;

		TK_SAMPLER_BINDING(0) uniform sampler2D s_texture0;
		in vec2 v_texture;

		out vec4 fragColor;

		void main()
		{
			vec2 uv = v_texture;

			if (gtf.enableFlags.x != 0)
			{
				fragColor = Fxaa(uv, s_texture0);
			}
			else
			{
				fragColor = texture(s_texture0, uv);
			}

			if (gtf.enableFlags.y != 0)
			{
				fragColor.rgb = Tonemap(fragColor.rgb);
			}

			if (gtf.enableFlags.z != 0)
			{
				fragColor.rgb = Gamma(fragColor.rgb);
			}
		}
	-->
	</source>
</shader>