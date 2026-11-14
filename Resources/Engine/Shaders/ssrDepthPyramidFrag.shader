<shader>
	<type name = "fragmentShader" />
	<include name = "vulkanCompatInc.shader" />
	<include name = "ssrCommonInc.shader" />
	<texture slot = "0" name = "s_pyramidDepth" />
	<source>
	<!--

precision highp float;
precision highp int;

TK_LOC(2) in vec2 v_texture;

layout (location = 0) out vec4 fragColor;

TK_SAMPLER_BINDING(0) uniform sampler2D s_pyramidDepth; // Level below: g buffer (b), or the previous level.

// Depth of a pixel with no geometry. A tile that holds any geometry has to report the nearest surface, and
// a tile of pure background must not read as a hole the ray can pass through: background is the far plane,
// not zero.
#define SSR_PYRAMID_FAR 1.0e6

// One level of the min depth hierarchy the ray march climbs: each texel is the nearest surface any of the
// four texels below it holds. The march uses it to skip a whole tile at once while nothing is in front of
// it, which is what lets a ray cover the screen in a handful of steps instead of dozens.
//
// The value is written to every channel so the next level reads it with the same `.b` the g buffer uses:
// one shader serves the first level and all the ones after it.
float DepthAt(vec2 uv)
{
	float d = texture(s_pyramidDepth, uv).b;
	return d > 0.0 ? d : SSR_PYRAMID_FAR;
}

void main()
{
	vec2 sourceSize = vec2(textureSize(s_pyramidDepth, 0));
	vec2 texel      = 0.5 / sourceSize;

	float d0 = DepthAt(v_texture + vec2(-texel.x, -texel.y));
	float d1 = DepthAt(v_texture + vec2( texel.x, -texel.y));
	float d2 = DepthAt(v_texture + vec2(-texel.x,  texel.y));
	float d3 = DepthAt(v_texture + vec2( texel.x,  texel.y));

	float depth = min(min(d0, d1), min(d2, d3));
	fragColor   = vec4(depth, depth, depth, 1.0);
}

	-->
	</source>
</shader>
