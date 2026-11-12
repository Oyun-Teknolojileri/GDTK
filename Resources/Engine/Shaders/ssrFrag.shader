<shader>
	<type name = "fragmentShader" />
	<include name = "vulkanCompatInc.shader" />
	<include name = "normalEncodingInc.shader" />
	<include name = "ssrCommonInc.shader" />
	<texture slot = "0" name = "s_diffuseColor" />
	<texture slot = "1" name = "s_normalDepth" />
	<texture slot = "2" name = "s_envSpecular" />
	<texture slot = "3" name = "s_pyramid0" />
	<texture slot = "4" name = "s_pyramid1" />
	<texture slot = "5" name = "s_pyramid2" />
	<texture slot = "6" name = "s_pyramid3" />
	<texture slot = "7" name = "s_pyramid4" />
	<texture slot = "8" name = "s_pyramid5" />
	<texture slot = "9" name = "s_blueNoise" />
	<define name = "SSR_STEP_COUNT" val = "8,16,32,64" />
	<source>
	<!--

precision highp float;
precision highp int;

TK_LOC(2) in vec2 v_texture;

// rg: the reflection radiance, a: coverage - one where this pixel is a reflective surface, zero where
// it is not. The reflection itself is complete: a ray that misses its screen space target falls back to
// the environment cubemap, so there is no such thing as a pixel the pass "knows nothing about".
layout (location = 0) out vec4 fragColor;

// xy: where the ray ended on screen, z: its view depth there, w: how far along the ray that was. The
// resolve reprojects the ray end for the parallax candidate, and a miss ends at the last position it
// reached rather than at zero: that is the direction the environment sample stands in for.
layout (location = 1) out vec4 fragHit;

TK_SAMPLER_BINDING(0) uniform sampler2D s_diffuseColor; // Scene color, HDR and pre tonemap.
TK_SAMPLER_BINDING(1) uniform sampler2D s_normalDepth;  // rg: world normal, b: linear depth, a: roughness.
TK_SAMPLER_BINDING(2) uniform samplerCube s_envSpecular; // Pre filtered sky radiance, roughness in the mips.

// The min depth hierarchy, level 0 half the screen and each level after it half of the one before. The
// march reads it to skip whole tiles at once; separate samplers because a fragment shader can not index
// a sampler array dynamically on the targets this ships to.
TK_SAMPLER_BINDING(3) uniform sampler2D s_pyramid0;
TK_SAMPLER_BINDING(4) uniform sampler2D s_pyramid1;
TK_SAMPLER_BINDING(5) uniform sampler2D s_pyramid2;
TK_SAMPLER_BINDING(6) uniform sampler2D s_pyramid3;
TK_SAMPLER_BINDING(7) uniform sampler2D s_pyramid4;
TK_SAMPLER_BINDING(8) uniform sampler2D s_pyramid5;

// Two blue noise values per pixel, one tile of 128x128 texels. The sampler below walks it with the
// frame index, so consecutive frames draw from different cells of the same tile instead of from a new
// random sequence each time.
TK_SAMPLER_BINDING(9) uniform sampler2D s_blueNoise;

// Levels the pyramid carries.
#define SSR_PYRAMID_MIP_COUNT 6

// Set to 0 to march every step at full resolution against the g buffer, which is what the pass did before
// the hierarchy existed. Kept as a switch because it is the only way to measure what the hierarchy buys at
// a given step count: with the step budget covering the whole screen the two are close, and below that the
// hierarchy is what lets a few steps still reach the far side of the frame.
#ifndef SSR_USE_PYRAMID
#define SSR_USE_PYRAMID 1
#endif

// Nearest surface the tile at `mip` holds under `uv`, or the far plane when the tile has no geometry.
float PyramidDepth(vec2 uv, int mip)
{
	if (mip <= 0)
	{
		return texture(s_pyramid0, uv).b;
	}
	if (mip == 1)
	{
		return texture(s_pyramid1, uv).b;
	}
	if (mip == 2)
	{
		return texture(s_pyramid2, uv).b;
	}
	if (mip == 3)
	{
		return texture(s_pyramid3, uv).b;
	}
	if (mip == 4)
	{
		return texture(s_pyramid4, uv).b;
	}
	return texture(s_pyramid5, uv).b;
}


