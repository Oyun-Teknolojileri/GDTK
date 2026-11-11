<shader>
	<type name = "fragmentShader" />
	<include name = "vulkanCompatInc.shader" />
	<include name = "ssrCommonInc.shader" />
	<texture slot = "0" name = "s_trace" />
	<source>
	<!--

precision highp float;
precision highp int;

TK_LOC(2) in vec2 v_texture;

layout (location = 0) out vec4 fragColor;

TK_SAMPLER_BINDING(0) uniform sampler2D s_trace; // rgb: this frame's reflection, a: coverage.

// One texel of this target covers 8x8 texels of the trace. The reference keeps such an average for two
// reasons: it is the low frequency anchor the temporal clip leans towards, and it lets a pixel with few
// samples borrow the radiance of its block instead of showing a single noisy ray.
//
// The average is luminance weighted, so one pixel that caught a specular highlight can not drag a whole
// block: the weight is the inverse of the luminance, bounded.
void main()
{
	vec2  texel  = ssrPass.screenParams.xy * 8.0;
	vec2  origin = v_texture - texel * 3.5;
	vec3  sum    = vec3(0.0);
	float weight = 0.0;

	for (int y = 0; y < 8; ++y)
	{
		for (int x = 0; x < 8; ++x)
		{
			vec4 sampleValue = texture(s_trace, origin + vec2(float(x), float(y)) * texel);
			if (sampleValue.a < 0.5)
			{
				continue;
			}

			float luminance = max(dot(sampleValue.rgb, vec3(0.299, 0.587, 0.114)), 0.001);
			float w         = 1.0 / (1.0 + luminance);

			sum    += sampleValue.rgb * w;
			weight += w;
		}
	}

	fragColor = vec4(weight > 0.0 ? sum / weight : vec3(0.0), 1.0);
}

	-->
	</source>
</shader>
