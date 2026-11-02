<shader>
	<type name = "fragmentShader" />
	<include name = "vulkanCompatInc.shader" />
	<include name = "ssaoBlurPassDataInc.shader" />
	<texture slot = "0" name = "s_diffuseColor" />
	<texture slot = "2" name = "s_normalDepth" />
	<source>
	<!--

precision highp float;

out vec4 fragColor;
TK_LOC(2) in vec2 v_texture;

TK_SAMPLER_BINDING(0) uniform sampler2D s_diffuseColor; // SSAO texture (must have bilinear filtering)
TK_SAMPLER_BINDING(2) uniform sampler2D s_normalDepth;  // packed normal (RG) + linear depth (B)

// How fast a tap stops counting as the same surface, per world unit of depth difference. The depth
// in the buffer is in the scene's own units, so a quarter of a unit halves a tap's weight.
const float DepthFalloff = 8.0;

void main()
{
	// Bilinear 5x5 Gaussian approximation using 9 texture fetches.
	// Each off-center fetch samples between 4 texels, leveraging
	// hardware bilinear interpolation.
	//
	// 5x5 Gaussian weights:
	// [1  4  6  4  1]
	// [4 16 24 16  4]
	// [6 24 36 24  6]
	// [4 16 24 16  4]
	// [1  4  6  4  1]  / 256
	//
	// Grouped into 2x2 blocks with bilinear offsets: corner blocks weigh 25, edge blocks 30, the
	// centre 36.
	float o = 1.0 + (4.0 / 20.0);
	float e = 1.0 + (6.0 / 30.0);

	vec2 uv        = v_texture;
	vec2 texelSize = ssaoBlur.texelSizeAndPad.xy;

	vec2 offsets[9] = vec2[9](vec2(-o, -o), vec2(o, -o), vec2(-o, o), vec2(o, o),
	                          vec2(0.0, -e), vec2(0.0, e), vec2(-e, 0.0), vec2(e, 0.0),
	                          vec2(0.0, 0.0));
	float weights[9] = float[9](25.0, 25.0, 25.0, 25.0, 30.0, 30.0, 30.0, 30.0, 36.0);

	// Bilateral: every tap is weighted by how close its depth is to the centre's. A plain Gaussian
	// over the same taps averages the hemisphere noise together with the depth edges it sits on, so
	// the result reads as a soft blur that has to be kept small to stay honest; weighting by depth
	// lets the kernel average harder across a surface without crossing onto the thing behind it.
	float centerDepth = texture(s_normalDepth, uv).b;

	float accumulated = 0.0;
	float weightSum   = 0.0;

	for (int i = 0; i < 9; ++i)
	{
		vec2 tapUv   = uv + texelSize * offsets[i];
		float depth  = texture(s_normalDepth, tapUv).b;
		float weight = weights[i] * exp2(-abs(depth - centerDepth) * DepthFalloff);

		accumulated += texture(s_diffuseColor, tapUv).r * weight;
		weightSum   += weight;
	}

	fragColor = vec4(accumulated / max(weightSum, 1e-4), 0.0, 0.0, 1.0);
}

	-->
	</source>
</shader>