// Positive linear view depth the g buffer holds for a scene UV. Zero means background (sky).
float SceneLinearDepth(vec2 uv)
{
	return texture(s_normalDepth, uv).b;
}

// Geometric normal of the surface at `uv`, rebuilt from the depth buffer instead of read from the
// g buffer. A shading normal can lean away from the geometry it sits on (normal maps, grazing or
// thin surfaces) and a reflection ray has to avoid the geometry, not the shading.
//
// `texelStep` is the baseline of the difference, in texels. The ray start wants one texel so the
// normal follows the surface it starts on. The plane a hit is refined onto wants a wider baseline:
// over a single texel the stored depth quantization dominates the slope, and that noise moves the
// intersection from scanline to scanline.
vec3 GeometricNormal(vec2 uv, float linearDepth, float texelStep)
{
	vec2 texel = ssrPass.screenParams.xy * texelStep;

	float left   = SceneLinearDepth(uv - vec2(texel.x, 0.0));
	float right  = SceneLinearDepth(uv + vec2(texel.x, 0.0));
	float bottom = SceneLinearDepth(uv - vec2(0.0, texel.y));
	float top    = SceneLinearDepth(uv + vec2(0.0, texel.y));

	// Neighbours that landed on the background keep the centre depth, so they can not bend the plane.
	left   = left   > 0.0 ? left   : linearDepth;
	right  = right  > 0.0 ? right  : linearDepth;
	bottom = bottom > 0.0 ? bottom : linearDepth;
	top    = top    > 0.0 ? top    : linearDepth;

	vec3 dx = SsrViewPos(uv + vec2(texel.x, 0.0), right) - SsrViewPos(uv - vec2(texel.x, 0.0), left);
	vec3 dy = SsrViewPos(uv + vec2(0.0, texel.y), top) - SsrViewPos(uv - vec2(0.0, texel.y), bottom);

	vec3 normal = normalize(cross(dx, dy));

	// The surface the depth buffer holds faces the eye, so its normal has to point back along the view
	// ray to that surface. That is decided against the view direction of the centre pixel, not against
	// the normal's own z: a floor seen at a grazing angle has a z near zero, so the sign of a rounding
	// level value flips the normal from scanline to scanline. A flipped normal points the ray start
	// into the surface it just left, the march then compares the ray against the wrong side of the
	// geometry and whole scanlines drop their reflection.
	vec3 toSurface = SsrViewPos(uv, linearDepth);
	return dot(normal, toSurface) < 0.0 ? normal : -normal;
}

// Cheap per pixel value in [0, 1), the same interleaved gradient noise SSAOPass uses.
float InterleavedGradientNoise(vec2 pixel)
{
	return fract(52.9829189 * fract(dot(pixel, vec2(0.06711056, 0.00583715))));
}

// Two random numbers for this pixel and frame, from the blue noise tile.
//
// Blue noise is what keeps the samples of one frame evenly spread: an uncorrelated random sequence leaves
// clumps and holes at 128x128 scale, and at one sample per pixel those clumps are what the denoiser then
// has to average away. The tile is walked by the frame index with the R2 steps, so consecutive frames draw
// from cells far apart in the tile while each frame stays spatially uniform.
vec2 SampleSequence(vec2 pixel, float frame)
{
	vec2 offset = fract(frame * vec2(0.7548776662, 0.5698402910));
	return texture(s_blueNoise, (pixel + offset * 128.0) / 128.0).rg;
}

