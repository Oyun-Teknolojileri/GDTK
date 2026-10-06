<shader>
	<type name = "fragmentShader" />
	<include name = "vulkanCompatInc.shader" />
	<texture slot = "0" name = "s_normalDepth" />
	<source>
	<!--

precision highp float;
precision lowp int;

TK_LOC(2) in vec2 v_texture;

layout (location = 0) out vec4 fragColor;

TK_SAMPLER_BINDING(0) uniform sampler2D s_normalDepth; // b: positive linear view depth, 0 on the background.

// A level of the depth pyramid covers HIZ_BLOCK by HIZ_BLOCK tiles of the level below, so level n
// covers HIZ_BLOCK ^ n pixels of the screen.
#define HIZ_BLOCK 4

// A background texel holds no surface. It is left out of the range instead of being folded into it:
// folding it in as the far end would make every tile the sky touches look like it reaches to
// infinity, and folding it in as zero would make every tile the sky touches look occupied. Only
// when a tile holds no surface at all does the whole range report "nothing here".
#define HIZ_NO_SURFACE 100000.0

// Nearest and farthest surface depth of the tile this fragment covers, in .x and .y. The pair is the
// depth window of everything inside the tile, so a ray whose own depth window does not meet it can
// not cross anything here and can step over the whole tile.
void main()
{
	ivec2 srcSize  = textureSize(s_normalDepth, 0);
	ivec2 tile     = ivec2(floor(v_texture * vec2(srcSize) / float(HIZ_BLOCK))) * HIZ_BLOCK;

	// texelFetch outside the texture is undefined and the right and bottom tiles of a size that is
	// not a multiple of HIZ_BLOCK reach past the g buffer, so the lookup is clamped.
	ivec2 maxTexel = srcSize - ivec2(1);

	float nearest  = HIZ_NO_SURFACE;
	float farthest = 0.0;

	for (int y = 0; y < HIZ_BLOCK; ++y)
	{
		for (int x = 0; x < HIZ_BLOCK; ++x)
		{
			ivec2 texel = clamp(tile + ivec2(x, y), ivec2(0), maxTexel);
			float depth = texelFetch(s_normalDepth, texel, 0).b;

			if (depth > 0.0)
			{
				nearest  = min(nearest, depth);
				farthest = max(farthest, depth);
			}
		}
	}

	fragColor = vec4(nearest, farthest, 0.0, 1.0);
}
	-->
	</source>
</shader>
