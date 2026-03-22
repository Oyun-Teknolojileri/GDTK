<shader>
	<type name = "fragmentShader" />
	<include name = "normalEncodingInc.shader" />
	<source>
	<!--
#version 300 es
precision highp float;
		
out vec4 fragColor;

in vec3 v_pos;
in vec3 v_normal;
in vec2 v_texture;

uniform sampler2D s_texture1; // packed normal (RG) + linear depth (B)
uniform sampler2D s_texture2; // noise

uniform vec2 screenSize;
uniform mat4 viewMatrix;
uniform vec3 samples[128];
uniform mat4 projection;
uniform mat4 inverseProjection;
uniform float radius;
uniform float bias;
uniform int kernelSize;

vec3 reconstructViewPos(vec2 uv, float linearDepth)
{
	vec2 ndc = uv * 2.0 - 1.0;
	vec4 clipPos = vec4(ndc, 0.0, 1.0);
	vec4 viewPos = inverseProjection * clipPos;
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

	// tile noise texture over screen based on screen dimensions divided by noise size
	vec2 noiseScale = vec2(screenSize.x / 4.0, screenSize.y / 4.0); 
	mat3 invTrsView = (transpose(inverse(mat3(viewMatrix))));
	normal = normalize(invTrsView * normal); // World to View
	vec3 randomVec = vec3(texture(s_texture2, texCoord * noiseScale).xy, 0.0);
			
	// create TBN change-of-basis matrix: from tangent-space to view-space
	vec3 tangent = normalize(randomVec - normal * dot(randomVec, normal));
	vec3 bitangent = normalize(cross(normal, tangent));
	mat3 TBN = mat3(tangent, bitangent, normal);
			
	// iterate over the sample kernel and calculate occlusion factor
	float occlusion = 0.0;
	for(int i = 0; i < kernelSize; ++i)
	{
		// get sample position
		vec3 samplePos = TBN * samples[i]; // from tangent to view-space
		samplePos = fragPos + samplePos * radius; 
				
		// project sample position (to sample texture) (to get position on screen/texture)
		vec4 offset = vec4(samplePos, 1.0);
		offset = projection * offset; // from view to clip-space
		offset.xyz /= offset.w; // perspective divide
		offset.xyz = offset.xyz * 0.5 + 0.5; // transform to range 0.0 - 1.0
				
		// get sample depth
		float sampleDepth = -texture(s_texture1, offset.xy).b; // get linear depth and negate to match view space z
				
		// range check & accumulate
		float rangeCheck = smoothstep(0.0, 1.0, radius / abs(fragPos.z - sampleDepth));
		occlusion += (sampleDepth >= samplePos.z + bias ? 1.0 : 0.0) * rangeCheck;           
	}
	occlusion = max(1.0 - (occlusion / float(kernelSize)), 0.0);
	fragColor = vec4(occlusion, 0.0, 0.0, 1.0);
}

	-->
	</source>
</shader>