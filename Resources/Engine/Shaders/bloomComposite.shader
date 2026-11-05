<shader>
	<type name = "fragmentShader" />
	<include name = "vulkanCompatInc.shader" />
	<include name = "bloomPassDataInc.shader" />
	<texture slot = "0" name = "s_glow" />
	<define name = "BLOOM_BICUBIC" val="0,1" />
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
 * one a box average of the level below. Sampling level i at the frame's resolution gives the halo
 * at 2^i the glow resolution - narrow levels keep the highlight tight to what emits it, wide
 * levels carry the spread - so the weighted sum of the levels IS the bloom, and there is no
 * upsample chain to run and no per level target to read back.
 *
 * The weights are the radius: flat across the levels the iteration count selects is the widest
 * halo that count allows, and a Gaussian falloff over the same levels tightens it without the
 * reach moving. They are normalized, so the shape of the halo changes and its brightness does not.
 *
 * A wide level is only a few dozen texels across, and reading it with a single bilinear sample
 * shows those texels as squares where the sum overlaps the frame. Two things prevent that. Every
 * fetch is a 2x2 box inside the level, and BLOOM_BICUBIC widens that to the B-spline reconstruction
 * below, four bilinear taps through the cubic basis - the same filter Godot calls its bicubic
 * upscale. Off keeps the box, which is cheaper on hardware where the extra arithmetic shows.
 */

#if BLOOM_BICUBIC

// The four cubic B-spline basis functions. They sum to one for any fraction, so the reconstructed
// value needs no normalisation. Same form as Godot's bicubic_filter_inc.glsl.
float CubicW0 (float a) { return (1.0 / 6.0) * (a * (a * (-a + 3.0) - 3.0) + 1.0); }
float CubicW1 (float a) { return (1.0 / 6.0) * (a * a * (3.0 * a - 6.0) + 4.0); }
float CubicW2 (float a) { return (1.0 / 6.0) * (a * (a * (-3.0 * a + 3.0) + 3.0) + 1.0); }
float CubicW3 (float a) { return (1.0 / 6.0) * (a * a * a); }

// Amplitude of each of the two contributing taps, and the offset that puts the tap on a texel
// centre so the pair straddles the one being reconstructed.
float CubicG0 (float a) { return CubicW0(a) + CubicW1(a); }
float CubicG1 (float a) { return CubicW2(a) + CubicW3(a); }
float CubicH0 (float a) { return -1.0 + CubicW1(a) / (CubicW0(a) + CubicW1(a)); }
float CubicH1 (float a) { return 1.0 + CubicW3(a) / (CubicW2(a) + CubicW3(a)); }

vec3 ReadLevel (vec2 uv, float lod, vec2 baseTexel)
{
	// baseTexel is level zero's texel size, and a level is half the one below it.
	vec2 texelSize = baseTexel * exp2(lod);

	vec2 p    = uv / texelSize + 0.5;
	vec2 base = floor(p);
	vec2 frac = p - base;

	float g0x = CubicG0(frac.x);
	float g1x = CubicG1(frac.x);
	float h0x = CubicH0(frac.x);
	float h1x = CubicH1(frac.x);
	float h0y = CubicH0(frac.y);
	float h1y = CubicH1(frac.y);

	vec2 t0 = (base + vec2(h0x, h0y) - 0.5) * texelSize;
	vec2 t1 = (base + vec2(h1x, h0y) - 0.5) * texelSize;
	vec2 t2 = (base + vec2(h0x, h1y) - 0.5) * texelSize;
	vec2 t3 = (base + vec2(h1x, h1y) - 0.5) * texelSize;

	vec3 top    = g0x * textureLod(s_glow, t0, lod).rgb + g1x * textureLod(s_glow, t1, lod).rgb;
	vec3 bottom = g0x * textureLod(s_glow, t2, lod).rgb + g1x * textureLod(s_glow, t3, lod).rgb;

	return CubicG0(frac.y) * top + CubicG1(frac.y) * bottom;
}

#else

vec3 ReadLevel (vec2 uv, float lod, vec2 baseTexel)
{
	vec2 texel = baseTexel * exp2(lod) * 0.5;
	vec3 sum   = textureLod(s_glow, uv + vec2( texel.x,  texel.y), lod).rgb;
	sum       += textureLod(s_glow, uv + vec2(-texel.x,  texel.y), lod).rgb;
	sum       += textureLod(s_glow, uv + vec2( texel.x, -texel.y), lod).rgb;
	sum       += textureLod(s_glow, uv + vec2(-texel.x, -texel.y), lod).rgb;

	return sum * 0.25;
}

#endif // BLOOM_BICUBIC

void main()
{
	vec2 uv    = v_texture.xy;
	vec2 texel = bloom.composite.zw;

	vec3 glow = vec3(0.0);

	// A level the radius leaves at zero is not fetched at all. Both the reach and the radius leave
	// most of the pyramid at zero in the usual settings, and the filter costs four fetches a level.
	if (bloom.weightsA.x > 0.001) { glow += ReadLevel(uv, 0.0, texel) * bloom.weightsA.x; }
	if (bloom.weightsA.y > 0.001) { glow += ReadLevel(uv, 1.0, texel) * bloom.weightsA.y; }
	if (bloom.weightsA.z > 0.001) { glow += ReadLevel(uv, 2.0, texel) * bloom.weightsA.z; }
	if (bloom.weightsA.w > 0.001) { glow += ReadLevel(uv, 3.0, texel) * bloom.weightsA.w; }
	if (bloom.weightsB.x > 0.001) { glow += ReadLevel(uv, 4.0, texel) * bloom.weightsB.x; }
	if (bloom.weightsB.y > 0.001) { glow += ReadLevel(uv, 5.0, texel) * bloom.weightsB.y; }

	fragColor = vec4(glow * bloom.composite.x * bloom.composite.y, 1.0);
}
	-->
	</source>
</shader>
