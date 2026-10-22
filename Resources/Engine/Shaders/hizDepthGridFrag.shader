<shader>
	<type name = "fragmentShader" />
	<include name = "vulkanCompatInc.shader" />
	<texture slot = "0" name = "s_depth" />
	<define name = "HiZBlockSize" val = "8" />
	<source>
	<!--

precision highp float;
precision highp int;

out vec4 fragColor;

TK_SAMPLER_BINDING(0) uniform sampler2D s_depth;

/*
 * One texel per block of the depth buffer, holding the farthest depth in that block.
 *
 * The farthest depth is what an occlusion test needs: a light is hidden only when everything the block
 * covers is in front of it, so the block has to answer with its deepest sample. The block size is a
 * compile time define and the source size comes from the texture, so no uniforms are involved and the
 * shader works at any resolution. Half floats keep precision to about a ten thousandth of the depth
 * range, which is all a coarse test can use anyway.
 */
void main()
{
	ivec2 srcSize = textureSize(s_depth, 0);
	ivec2 base    = ivec2(gl_FragCoord.xy) * HiZBlockSize;
	ivec2 last    = srcSize - ivec2(1);

	float farthest = 0.0;
	for (int y = 0; y < HiZBlockSize; ++y)
	{
		for (int x = 0; x < HiZBlockSize; ++x)
		{
			ivec2 p = min(base + ivec2(x, y), last);
			farthest = max(farthest, texelFetch(s_depth, p, 0).r);
		}
	}

	fragColor = vec4(farthest, 0.0, 0.0, 1.0);
}
	-->
	</source>
</shader>
