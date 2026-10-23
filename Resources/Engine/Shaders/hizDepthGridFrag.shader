<shader>
	<type name = "fragmentShader" />
	<include name = "vulkanCompatInc.shader" />
	<texture slot = "0" name = "s_depth" />
	<define name = "HiZBlockSize" val = "8" />
	<define name = "HiZLinearDepth" val = "0,1" />
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
 *
 * HiZLinearDepth picks the source, and both answer the same question. 0 reads the red channel of the
 * frame's depth attachment, the depth the projection encoded. 1 reads the blue channel of the pre process
 * pass's G buffer, which holds the view space distance of the surface, and is preferred when that pass
 * runs because it is already single sample and needs no copy. Farther is the larger number on both, so the
 * reduction is a max either way.
 *
 * The two differ in what an empty texel means. A depth attachment is cleared to the far plane, so a texel
 * with nothing drawn on it already reads as the farthest. The G buffer is cleared to zero, and a view
 * space distance of zero only says that no geometry was written there, which is not a distance at all. It
 * is taken as further away than anything the frame can show: a block that is partly sky must not be able
 * to prove that a volume behind the sky is hidden.
 */
#if HiZLinearDepth
const float HiZEmptyDepth = 1.0e9;
#endif

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
#if HiZLinearDepth
			float depth = texelFetch(s_depth, p, 0).b;
			farthest    = max(farthest, depth > 0.0 ? depth : HiZEmptyDepth);
#else
			farthest = max(farthest, texelFetch(s_depth, p, 0).r);
#endif
		}
	}

	fragColor = vec4(farthest, 0.0, 0.0, 1.0);
}
	-->
	</source>
</shader>
