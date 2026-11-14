<shader>
	<type name = "fragmentShader" />
	<include name = "vulkanCompatInc.shader" />
	<texture slot = "0" name = "s_depth" />
	<define name = "DepthGridBlockSize" val = "8" />
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
 * shader works at any resolution.
 *
 * The source is the frame's depth attachment - the depth the projection encoded - read through the red
 * channel, and farther is the larger number, so the reduction is a max. A texel nothing was drawn on
 * already holds the far plane, because that is what the attachment is cleared to, so an empty texel needs
 * no special case: a block that is partly sky can never prove that a volume behind the sky is hidden.
 * Nothing here decodes the depth or compares it against anything; the CPU does that on the pixels this
 * pass wrote, and it needs them in the float range the target is allocated with, because an eight bit
 * channel on a scene with a far plane in the thousands puts both depths inside one quantization step.
 *
 * This is a single level reduction: there is no mip chain and nothing walks one, the grid it writes is
 * the coarsest level and the only one. The CPU tests the blocks an object covers one by one, so the
 * hierarchy a real Hi-Z would carry has nothing to answer here.
 */

void main()
{
	ivec2 srcSize = textureSize(s_depth, 0);
	ivec2 base    = ivec2(gl_FragCoord.xy) * DepthGridBlockSize;
	ivec2 last    = srcSize - ivec2(1);

	float farthest = 0.0;
	for (int y = 0; y < DepthGridBlockSize; ++y)
	{
		for (int x = 0; x < DepthGridBlockSize; ++x)
		{
			ivec2 p  = min(base + ivec2(x, y), last);
			farthest = max(farthest, texelFetch(s_depth, p, 0).r);
		}
	}

	fragColor = vec4(farthest, 0.0, 0.0, 1.0);
}
	-->
	</source>
</shader>
