<shader>
	<type name = "fragmentShader" />
	<include name = "vulkanCompatInc.shader" />
	<include name = "normalEncodingInc.shader" />
	<include name = "ssrPassDataInc.shader" />
	<texture slot = "0" name = "s_diffuseColor" />
	<texture slot = "1" name = "s_normalDepth" />
	<define name = "SSR_MAX_STEPS" val = "64,128,256,512" />
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

	vec3 dx = ReconstructViewPos(uv + vec2(texel.x, 0.0), right) - ReconstructViewPos(uv - vec2(texel.x, 0.0), left);
	vec3 dy = ReconstructViewPos(uv + vec2(0.0, texel.y), top) - ReconstructViewPos(uv - vec2(0.0, texel.y), bottom);

	vec3 normal = normalize(cross(dx, dy));

	// The surface the depth buffer holds faces the eye, so its normal has to point back along the view
	// ray to that surface. That is decided against the view direction of the centre pixel, not against
	// the normal's own z: a floor seen at a grazing angle has a z near zero, so the sign of a rounding
	// level value flips the normal from scanline to scanline. A flipped normal points the ray start
	// into the surface it just left, the march then compares the ray against the wrong side of the
	// geometry and whole scanlines drop their reflection.
	vec3 toSurface = ReconstructViewPos(uv, linearDepth);
	return dot(normal, toSurface) < 0.0 ? normal : -normal;
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