// Visible normal distribution sampling (Heitz 2018). Returns the half vector of a microfacet that
// reflects `Ve` (unit vector towards the eye, tangent space with N = +z) for the given roughness.
vec3 SampleGgxVndf(vec3 Ve, float alpha, vec2 Xi)
{
	vec3 Vh = normalize(vec3(alpha * Ve.x, alpha * Ve.y, Ve.z));

	float lensq = Vh.x * Vh.x + Vh.y * Vh.y;
	vec3 T1 = lensq > 0.0 ? vec3(-Vh.y, Vh.x, 0.0) * inversesqrt(lensq) : vec3(1.0, 0.0, 0.0);
	vec3 T2 = cross(Vh, T1);

	float r = sqrt(Xi.x);
	float phi = 2.0 * SSR_PI * Xi.y;
	float t1 = r * cos(phi);
	float t2 = r * sin(phi);

	// The distribution is stretched towards the horizon by the visible normal term.
	float s = 0.5 * (1.0 + Vh.z);
	t2 = (1.0 - s) * sqrt(max(0.0, 1.0 - t1 * t1)) + s * t2;

	vec3 Nh = t1 * T1 + t2 * T2 + sqrt(max(0.0, 1.0 - t1 * t1 - t2 * t2)) * Vh;
	return normalize(vec3(alpha * Nh.x, alpha * Nh.y, max(1e-6, Nh.z)));
}

// Probability of the sampled reflection direction. The resolve divides the BRDF of a tap by the pdf of
// the sample that produced it, so the taps are weighted by how likely each direction was: without it a
// direction the sampler rarely shoots votes as loudly as one it shoots often, and the lobe comes out
// tilted towards whatever the rare samples hit. The reference divides by the pdf of its own sample as
// well; the sampler there is the plain GGX one, this is the visible normal distribution, whose pdf
// carries the G1 term.
float ReflectionPdf(vec3 Ve, vec3 N, vec3 H, float alpha)
{
	float NdotH = clamp(dot(N, H), 0.0, 1.0);
	float NdotV = clamp(dot(N, Ve), 1e-4, 1.0);
	float VdotH = clamp(dot(Ve, H), 1e-4, 1.0);

	float a2 = alpha * alpha;
	float d  = (NdotH * a2 - NdotH) * NdotH + 1.0;
	float D  = a2 / (SSR_PI * d * d + 1e-7);
	float G1 = 2.0 * NdotV / (NdotV + sqrt(a2 + (1.0 - a2) * NdotV * NdotV) + 1e-6);

	return (D * G1 * VdotH / NdotV) / (4.0 * VdotH);
}

// Half vector of a microfacet, in view space, for a surface with normal `N`. `bias` bends the sample
// towards the mirror direction: at 1 the half vector is the normal exactly, at 0 it is the plain lobe.
vec3 SampleReflectionHalf(vec3 V, vec3 N, float alpha, vec2 Xi, float bias)
{
	// Tangent frame around the shading normal.
	vec3 up = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
	vec3 T  = normalize(cross(up, N));
	vec3 B  = cross(N, T);

	// V points from the eye to the surface, the sampler wants the direction towards the eye.
	vec3 Ve = vec3(dot(-V, T), dot(-V, B), dot(-V, N));
	vec3 Nh = SampleGgxVndf(Ve, alpha, Xi);
	vec3 H  = normalize(T * Nh.x + B * Nh.y + N * Nh.z);

	// The bias is applied to the sampled direction, not to the random sequence. Pinning the second
	// random number to zero (the usual short cut) fixes the azimuth of every sample to one side of the
	// normal, which slides the whole reflection sideways as the bias goes to 1; bending the half vector
	// towards the normal converges on the mirror direction instead, which is what the bias is for.
	return normalize(mix(H, N, clamp(bias, 0.0, 1.0)));
}

// Fades the reflection out towards the screen border, where the depth buffer stops holding the
// geometry the ray would need.
float EdgeFade(vec2 uv)
{
	vec2 margin = ssrPass.screenParams.xy * (ssrPass.screenParams.z + ssrPass.screenParams.w) *
	              max(ssrPass.fades.x, 0.0);
	vec2 dist   = min(uv, vec2(1.0) - uv);
	return smoothstep(0.0, margin.x, dist.x) * smoothstep(0.0, margin.y, dist.y);
}

