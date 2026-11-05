<shader>
	<type name = "fragmentShader" />
	<include name = "vulkanCompatInc.shader" />
	<include name = "bloomPassDataInc.shader" />
	<texture slot = "0" name = "s_glow" />
	<source>
	<!--

precision highp float;
precision highp sampler2D;

TK_SAMPLER_BINDING(0) uniform sampler2D s_glow;

TK_LOC(2) in vec2 v_texture;
out vec4 fragColor;

/*
 * Composite of the glow pyramid.
 *
 * The bright pass wrote level zero and the texture's mip chain holds every level above it, each
 * one the box average of the level below. Sampling level i at the frame's resolution gives the
 * halo at 2^i the glow resolution - narrow levels keep the highlight tight to what emits it,
 * wide levels carry the spread - so the sum of the levels IS the bloom, and there is no upsample
 * chain to run and no per level target to read back.
 *
 * Two details make the cheap sum usable. Every level is read through a 2x2 box in its own texel
 * grid: a wide level is only a few dozen texels across, and a single bilinear sample of it shows
 * those texels as squares where the sum overlaps the frame. And the sum is divided by the number
 * of levels, so Intensity means the same thing whether two levels are summed or six - without it
 * adding a level brightened the whole halo.
 */

vec3 ReadLevel (vec2 uv, float lod, vec2 baseTexel)
{
	vec2 texel = baseTexel * exp2(lod) * 0.5;
	vec3 sum   = textureLod(s_glow, uv + vec2( texel.x,  texel.y), lod).rgb;
	sum       += textureLod(s_glow, uv + vec2(-texel.x,  texel.y), lod).rgb;
	sum       += textureLod(s_glow, uv + vec2( texel.x, -texel.y), lod).rgb;
	sum       += textureLod(s_glow, uv + vec2(-texel.x, -texel.y), lod).rgb;

	return sum * 0.25;
}

void main()
{
	vec2 uv   = v_texture.xy;
	vec2 texel = bloom.composite.zw;

	vec3 glow = ReadLevel(uv, 0.0, texel) * bloom.weightsA.x;
	glow += ReadLevel(uv, 1.0, texel) * bloom.weightsA.y;
	glow += ReadLevel(uv, 2.0, texel) * bloom.weightsA.z;
	glow += ReadLevel(uv, 3.0, texel) * bloom.weightsA.w;
	glow += ReadLevel(uv, 4.0, texel) * bloom.weightsB.x;
	glow += ReadLevel(uv, 5.0, texel) * bloom.weightsB.y;

	fragColor = vec4(glow * bloom.composite.x * bloom.composite.y, 1.0);
}
	-->
	</source>
</shader>
