<shader>
	<type name = "fragmentShader" />
	<include name = "gradientSkyboxPassDataInc.shader" />
	<source>
	<!--
		
		precision highp float;

		out vec4 fragColor;
		in vec3 v_pos;

		void main()
		{
			vec3 texCoord = normalize(v_pos);
			float d = texCoord.y;
			float s = sign(d);
			vec3 gradColor = s < 0.0 ? gradientSky.bottomColor.xyz : gradientSky.topColor.xyz;
			float gradAmount = pow(abs(d), gradientSky.exponentAndPad.x);
			vec3 color = mix(gradientSky.middleColor.xyz, gradColor, gradAmount);
			fragColor = vec4(color, 1.0);
		}
	-->
	</source>
</shader>