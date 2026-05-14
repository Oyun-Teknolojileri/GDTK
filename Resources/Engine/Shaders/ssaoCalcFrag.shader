<shader>
	<type name = "fragmentShader" />
	<include name = "vulkanCompatInc.shader" />
	<include name = "normalEncodingInc.shader" />
	<include name = "ssaoCalcPassDataInc.shader" />
	<texture slot = "1" name = "s_texture1" />
	<define name = "KERNEL_SIZE" val = "16,8,32" />
	<source>
	<!--
#version 300 es
precision highp float;

out vec4 fragColor;

in vec3 v_pos;
in vec3 v_normal;
in vec2 v_texture;

TK_SAMPLER_BINDING(1) uniform sampler2D s_texture1; // packed normal (RG) + linear depth (B)

vec3 reconstructViewPos(vec2 uv, float linearDepth)
{
	vec2 ndc = uv * 2.0 - 1.0;
#ifdef VULKAN
	// fullQuadVert flips v_texture.y for Vulkan's top-down tex coords, but the projection
	// matrix is GL-style (positive y_view → positive NDC.y), so map the flipped UV back to
	// the projection's NDC convention before inverse-projecting.
	ndc.y = -ndc.y;
#endif
	vec4 clipPos = vec4(ndc, 0.0, 1.0);
	vec4 viewPos = ssaoCalc.inverseProjection * clipPos;
	vec3 viewDir = viewPos.xyz / viewPos.w;
	// viewDir.z is negative in view space, linearDepth is positive
	return viewDir * (linearDepth / -viewDir.z);
}

void main()
{
	vec2 texCoord = v_texture;
	vec4 normalDepthData = texture(s_texture1, texCoord);

	vec3 normal = decodeNormal(normalDepthData.rg);
	float linearDepth = normalDepthData.b;
	vec3 fragPos = reconstructViewPos(texCoord, linearDepth);

	normal = normalize(mat3(ssaoCalc.normalToView) * normal); // World to View

	// Interleaved Gradient Noise for per-pixel random rotation (no texture needed)
	float noise = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
	float angle = noise * 6.283185;
	float cosA = cos(angle);
	float sinA = sin(angle);

	// Build rotated TBN from normal and noise angle
	vec3 t = normalize(cross(normal, abs(normal.y) > 0.99 ? vec3(1.0, 0.0, 0.0) : vec3(0.0, 1.0, 0.0)));
	vec3 b = cross(normal, t);
	vec3 tangent = t * cosA + b * sinA;
	vec3 bitangent = -t * sinA + b * cosA;
	mat3 TBN = mat3(tangent, bitangent, normal);

	float radius = ssaoCalc.radiusBiasAndPad.x;
	float bias = ssaoCalc.radiusBiasAndPad.y;

	// depth-scaled bias: surfaces further away need more bias to avoid acne
	float depthBias = bias * (1.0 + abs(fragPos.z) * 0.1);

	// iterate over the sample kernel and calculate occlusion factor
	float occlusion = 0.0;
	for(int i = 0; i < KERNEL_SIZE; ++i)
	{
		// get sample position
		vec3 samplePos = TBN * ssaoCalc.samples[i].xyz; // from tangent to view-space
		samplePos = fragPos + samplePos * radius;

		// project sample position to UV using precomputed projection params
		// clip.x = P00*x + P20*z, clip.y = P11*y + P21*z, clip.w = -z
		float invW = -1.0 / samplePos.z;
		vec2 sampleUV = vec2(ssaoCalc.projParams.x * samplePos.x + ssaoCalc.projParams.z * samplePos.z,
		                     ssaoCalc.projParams.y * samplePos.y + ssaoCalc.projParams.w * samplePos.z) * invW * 0.5 + 0.5;
#ifdef VULKAN
		// Mirror of the reconstruction flip: NDC.y → tex-space UV.y must invert to match the
		// flipped v_texture used to sample s_texture1 (top-of-screen geometry was rendered to
		// memory v=0 under Vulkan's tex coord convention).
		sampleUV.y = 1.0 - sampleUV.y;
#endif

		// get sample depth
		float sampleDepth = -texture(s_texture1, sampleUV).b; // get linear depth and negate to match view space z

		// range check & accumulate with smooth falloff instead of hard binary test
		float rangeCheck = smoothstep(0.0, 1.0, radius / abs(fragPos.z - sampleDepth));
		float diff = sampleDepth - samplePos.z;
		occlusion += smoothstep(0.0, depthBias, diff) * rangeCheck;
	}
	occlusion = max(1.0 - (occlusion / float(KERNEL_SIZE)), 0.0);
	fragColor = vec4(occlusion, 0.0, 0.0, 1.0);
}

	-->
	</source>
</shader>