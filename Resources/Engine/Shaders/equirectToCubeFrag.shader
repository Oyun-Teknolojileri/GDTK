<shader>
	<type name = "fragmentShader" />
	<include name = "vulkanCompatInc.shader" />
	<include name = "cubemapEquirectPassDataInc.shader" />
	<texture slot = "0" name = "s_diffuseColor" />
	<source>
	<!--
		
		precision highp float;
		TK_SAMPLER_BINDING(0) uniform sampler2D s_diffuseColor;

		in vec3 v_pos;
		out vec4 fragColor;

		const vec2 invAtan = vec2(0.1591, 0.3183);
		vec2 SampleSphericalMap(vec3 v)
		{
		    vec2 uv = vec2(atan(v.z, v.x), asin(v.y));
		    uv *= invAtan;
		    uv += 0.5;
		    return uv;
		}

		void main()
		{
	    vec2 uv = SampleSphericalMap(normalize(v_pos));
	    vec3 color = texture(s_diffuseColor, uv).rgb;

			// cubemapEquirect.exposureAndPad.x
			color = vec3(1.0) - exp(-color * cubemapEquirect.exposureAndPad.x);

			fragColor = vec4(color, 1.0);
		}
	-->
	</source>
</shader>