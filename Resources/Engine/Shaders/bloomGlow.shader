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
 * The reduction is ordered so that a moving highlight can not make the halo swim.
 *
 * The threshold is applied to every sample on its own, and the thresholded contributions are what
 * gets averaged. Testing the block average instead put the non linearity behind the reduction: a sub
 * pixel glint entered the block divided by sixteen, so whether the block landed above the smoothstep
 * knee depended on where inside the block the glint sat, and a small frame to frame ripple in the
 * glint came out as an on / off swing in the glow. Thresholded per sample the block average is a
 * linear combination of sixteen contributions, and ripple in one of them stays proportional.
 *
 * The average is a Karis average: every sample is weighted by 1 / (1 + luminance), so the sample
 * whose value is the one that moves counts the least. A plain average lets a single very bright
 * texel own its block, and the frame to frame change of that texel is what the halo repeats back.
 * The weights are normalized, so the reduction stays an average and not a weighted sum, and a block
 * of equal samples is unchanged by the weighting: a wide bright surface keeps its glow, and only the
 * halo of a single hot texel shrinks.
 *
 * The HDR ceiling is applied before both, so one very bright pixel can not own the halo around it
 * even through the Karis weight.
 *
 * Level zero of the pyramid is all this pass writes. Every level above it is the box average of
 * the level below, which the texture's mip chain provides.
 */

float Luminance (vec3 c)
{
	return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

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
	float weightSum = 0.0;

	for (int y = 0; y < 4; ++y)
	{
		for (int x = 0; x < 4; ++x)
		{
			vec3 c = texture(s_color, base + vec2(float(x), float(y)) * srcTexel).rgb;
			c      = Prefilter(min(c, vec3(bloom.params.y)));

			float weight = 1.0 / (1.0 + Luminance(c));
			sum         += c * weight;
			weightSum   += weight;
		}
	}

	fragColor = vec4(weightSum > 0.0 ? sum / weightSum : vec3(0.0), 1.0);
}
	-->
	</source>
</shader>