// Debug views. A grazing angle artifact has to be told apart by what the pass produces at each
// stage, not by guessing: depth bands point at the stored depth, a striped hit uv points at the
// march, a striped confidence points at the depth test, a low mip points at the gather.
#define SSR_DEBUG_REFLECTION 0
#define SSR_DEBUG_CONFIDENCE 1
#define SSR_DEBUG_MIP        2
#define SSR_DEBUG_DEPTH      3
#define SSR_DEBUG_HIT_UV     4
#define SSR_DEBUG_LENGTH     5
#define SSR_DEBUG_HIT_ERROR  6
// The modes are the numbers the editor's list is indexed with, so they are consecutive: a gap here is a
// slot the panel has to show as unavailable.
#define SSR_DEBUG_MARCH_COST 7

// Depth a screen pixel spans, for the adaptive thickness of the hit test.
#define SSR_THICKNESS_PIXELS 4.0
// Baseline of the normal the hit plane is built from, in texels.
#define SSR_PLANE_NORMAL_STEP 4.0

bool DebugViewEnabled()
{
	return ssrPass.flags.x > 0.5;
}

int DebugViewMode()
{
	return int(ssrPass.flags.z + 0.5);
}

// Mip level the hit color is gathered from. The lobe is sampled stochastically, so the directions
// already cover the cone; what the gather has to cover is the part of the cone the BRDF bias left
// out plus the footprint the surface has at the hit distance. Without that the residual spread of
// the samples stays in the image as noise the temporal resolve can only partly average out.
//
// `rayPixels` is the screen space length of the ray: the footprint grows with it.
float ReflectionLod(float roughness, float rayPixels, float brdfBias)
{
	float cone   = max(roughness * roughness * (1.0 - clamp(brdfBias, 0.0, 0.99)) * 0.5, 0.0001);
	float maxLod = log2(max(max(ssrPass.screenParams.z, ssrPass.screenParams.w), 2.0));

	return clamp(log2(max(cone * rayPixels, 1.0)), 0.0, maxLod);
}

// Gathers the reflection at the given mip. The mip chain carries the cone blur, the cross of taps
// only smooths the trilinear transition between levels so they do not show up as rings.
vec3 SampleReflection(vec2 uv, float lod)
{
	vec2 offset = ssrPass.screenParams.xy * exp2(lod) * 0.5;

	vec3 sum = textureLod(s_diffuseColor, uv, lod).rgb;
	sum += textureLod(s_diffuseColor, uv + vec2(offset.x, 0.0), lod).rgb;
	sum += textureLod(s_diffuseColor, uv - vec2(offset.x, 0.0), lod).rgb;
	sum += textureLod(s_diffuseColor, uv + vec2(0.0, offset.y), lod).rgb;
	sum += textureLod(s_diffuseColor, uv - vec2(0.0, offset.y), lod).rgb;

	return sum * 0.2;
}

// Radiance the environment provides along a world space direction, at the mip that matches the lobe the
// sample stands for. The sky cubemap is pre filtered exactly the way the material IBL uses it, so
// roughness selects the mip here too. This is where a ray that leaves the screen ends up: the reflection
// keeps a value it can be accumulated instead of falling back to "no data".
vec3 EnvironmentRadiance(vec3 worldDir, float roughness)
{
	if (ssrPass.environment.z < 0.5)
	{
		return vec3(0.0);
	}

	vec3  envDir = mat3(ssrPass.envRotation) * worldDir;
	float mip    = clamp(roughness, 0.0, 1.0) * ssrPass.environment.y;

	return textureLod(s_envSpecular, envDir, mip).rgb * ssrPass.environment.x;
}

