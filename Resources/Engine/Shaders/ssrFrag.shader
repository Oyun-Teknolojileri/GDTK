<shader>
	<type name = "fragmentShader" />
	<include name = "vulkanCompatInc.shader" />
	<include name = "normalEncodingInc.shader" />
	<include name = "ssrPassDataInc.shader" />
	<texture slot = "0" name = "s_diffuseColor" />
	<texture slot = "1" name = "s_normalDepth" />
	<define name = "SSR_STEP_COUNT" val = "8,16,32,64" />
	<source>
	<!--

precision highp float;
precision lowp int;

TK_LOC(2) in vec2 v_texture;

layout (location = 0) out vec4 fragColor;

TK_SAMPLER_BINDING(0) uniform sampler2D s_diffuseColor; // Scene color, HDR and pre tonemap.
TK_SAMPLER_BINDING(1) uniform sampler2D s_normalDepth;  // rg: world normal, b: linear depth, a: roughness.

// View space position of a pixel from its positive linear view depth. Mirrors the reconstruction
// SSAOPass does, Vulkan's flipped texture v included.
vec3 ReconstructViewPos(vec2 uv, float linearDepth)
{
	vec2 ndc = uv * 2.0 - 1.0;
#ifdef VULKAN
	ndc.y = -ndc.y;
#endif
	vec4 viewPos = ssrPass.inverseProjection * vec4(ndc, 0.0, 1.0);
	vec3 viewDir = viewPos.xyz / viewPos.w;
	return viewDir * (linearDepth / -viewDir.z);
}

// Project a view space position to a scene UV. False when it can not be sampled: behind the camera
// or outside the screen. Same projection parameter trick SSAOPass uses.
bool ProjectToUV(vec3 viewPos, out vec2 uv)
{
	if (viewPos.z >= 0.0)
	{
		uv = vec2(0.0);
		return false;
	}

	float invW = -1.0 / viewPos.z;
	uv = vec2(ssrPass.projParams.x * viewPos.x + ssrPass.projParams.z * viewPos.z,
	          ssrPass.projParams.y * viewPos.y + ssrPass.projParams.w * viewPos.z) * invW * 0.5 + 0.5;
#ifdef VULKAN
	uv.y = 1.0 - uv.y;
#endif
	return uv.x >= 0.0 && uv.x <= 1.0 && uv.y >= 0.0 && uv.y <= 1.0;
}

// Positive linear view depth the g buffer holds for a scene UV. Zero means background (sky).
float SceneLinearDepth(vec2 uv)
{
	return texture(s_normalDepth, uv).b;
}

// Geometric normal of the surface at `uv`, rebuilt from the depth buffer instead of read from the
// g buffer. A shading normal can lean away from the geometry it sits on (normal maps, grazing or
// thin surfaces) and a reflection ray has to avoid the geometry, not the shading.
vec3 GeometricNormal(vec2 uv, float linearDepth)
{
	vec2 texel = ssrPass.screenParams.xy;

	float left   = SceneLinearDepth(uv - vec2(texel.x, 0.0));
	float right  = SceneLinearDepth(uv + vec2(texel.x, 0.0));
	float bottom = SceneLinearDepth(uv - vec2(0.0, texel.y));
	float top    = SceneLinearDepth(uv + vec2(0.0, texel.y));

	// Neighbours that landed on the background keep the centre depth, so they can not bend the plane.
	left   = left   > 0.0 ? left   : linearDepth;
	right  = right  > 0.0 ? right  : linearDepth;
	bottom = bottom > 0.0 ? bottom : linearDepth;
	top    = top    > 0.0 ? top    : linearDepth;

	vec3 dx = ReconstructViewPos(uv + vec2(texel.x, 0.0), right) - ReconstructViewPos(uv - vec2(texel.x, 0.0), left);
	vec3 dy = ReconstructViewPos(uv + vec2(0.0, texel.y), top) - ReconstructViewPos(uv - vec2(0.0, texel.y), bottom);

	vec3 normal = normalize(cross(dx, dy));

	// View space +z points back at the eye, so a camera facing normal has a positive z. The screen
	// coordinate flip of the backend decides the winding of the cross product, this removes it.
	return normal.z < 0.0 ? -normal : normal;
}

