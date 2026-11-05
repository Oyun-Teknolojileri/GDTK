<shader>
	<type name = "fragmentShader" />
	<include name = "vulkanCompatInc.shader" />
	<include name = "bloomPassDataInc.shader" />
	<texture slot = "0" name = "s_color" />
	<source>
	<!--

precision highp float;
precision highp sampler2D;

TK_SAMPLER_BINDING(0) uniform sampler2D s_color;

TK_LOC(2) in vec2 v_texture;
out vec4 fragColor;

/*
 * Bright pass of the glow pyramid.
 *
 * One texel of the target covers a 4x4 block of the frame - the target is a quarter of the frame
 * in each axis - so the whole block is averaged. A single sample made a thin highlight (a specular
 * glint, a lamp, a lit window) land on one texel and miss the next, which is what made the bloom
 * pop as the camera moved.
 *
 * Two shaping steps sit on top of the average. Every sample is clamped to an HDR ceiling before
 * it counts, so one very bright pixel cannot own the halo around it, and the threshold fades in
 * over a knee instead of cutting, so a gradient crossing it does not draw a band into the glow.
 *
 * Level zero of the pyramid is all this pass writes. Every level above it is the box average of
 * the level below, which the texture's mip chain provides.
 */

vec3 Prefilter (vec3 c)
{
	float brightness = max(c.r, max(c.g, c.b));
	float threshold  = bloom.params.x;
	float knee       = max(threshold * 0.5, 0.0001);
	float contribution = smoothstep(threshold - knee, threshold + knee, brightness);

	return c * contribution;
}

void main()
{
	vec2 srcTexel = 1.0 / vec2(textureSize(s_color, 0));
	vec2 base     = v_texture - srcTexel * 1.5;
	vec3 sum      = vec3(0.0);

	for (int y = 0; y < 4; ++y)
	{
		for (int x = 0; x < 4; ++x)
		{
			vec3 c = texture(s_color, base + vec2(float(x), float(y)) * srcTexel).rgb;
			sum += min(c, vec3(bloom.params.y));
		}
	}

	fragColor = vec4(Prefilter(sum * (1.0 / 16.0)), 1.0);
}
	-->
	</source>
</shader>