void main()
{
	vec2 uv      = v_texture;
	vec4 gBuffer = texture(s_normalDepth, uv);

	// Every path that leaves without a hit reports a zero depth, so a stale hit is never reprojected.
	fragHit = vec4(0.0);

	float linearDepth = gBuffer.b;
	float roughness   = gBuffer.a;

	// Depth debug view first: it has to be readable everywhere, including the pixels this pass would
	// otherwise leave alone. Scaled by a fixed range so the stored depth is visible as bands.
	if (DebugViewEnabled() && DebugViewMode() == SSR_DEBUG_DEPTH)
	{
		fragColor = vec4(vec3(linearDepth / 50.0), 1.0);
		return;
	}

	// This pass produces one sample of the reflection per pixel and the coverage the resolve needs. The
	// sample is complete: where the screen space march finds nothing the environment cubemap stands in,
	// so a pixel is either not reflective at all (coverage zero) or carries a reflection.
	//
	// No geometry here (sky, background) or a surface too rough to mirror anything: no reflection.
	if (linearDepth <= 0.0 || roughness >= ssrPass.params.w)
	{
		fragColor = DebugViewEnabled() ? vec4(0.0, 0.0, 0.0, 1.0) : vec4(0.0);
		return;
	}

	vec3 viewPos   = SsrViewPos(uv, linearDepth);
	vec3 normal    = normalize(mat3(ssrPass.view) * decodeNormal(gBuffer.rg));
	vec3 geoNormal = GeometricNormal(uv, linearDepth, 1.0);

	// One sample of the reflection lobe per pixel instead of the mirror direction. A rough surface
	// reflects a cone, not a ray: sampling the visible normal distribution is what turns roughness
	// into a real blur instead of a mip level picked by hand, and the temporal resolve integrates
	// the samples over the frames. The bias bends the sample back towards the mirror direction.
	vec3 V      = normalize(viewPos);
	vec2 Xi     = SampleSequence(gl_FragCoord.xy, ssrPass.flags.w);
	float alpha = max(roughness * roughness, 0.0001);
	vec3 H      = SampleReflectionHalf(V, normal, alpha, Xi, clamp(ssrPass.temporal.z, 0.0, 1.0));
	vec3 rayDir = reflect(V, H);
	float pdf   = ReflectionPdf(-V, normal, H, alpha);

	// Push the ray start above the reconstructed surface. The further the shading normal leans away
	// from it, the bigger the push has to be, which is what keeps bumpy surfaces from self reflecting.
	float divergence = 1.0 - pow(clamp(dot(normal, geoNormal), 0.0, 1.0), 8.0);
	viewPos += geoNormal * (0.01 + 0.02 * linearDepth) * divergence;

	// A ray that immediately points into the surface would self intersect: bounce it once.
	if (dot(rayDir, geoNormal) < 0.0)
	{
		rayDir = normalize(reflect(rayDir, geoNormal));
	}

	// Rays pointing back at the camera can not hit anything the depth buffer holds: what they reflect is
	// behind the viewer. The environment is the whole answer for them, and writing it here - rather than a
	// zero - is what keeps the middle of a mirror from turning black.
	if (rayDir.z > 0.0)
	{
		vec3 worldDir = mat3(ssrPass.inverseView) * rayDir;
		fragColor     = vec4(EnvironmentRadiance(worldDir, roughness), 1.0);
		fragHit       = vec4(0.0);
		return;
	}

	// Per pixel jitter of the first step. The marching error becomes noise that the resolve below
	// averages out, instead of a stable banding pattern along the surface. Kept well under a full
	// step so what is left is small enough to be filtered.
	float jitter      = InterleavedGradientNoise(gl_FragCoord.xy) * 0.35;

	// The march advances in screen space: a step covers `stepPixels` pixels of screen movement, so the
	// whole budget covers `stepCount * stepPixels` pixels (the screen diagonal) whatever the angle,
	// and `stepCount` selects the density. A world space step instead samples the near field finely
	// and the far field coarsely, which is what slices a grazing reflection into bands.
	//
	// The view plane projection of the ray direction is what turns a screen movement into a world
	// step: a steep ray (a floor right in front of the camera) moves less screen distance per world
	// unit, so without it such a ray covers only a fraction of the screen and the reflection drops to
	// the environment map with a hard edge. MaxDistance stays the range limit, checked per iteration.
	//
	// The requested step count is a uniform used exactly as given; SSR_STEP_COUNT is only the
	// compile time bound of the loop, so the setting never gets rounded to a shader variant.
	float stepCount   = max(ssrPass.flags.y, 1.0);
	float pixelWorld  = 2.0 / (ssrPass.projParams.y * ssrPass.screenParams.w);
	float pixelScale  = pixelWorld / max(length(rayDir.xy), 0.05);
	float stepPixels  = max(length(ssrPass.screenParams.zw) / stepCount, 2.0);
	float maxDistance = ssrPass.params.y;

	vec3 rayPos      = viewPos + rayDir * (pixelScale * (-viewPos.z) * stepPixels) * jitter;
	vec3 prevPos     = rayPos;
	vec2 hitUV       = vec2(0.0);
	float hitDistance = 0.0;
	float hitDepthOut = 0.0;
	float hitError    = 0.0;
	float confidence  = 0.0;

	// Delta of the previous sample, so a ray that jumped over the whole thickness in one step (what
	// happens at shallow angles and over thin geometry) is still recognized as a crossing.
	float prevDelta  = -1.0;

	// Iterations the march consumed. A hierarchical march is supposed to spend fewer of them for the
	// same picture, which is what the debug view below reports: it is the only honest measure of what
	// the depth hierarchy buys, since the picture itself is the same either way.
	float marchedIterations = 0.0;

	// Hierarchical march. The pyramid answers one question per step: does the tile the ray is in hold
	// anything in front of the ray? If not, the step is a whole tile of that level and the ray climbs to a
	// coarser one; if it does, the ray descends to look closer. The hit test itself stays against the exact
	// depth of the g buffer, so the pyramid only decides how far a step may jump - it never says "hit".
	int mip = 0;

	for (int i = 0; i < SSR_STEP_COUNT; ++i)
	{
		if (float(i) >= stepCount)
		{
			break;
		}

		vec2 probeUV;
		if (SSR_USE_PYRAMID == 1 && SsrProjectToUV(rayPos, probeUV))
		{
			float tileDepth = PyramidDepth(probeUV, mip);

			if (-rayPos.z >= tileDepth && mip > 0)
			{
				// Something this level holds is in front of the ray: the tile is not empty at this
				// resolution, so look at the finer one before stepping.
				mip -= 1;
				continue;
			}

			if (mip < SSR_PYRAMID_MIP_COUNT - 1 && -rayPos.z < tileDepth * 0.5)
			{
				// Far in front of everything this level holds: the next step may cover a bigger tile.
				mip += 1;
			}
		}

		marchedIterations += 1.0;
		prevPos = rayPos;
		rayPos += rayDir * (pixelScale * (-rayPos.z) * stepPixels * exp2(float(mip)));

		vec3 rayOffset = rayPos - viewPos;
		if (dot(rayOffset, rayOffset) > maxDistance * maxDistance)
		{
			break;
		}

		vec2 rayUV;
		if (!SsrProjectToUV(rayPos, rayUV))
		{
			break;
		}

		float sceneDepth = SceneLinearDepth(rayUV);

		// One screen pixel spans more depth the further away and the more parallel the surface is to the
		// view. A fixed thickness then ends up thinner than a single pixel, so the hit test flips
		// between neighbouring scanlines: widen it to at least a few pixels worth of depth.
		float thicknessNow = max(ssrPass.params.z, pixelWorld * (-rayPos.z) * SSR_THICKNESS_PIXELS);

		// Positive delta means the ray walked behind the geometry at that pixel. Inside the thickness
		// it is a regular candidate; a delta that flipped from in front to behind between two samples
		// is a candidate as well, because a shallow ray or a thin surface can be skipped whole. How
		// much of either is trusted is decided by the confidence below, which is what keeps grazing
		// rays from hatching between hit and miss.
		float delta   = -rayPos.z - sceneDepth;
		bool inside   = delta > 0.0 && delta < thicknessNow;
		bool crossing = prevDelta <= 0.0 && delta > 0.0;
		prevDelta     = delta;

		if (sceneDepth <= 0.0 || (!inside && !crossing))
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
			if (!SsrProjectToUV(mid, midUV))
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
		if (!SsrProjectToUV(hi, refinedUV))
		{
			continue;
		}

		float hitDepth = SceneLinearDepth(refinedUV);
		if (hitDepth <= 0.0)
		{
			continue;
		}

		// How far the bisected crossing is from the surface it belongs to.
		float miss = length(hi - SsrViewPos(refinedUV, hitDepth));

		// The stored shading normal, used to reject backfaces below.
		vec3 hitNormal   = normalize(mat3(ssrPass.view) * decodeNormal(texture(s_normalDepth, refinedUV).rg));

		// Refine onto the plane of the surface at the hit pixel, built from a wider depth neighbourhood
		// than one texel so the stored depth quantization does not dominate its orientation. The result
		// is continuous in the ray parameters and exact where the surface is flat, so the hit slides
		// smoothly instead of snapping to the depth texel grid. The bisection stays as the fallback for
		// a plane that is degenerate or that projects off screen; a poor plane shows up as a large miss
		// and fades out through the confidence below.
		vec3 surfaceNorm = GeometricNormal(refinedUV, hitDepth, SSR_PLANE_NORMAL_STEP);
		float planeDenom = dot(surfaceNorm, rayDir);
		if (abs(planeDenom) > 0.0001)
		{
			float planeT = dot(surfaceNorm, SsrViewPos(refinedUV, hitDepth) - viewPos) / planeDenom;
			if (planeT > 0.0)
			{
				vec3 planePos = viewPos + rayDir * planeT;
				vec2 planeUV;
				if (SsrProjectToUV(planePos, planeUV))
				{
					float planeDepth = SceneLinearDepth(planeUV);
					if (planeDepth > 0.0)
					{
						refinedUV = planeUV;
						hitDepth  = planeDepth;
						miss      = length(planePos - SsrViewPos(planeUV, planeDepth));
					}
				}
			}
		}

		// Reject the hits the ray walked through. When the ray barely moves across the screen it is
		// almost parallel to the view direction, and a surface whose normal agrees with the ray is a
		// backface: accepting it would paint the reflection onto the inside of the geometry.
		bool frontal = all(lessThan(abs(refinedUV - uv), ssrPass.screenParams.xy * 4.0));
		if (frontal && dot(rayDir, hitNormal) >= 0.0)
		{
			continue;
		}

		// Trust the hit by how close the ray actually got to the surface it crossed: a grazing
		// crossing touches the reconstructed surface far away and fades out instead of popping.
		float conf = 1.0 - smoothstep(0.0, thicknessNow, miss);
		conf      *= conf;

		if (conf <= 0.0)
		{
			continue;
		}

		hitUV        = refinedUV;
		hitDistance  = length(hi - viewPos);
		hitDepthOut  = hitDepth;
		hitError     = miss;
		confidence   = conf;
		break;
	}

	// Where the ray ended, crossed a surface or not. A miss ends at the last position the march reached:
	// the environment sample stands in for the reflection in that direction, and the reprojection needs a
	// position to carry the history with, exactly as a hit does.
	vec2  rayEndUV    = uv;
	float rayEndDepth = max(-rayPos.z, 0.001);
	float rayLength   = ssrPass.params.y;
	{
		vec2 projected;
		if (SsrProjectToUV(rayPos, projected))
		{
			rayEndUV = projected;
		}
		rayLength = max(length(rayPos - viewPos), 0.001);
	}

	// The reflection direction in world space. Both the environment sample and the checker of the
	// reflection stand along it, so it is what the pixel reflects whichever branch below is taken.
	vec3 worldRayDir = mat3(ssrPass.inverseView) * normalize(rayDir);

	// The march can only reach `stepCount * stepPixels` pixels, and past that the reflection is the
	// environment's. Fading between the two instead of cutting is what keeps the last reachable row from
	// ending in a hard edge: without it a hit sits directly next to a miss, which is a visible cut in the
	// middle of the floor. Where each fade starts is a setting, so both are kept off the unreachable end
	// (a start equal to the limit is a smoothstep between two equal edges, undefined).
	//
	// No fade on the incidence angle. A shallow surface is exactly where a reflection is wanted, and a
	// threshold there is a threshold on a camera dependent value: lowering the camera puts a large part of
	// a floor below it at once, and the reflection disappears in a single frame. No gloss fade either: a
	// rough surface keeps its reflection, the blur it needs comes from the sampled direction and the cone
	// mip, not from fading the whole reflection out.
	float reachable       = stepCount * stepPixels;
	float hitPixels       = length(hitUV - uv) * max(ssrPass.screenParams.z, ssrPass.screenParams.w);
	float screenFadeStart = clamp(ssrPass.fades.y, 0.0, 0.95);
	float rangeFadeStart  = clamp(ssrPass.fades.z, 0.0, 0.95);
	float screenFade      = 1.0 - smoothstep(reachable * screenFadeStart, reachable, hitPixels);
	float rangeFade       = 1.0 - smoothstep(ssrPass.params.y * rangeFadeStart, ssrPass.params.y, hitDistance);

	// A hit on the pixel's own neighbourhood is the surface reflecting itself: the march starts a fraction
	// of a step along the ray, so an immediate crossing is a depth test artifact and not a reflection.
	float minAdvance = smoothstep(0.0, 2.0, hitPixels);

	float trust = 0.0;
	if (confidence > 0.0)
	{
		trust = clamp(confidence * EdgeFade(hitUV) * screenFade * rangeFade * minAdvance, 0.0, 1.0);
	}

	// Confidence, the screen border and the two ray length limits all move the pixel's reflection towards
	// the environment, which is what the forward pass shaded it with anyway. Losing the screen space hit
	// never loses the reflection: it only makes it coarser.
	float lod         = 0.0;
	vec3  hitRadiance = vec3(0.0);
	if (confidence > 0.0)
	{
		// The reflection cone picks the mip, the mip chain does the blur.
		lod         = ReflectionLod(roughness, hitPixels, ssrPass.temporal.z);
		hitRadiance = SampleReflection(hitUV, lod);
		fragHit     = vec4(hitUV, hitDepthOut, hitDistance);
	}
	else
	{
		fragHit = vec4(rayEndUV, rayEndDepth, rayLength);
	}

	vec3 envRadiance = EnvironmentRadiance(worldRayDir, roughness);
	vec3 radiance    = mix(envRadiance, hitRadiance, trust);

	// Debug view: show what this pass produces at each stage, so an artifact is told apart instead
	// of guessed at. The resolve pass hands these through unchanged.
	if (DebugViewEnabled())
	{
		int mode = DebugViewMode();
		if (mode == SSR_DEBUG_CONFIDENCE)
		{
			fragColor = vec4(vec3(trust), 1.0);
			return;
		}
		if (mode == SSR_DEBUG_MIP)
		{
			fragColor = vec4(vec3(lod / 6.0), 1.0);
			return;
		}
		if (mode == SSR_DEBUG_HIT_UV)
		{
			fragColor = vec4(fragHit.xy, 0.0, 1.0);
			return;
		}
		if (mode == SSR_DEBUG_LENGTH)
		{
			fragColor = vec4(vec3(hitDistance / max(maxDistance, 0.001)), 1.0);
			return;
		}
		if (mode == SSR_DEBUG_MARCH_COST)
		{
			// Fraction of the step budget this pixel spent. Bright means the march ran out of iterations,
			// which on a phone is the number that matters: it is what a lower budget would truncate.
			fragColor = vec4(vec3(marchedIterations / max(stepCount, 1.0)), 1.0);
			return;
		}
		if (mode == SSR_DEBUG_HIT_ERROR)
		{
			// How far the ray ended up from the surface it crossed, in units of the thickness. Bright
			// bands here mean the depth the march compared against is wrong or too coarse.
			fragColor = vec4(vec3(hitError / max(ssrPass.params.z, 0.001)), 1.0);
			return;
		}

		fragColor = vec4(radiance, 1.0);
		return;
	}

	// rgb: the complete reflection, a: coverage - one where this pixel is a reflective surface. A miss is
	// not "no data" any more: it is the environment's reflection, which accumulates just as well as a hit.
	fragColor = vec4(radiance, 1.0);
}

	-->
	</source>
</shader>