// Cheap per pixel value in [0, 1), the same interleaved gradient noise SSAOPass uses.
float InterleavedGradientNoise(vec2 pixel)
{
	return fract(52.9829189 * fract(dot(pixel, vec2(0.06711056, 0.00583715))));
}

// Fades the reflection out towards the screen border, where the depth buffer stops holding the
// geometry the ray would need.
float EdgeFade(vec2 uv)
{
	vec2 margin = ssrPass.screenParams.xy * (ssrPass.screenParams.z + ssrPass.screenParams.w) * 0.05;
	vec2 dist   = min(uv, vec2(1.0) - uv);
	return smoothstep(0.0, margin.x, dist.x) * smoothstep(0.0, margin.y, dist.y);
}

// A handful of taps around the hit instead of a single bilinear fetch: what is left of the step
// quantization turns into a soft gradient and rougher surfaces get a wider gather.
vec3 SampleReflection(vec2 uv, float roughness)
{
	vec2 texel   = ssrPass.screenParams.xy;
	float radius = 0.75 + roughness * 4.0;

	vec3 sum = texture(s_diffuseColor, uv).rgb;
	sum += texture(s_diffuseColor, uv + vec2(radius, radius * 0.5) * texel).rgb;
	sum += texture(s_diffuseColor, uv + vec2(-radius * 0.5, radius) * texel).rgb;
	sum += texture(s_diffuseColor, uv + vec2(-radius, -radius * 0.5) * texel).rgb;
	sum += texture(s_diffuseColor, uv + vec2(radius * 0.5, -radius) * texel).rgb;

	return sum * 0.2;
}

