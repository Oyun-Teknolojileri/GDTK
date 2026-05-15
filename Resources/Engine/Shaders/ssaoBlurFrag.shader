<shader>
	<type name = "fragmentShader" />
	<include name = "vulkanCompatInc.shader" />
	<include name = "ssaoBlurPassDataInc.shader" />
	<texture slot = "0" name = "s_texture0" />
	<source>
	<!--

precision highp float;

out vec4 fragColor;
TK_LOC(2) in vec2 v_texture;

TK_SAMPLER_BINDING(0) uniform sampler2D s_texture0; // SSAO texture (must have bilinear filtering)

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
	// Grouped into 2x2 blocks with bilinear offsets:
	// Corner 2x2 blocks (weight sum = 1+4+4+16 = 25 each)
	// Edge 2x1 blocks   (weight sum = 6+24 = 30 each)
	// Center             (weight = 36)

	// Bilinear offsets: sample at weighted center of each 2x2 block.
	// For corner blocks: offset = 1 + 16/(4+16) = 1.8, but simpler:
	// offset = 1.0 + (weight_far / (weight_near + weight_far))
	float o = 1.0 + (4.0 / 20.0); // = 1.2 texels from center for corners
	float e = 1.0 + (6.0 / 30.0); // = 1.2 texels for edge pairs

	vec2 uv = v_texture;

	// 4 corner samples (each approximates a 2x2 block)
	float tl = texture(s_texture0, uv + ssaoBlur.texelSizeAndPad.xy * vec2(-o, -o)).r;
	float tr = texture(s_texture0, uv + ssaoBlur.texelSizeAndPad.xy * vec2( o, -o)).r;
	float bl = texture(s_texture0, uv + ssaoBlur.texelSizeAndPad.xy * vec2(-o,  o)).r;
	float br = texture(s_texture0, uv + ssaoBlur.texelSizeAndPad.xy * vec2( o,  o)).r;

	// 4 edge samples (each approximates a 2x1 block)
	float t  = texture(s_texture0, uv + ssaoBlur.texelSizeAndPad.xy * vec2( 0.0, -e)).r;
	float b  = texture(s_texture0, uv + ssaoBlur.texelSizeAndPad.xy * vec2( 0.0,  e)).r;
	float l  = texture(s_texture0, uv + ssaoBlur.texelSizeAndPad.xy * vec2(-e,  0.0)).r;
	float r  = texture(s_texture0, uv + ssaoBlur.texelSizeAndPad.xy * vec2( e,  0.0)).r;

	// Center sample
	float c  = texture(s_texture0, uv).r;

	// Weighted sum approximating 5x5 Gaussian
	// Corners: 25/256 each, Edges: 30/256 each, Center: 36/256
	float result = (tl + tr + bl + br) * (25.0 / 256.0)
	             + (t + b + l + r)     * (30.0 / 256.0)
	             + c                   * (36.0 / 256.0);

	fragColor = vec4(result, 0.0, 0.0, 1.0);
}

	-->
	</source>
</shader>
