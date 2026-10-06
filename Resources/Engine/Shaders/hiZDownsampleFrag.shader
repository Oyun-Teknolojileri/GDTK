<shader>
	<type name = "fragmentShader" />
	<include name = "vulkanCompatInc.shader" />
	<texture slot = "0" name = "s_hiz" />
	<source>
	<!--

precision highp float;
precision lowp int;

TK_LOC(2) in vec2 v_texture;

layout (location = 0) out vec4 fragColor;

TK_SAMPLER_BINDING(0) uniform sampler2D s_hiz; // xy: nearest / farthest depth of the level below.

// One destination texel covers a HIZ_BLOCK by HIZ_BLOCK block of the level below, so the levels of
// the pyramid are HIZ_BLOCK times coarser than each other. See hiZDepthFrag.shader for the meaning
// of the pair and of the background.
#define HIZ_BLOCK 4
#define HIZ_NO_SURFACE 100000.0

// min of the nearests, max of the farthests. A tile that holds nothing inherits the empty pair from
// the tile below it.
void main()
{
	ivec2 srcSize  = textureSize(s_hiz, 0);
	ivec2 tile     = ivec2(floor(v_texture * vec2(srcSize) / float(HIZ_BLOCK))) * HIZ_BLOCK;
	ivec2 maxTexel = srcSize - ivec2(1);

	float nearest  = HIZ_NO_SURFACE;
	float farthest = 0.0;

	for (int y = 0; y < HIZ_BLOCK; ++y)
	{
		for (int x = 0; x < HIZ_BLOCK; ++x)
		{
			ivec2 texel = clamp(tile + ivec2(x, y), ivec2(0), maxTexel);
			vec2 range  = texelFetch(s_hiz, texel, 0).xy;

			nearest  = min(nearest, range.x);
			farthest = max(farthest, range.y);
		}
	}

	fragColor = vec4(nearest, farthest, 0.0, 1.0);
}
	-->
	</source>
</shader>