void main()
{
	vec2 uv      = v_texture;
	vec4 base    = texture(s_diffuseColor, uv);
	vec4 gBuffer = texture(s_normalDepth, uv);

	float linearDepth = gBuffer.b;
	float roughness   = gBuffer.a;

	// No geometry here (sky, background) or a surface too rough to mirror anything: keep the
	// forward shaded color, which already carries the sky / environment volume reflection. This is
	// the fallback the whole effect leans on, so it stays untouched instead of being darkened.
	if (linearDepth <= 0.0 || roughness >= ssrPass.params.w)
	{
		fragColor = base;
		return;
	}

	vec3 viewPos   = ReconstructViewPos(uv, linearDepth);
	vec3 normal    = normalize(mat3(ssrPass.view) * decodeNormal(gBuffer.rg));
	vec3 geoNormal = GeometricNormal(uv, linearDepth);
	vec3 rayDir    = reflect(normalize(viewPos), normal);

	// Push the ray start above the reconstructed surface. The further the shading normal leans away
	// from it, the bigger the push has to be, which is what keeps bumpy surfaces from self reflecting.
	float divergence = 1.0 - pow(clamp(dot(normal, geoNormal), 0.0, 1.0), 8.0);
	viewPos += geoNormal * (0.01 + 0.02 * linearDepth) * divergence;

	// A ray that immediately points into the surface would self intersect: bounce it once.
	if (dot(rayDir, geoNormal) < 0.0)
	{
		rayDir = normalize(reflect(rayDir, geoNormal));
	}

	// Rays pointing back at the camera can not hit anything in front of the depth buffer.
	if (rayDir.z > 0.0)
	{
		fragColor = base;
		return;
	}

	// Per pixel jitter of the first step. The marching error becomes noise that the gather below
	// averages out, instead of a stable banding pattern along the surface.
	float jitter     = InterleavedGradientNoise(gl_FragCoord.xy);

	float stepLength = ssrPass.params.y / float(SSR_STEP_COUNT);
	vec3 rayPos      = viewPos + rayDir * stepLength * jitter;
	vec3 prevPos     = rayPos;
	vec2 hitUV       = vec2(0.0);
	float confidence = 0.0;

	for (int i = 0; i < SSR_STEP_COUNT; ++i)
	{
		prevPos = rayPos;
		rayPos += rayDir * stepLength;

		vec2 rayUV;
		if (!ProjectToUV(rayPos, rayUV))
		{
			break;
		}

		float sceneDepth = SceneLinearDepth(rayUV);

		// Positive delta means the ray walked behind the geometry at that pixel, and staying inside
		// the thickness keeps it from latching onto something far behind. Walking past a gap in the
		// geometry is not a failure, so the march keeps going.
		float delta = -rayPos.z - sceneDepth;
		if (sceneDepth <= 0.0 || delta <= 0.0 || delta >= ssrPass.params.z)
		{
			continue;
		}

		// Refine the crossing between prevPos and rayPos.
		vec3 lo = prevPos;
		vec3 hi = rayPos;
		for (int j = 0; j < 6; ++j)
		{
			vec3 mid = (lo + hi) * 0.5;
			vec2 midUV;
			if (!ProjectToUV(mid, midUV))
			{
				break;
			}

			if (-mid.z - SceneLinearDepth(midUV) > 0.0)
			{
				hi = mid;
			}
			else
			{
				lo = mid;
			}
		}

		vec2 refinedUV;
		if (!ProjectToUV(hi, refinedUV))
		{
			continue;
		}

		float hitDepth = SceneLinearDepth(refinedUV);
		if (hitDepth <= 0.0)
		{
			continue;
		}

		// Reject the hits the ray walked through. When the ray barely moves across the screen it is
		// almost parallel to the view direction, and a surface whose normal agrees with the ray is a
		// backface: accepting it would paint the reflection onto the inside of the geometry.
		vec3 hitNormal = normalize(mat3(ssrPass.view) * decodeNormal(texture(s_normalDepth, refinedUV).rg));
		bool frontal   = all(lessThan(abs(refinedUV - uv), ssrPass.screenParams.xy * 4.0));
		if (frontal && dot(rayDir, hitNormal) >= 0.0)
		{
			continue;
		}

		// Trust the hit by how close the ray actually got to the surface it crossed: a grazing
		// crossing touches the reconstructed surface far away and fades out instead of popping.
		vec3 hitPos = ReconstructViewPos(refinedUV, hitDepth);
		float miss  = length(hi - hitPos);
		float conf  = 1.0 - smoothstep(0.0, ssrPass.params.z, miss);
		conf       *= conf;

		if (conf <= 0.0)
		{
			continue;
		}

		hitUV      = refinedUV;
		confidence = conf;
		break;
	}

	if (confidence <= 0.0)
	{
		// No screen space reflection found: sky or the active environment volumes keep lighting the
		// pixel through the forward pass result.
		fragColor = base;
		return;
	}

	vec3 reflection = SampleReflection(hitUV, roughness);

	// Gloss, hit confidence, the screen border and the ray length all fade the reflection out, so
	// the forward shaded environment reflection takes over exactly where this one is not
	// trustworthy. The length fade is in pixels: a long ray is both less accurate and more likely to
	// have run out of buffer.
	float gloss     = 1.0 - roughness / max(ssrPass.params.w, 0.0001);
	float rayPixels = length((hitUV - uv) * ssrPass.screenParams.zw);
	float fade      = 1.0 - smoothstep(0.0, max(ssrPass.screenParams.z, ssrPass.screenParams.w) * 0.5, rayPixels);
	float weight    = clamp(ssrPass.params.x * gloss * confidence * EdgeFade(hitUV) * fade, 0.0, 1.0);

	fragColor = vec4(mix(base.rgb, reflection, weight), base.a);
}

	-->
	</source>
</shader>