// Mip level the reflection is gathered from. A reflection cone widens with roughness and with the
// length of the ray, and the scene color mip chain is what turns that cone into a blur: without it a
// mirror reflection of a slanted surface aliases into stripes, and the step jitter stays visible as
// dithering instead of averaging out.
//
// `rayLength` is the length of the ray in screen space, normalized so that 1.0 spans the larger
// screen dimension: the footprint that matters for aliasing is the screen space one, not the world
// space one. A world space cone would blur even a mirror by several mips.
//
// `incidenceCos` is the cosine between the view direction and the surface normal. A grazing surface
// compresses the reflected image: the reflected scene packs the secant of that angle more content
// into the same screen footprint. That compression is the dominant alias source on a shallow floor,
// and it is independent of the march density: no step count fixes it, only the gather does.
float ReflectionLod(float roughness, float rayLength, float incidenceCos)
{
	float coneAngle = min(roughness, 0.999) * 3.14159265 * 0.5;
	float coneLen   = max(rayLength, 0.0001);
	float opLen     = 2.0 * tan(coneAngle) * coneLen;

	// Sphere fitted inside the cone, ending at the end of the cone (the same fit cone traced
	// reflections use).
	float radius    = (opLen * (sqrt(opLen * opLen + 4.0 * coneLen * coneLen) - opLen)) / (4.0 * coneLen);

	float maxScreen = max(ssrPass.screenParams.z, ssrPass.screenParams.w);
	float maxLod    = log2(max(maxScreen, 2.0));

	// The divisor sets how much blur a given cone radius buys. The hit is refined onto the surface
	// plane, so the gather does not have to hide a quantized hit any more: it only has to cover the
	// cone, and a wider divisor keeps the reflection crisp.
	float base      = log2(max(radius * maxScreen / 32.0, 1.0));

	// sec(incidence) in mip levels, capped at 2: a grazing surface does compress the reflected image,
	// but the full secant blurred a mirror into mush without removing the bands, which came from the
	// hit position rather than from the gather.
	float grazing   = min(log2(1.0 / max(abs(incidenceCos), 0.05)), 2.0);

	return clamp(base + grazing, 0.0, maxLod);
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

void main()
{
	vec2 uv      = v_texture;
	vec4 gBuffer = texture(s_normalDepth, uv);

	float linearDepth = gBuffer.b;
	float roughness   = gBuffer.a;

	// Depth debug view first: it has to be readable everywhere, including the pixels this pass would
	// otherwise leave alone. Scaled by a fixed range so the stored depth is visible as bands.
	if (DebugViewEnabled() && DebugViewMode() == SSR_DEBUG_DEPTH)
	{
		fragColor = vec4(vec3(linearDepth / 50.0), 1.0);
		return;
	}

	// This pass only produces the reflection and its weight; the filter pass that follows averages them
	// in screen space and composites the result over the scene color. A pixel this pass leaves alone
	// therefore writes a zero weight, not a colour: the filter keeps the forward shaded pixel for it.
	//
	// No geometry here (sky, background) or a surface too rough to mirror anything: no reflection. What
	// lights the pixel is the sky or the active environment volumes through the forward pass result.
	if (linearDepth <= 0.0 || roughness >= ssrPass.params.w)
	{
		fragColor = DebugViewEnabled() ? vec4(0.0, 0.0, 0.0, 1.0) : vec4(0.0);
		return;
	}

	vec3 viewPos   = ReconstructViewPos(uv, linearDepth);
	vec3 normal    = normalize(mat3(ssrPass.view) * decodeNormal(gBuffer.rg));
	vec3 geoNormal = GeometricNormal(uv, linearDepth, 1.0);
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
		fragColor = DebugViewEnabled() ? vec4(0.0, 0.0, 0.0, 1.0) : vec4(0.0);
		return;
	}

	// Per pixel jitter of the first step. The marching error becomes noise that the mip gather below
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
	// The requested step count is a uniform used exactly as given; SSR_MAX_STEPS is only the
	// compile time bound of the loop, so the setting never gets rounded to a shader variant.
	float stepCount   = max(ssrPass.flags.y, 1.0);
	float pixelWorld  = 2.0 / (ssrPass.projParams.y * ssrPass.screenParams.w);
	float pixelScale  = pixelWorld / max(length(rayDir.xy), 0.05);
	float stepPixels  = clamp(length(ssrPass.screenParams.zw) / stepCount, 2.0, 64.0);
	float maxDistance = ssrPass.params.y;

	vec3 rayPos      = viewPos + rayDir * (pixelScale * (-viewPos.z) * stepPixels) * jitter;
	vec3 prevPos     = rayPos;
	vec2 hitUV       = vec2(0.0);
	float hitDistance = 0.0;
	float hitError    = 0.0;
	float confidence  = 0.0;

	// Delta of the previous sample, so a ray that jumped over the whole thickness in one step (what
	// happens at shallow angles and over thin geometry) is still recognized as a crossing.
	float prevDelta  = -1.0;

	for (int i = 0; i < SSR_MAX_STEPS; ++i)
	{
		if (float(i) >= stepCount)
		{
			break;
		}

		prevPos = rayPos;
		rayPos += rayDir * (pixelScale * (-rayPos.z) * stepPixels);

		vec3 rayOffset = rayPos - viewPos;
		if (dot(rayOffset, rayOffset) > maxDistance * maxDistance)
		{
			break;
		}

		vec2 rayUV;
		if (!ProjectToUV(rayPos, rayUV))
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

		// How far the bisected crossing is from the surface it belongs to.
		float miss = length(hi - ReconstructViewPos(refinedUV, hitDepth));

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
			float planeT = dot(surfaceNorm, ReconstructViewPos(refinedUV, hitDepth) - viewPos) / planeDenom;
			if (planeT > 0.0)
			{
				vec3 planePos = viewPos + rayDir * planeT;
				vec2 planeUV;
				if (ProjectToUV(planePos, planeUV))
				{
					float planeDepth = SceneLinearDepth(planeUV);
					if (planeDepth > 0.0)
					{
						refinedUV = planeUV;
						hitDepth  = planeDepth;
						miss      = length(planePos - ReconstructViewPos(planeUV, planeDepth));
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

		hitUV       = refinedUV;
		hitDistance = length(hi - viewPos);
		hitError    = miss;
		confidence  = conf;
		break;
	}

	if (confidence <= 0.0)
	{
		// No screen space reflection found: the pixel keeps the forward shaded colour, and the filter
		// pass sees the zero weight. The debug view keeps such pixels black.
		fragColor = DebugViewEnabled() ? vec4(0.0, 0.0, 0.0, 1.0) : vec4(0.0);
		return;
	}

	// The reflection cone picks the mip, the mip chain does the blur.
	float rayUV     = length(hitUV - uv);
	float incidence = dot(normalize(viewPos), normal);
	float lod       = ReflectionLod(roughness, rayUV / max(ssrPass.screenParams.z, ssrPass.screenParams.w), incidence);
	vec3 reflection = SampleReflection(hitUV, lod);

	// Gloss, hit confidence, the screen border and the two ray length limits all fade the reflection
	// out, so the forward shaded environment reflection takes over exactly where this one is not
	// trustworthy. The screen length fade only starts past the screen diagonal: the reflection cone
	// already blurs the long rays, and fading them earlier cut the reflection off in the middle of the
	// screen.
	float gloss      = 1.0 - roughness / max(ssrPass.params.w, 0.0001);
	float rayPixels  = rayUV * max(ssrPass.screenParams.z, ssrPass.screenParams.w);

	// The march can only reach `stepCount * stepPixels` pixels, and past that the pixel keeps the
	// forward shaded environment reflection. Fading towards that limit is what keeps the last reachable
	// row from ending in a hard edge: without it a hit sits directly next to a miss, which is a visible
	// cut in the middle of the floor.
	float coverage   = stepCount * stepPixels;
	float screenFade = 1.0 - smoothstep(coverage * 0.5, coverage, rayPixels);
	float rangeFade  = 1.0 - smoothstep(ssrPass.params.y * 0.6, ssrPass.params.y, hitDistance);

	// No fade on the incidence angle. A shallow surface is exactly where a reflection is wanted, and a
	// threshold there is a threshold on a camera dependent value: lowering the camera puts a large part
	// of a floor below it at once, and the reflection disappears in a single frame. The hard part of a
	// grazing angle is handled where it comes from instead, by the resolution of the depth the crossing
	// is computed from in the pre process pass.
	float weight     = clamp(ssrPass.params.x * gloss * confidence * EdgeFade(hitUV) * screenFade * rangeFade, 0.0, 1.0);

	// Debug view: show what this pass produces at each stage, so a grazing angle artifact is told
	// apart instead of guessed at. A miss, a rough surface or a pixel the ray budget did not reach
	// stays black. The filter pass hands these through unchanged.
	if (DebugViewEnabled())
	{
		int mode = DebugViewMode();
		if (mode == SSR_DEBUG_CONFIDENCE)
		{
			fragColor = vec4(vec3(confidence), 1.0);
			return;
		}
		if (mode == SSR_DEBUG_MIP)
		{
			fragColor = vec4(vec3(lod / 6.0), 1.0);
			return;
		}
		if (mode == SSR_DEBUG_HIT_UV)
		{
			fragColor = vec4(hitUV, 0.0, 1.0);
			return;
		}
		if (mode == SSR_DEBUG_LENGTH)
		{
			fragColor = vec4(vec3(hitDistance / max(maxDistance, 0.001)), 1.0);
			return;
		}
		if (mode == SSR_DEBUG_HIT_ERROR)
		{
			// How far the ray ended up from the surface it crossed, in units of the thickness. Bright
			// bands here mean the depth the march compared against is wrong or too coarse.
			fragColor = vec4(vec3(hitError / max(ssrPass.params.z, 0.001)), 1.0);
			return;
		}

		fragColor = vec4(reflection * weight, 1.0);
		return;
	}

	// The filter pass averages this with its neighbours, premultiplied by the weight.
	fragColor = vec4(reflection, weight);
}

	-->
	</source>
</shader>
