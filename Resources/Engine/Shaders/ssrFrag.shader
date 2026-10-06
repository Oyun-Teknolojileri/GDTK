<shader>
	<type name = "fragmentShader" />
	<include name = "vulkanCompatInc.shader" />
	<include name = "normalEncodingInc.shader" />
	<include name = "ssrPassDataInc.shader" />
	<texture slot = "0" name = "s_diffuseColor" />
	<texture slot = "1" name = "s_normalDepth" />
	<texture slot = "2" name = "s_hiz1" />
	<texture slot = "3" name = "s_hiz2" />
	<texture slot = "4" name = "s_hiz3" />
	<texture slot = "5" name = "s_hiz4" />
	<define name = "SSR_MAX_STEPS" val = "64,128,256,512" />
	<source>
	<!--

precision highp float;
precision lowp int;

TK_LOC(2) in vec2 v_texture;

layout (location = 0) out vec4 fragColor;

TK_SAMPLER_BINDING(0) uniform sampler2D s_diffuseColor; // Scene color, HDR and pre tonemap.
TK_SAMPLER_BINDING(1) uniform sampler2D s_normalDepth;  // rg: world normal, b: linear depth, a: roughness.

// Nearest surface depth pyramid of the g buffer, one target per level: level 1 covers 4 x 4 texels,
// level 2 covers 4 x 4 tiles of level 1, and so on, so level n covers 4 ^ n pixels. The march tests
// a ray against a tile of this pyramid instead of against a single depth sample, which is what lets
// it step over space nothing can be hit in and what keeps it from stepping over geometry.
TK_SAMPLER_BINDING(2) uniform sampler2D s_hiz1;
TK_SAMPLER_BINDING(3) uniform sampler2D s_hiz2;
TK_SAMPLER_BINDING(4) uniform sampler2D s_hiz3;
TK_SAMPLER_BINDING(5) uniform sampler2D s_hiz4;

// Must match HIZ_NO_SURFACE in hiZDepthFrag.shader: a tile that holds no surface at all.
#define SSR_NO_SURFACE 100000.0

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

// Project a view space position to a scene UV without a range check, Vulkan's flipped texture v
// applied. The march needs the uv of positions that are still outside the frame: that is how it
// knows where the ray leaves the screen.
vec2 ViewToUv(vec3 viewPos)
{
	float invW = -1.0 / viewPos.z;
	vec2 uv = vec2(ssrPass.projParams.x * viewPos.x + ssrPass.projParams.z * viewPos.z,
	               ssrPass.projParams.y * viewPos.y + ssrPass.projParams.w * viewPos.z) * invW * 0.5 + 0.5;
#ifdef VULKAN
	uv.y = 1.0 - uv.y;
#endif
	return uv;
}

// Project a view space position to a scene UV. False when it can not be sampled: behind the camera
// or outside the screen.
bool ProjectToUV(vec3 viewPos, out vec2 uv)
{
	if (viewPos.z >= 0.0)
	{
		uv = vec2(0.0);
		return false;
	}

	uv = ViewToUv(viewPos);
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
#define SSR_DEBUG_REFLECTION  0
#define SSR_DEBUG_CONFIDENCE  1
#define SSR_DEBUG_MIP         2
#define SSR_DEBUG_DEPTH       3
#define SSR_DEBUG_HIT_UV      4
#define SSR_DEBUG_LENGTH      5
#define SSR_DEBUG_HIT_ERROR   6
#define SSR_DEBUG_HIZ_LEVEL   7
#define SSR_DEBUG_ITERATIONS  8
#define SSR_DEBUG_TILE_DEPTH  9

// Depth a screen pixel spans, for the adaptive thickness of the hit test.
#define SSR_THICKNESS_PIXELS 4.0
// Baseline of the normal the hit plane is built from, in texels.
#define SSR_PLANE_NORMAL_STEP 4.0
// Baseline of the normal a hit is oriented by, in texels. Narrower than the plane fit: the
// orientation has to stay readable next to a silhouette, where a wide baseline reads two surfaces.
#define SSR_HIT_NORMAL_STEP 2.0

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
// mirror reflection of a slanted surface aliases into stripes, and the hit placement stays visible
// as dithering instead of averaging out.
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

////////////////////////////////////////
// Hi-Z march
////////////////////////////////////////

// A uv target the axis solver is asked for is a value in the backend's own texture space. Vulkan
// samples the scene targets with a flipped v, so a uv the shader measured is turned back here.
float AxisTarget(float target)
{
#ifdef VULKAN
	return 1.0 - target;
#else
	return target;
#endif
}

// Lambda at which the projected ray crosses `target` on one screen axis. uv is
// 0.5 * (projScale * axis + projOffset * z) / -z + 0.5, so uv == target is linear in lambda and the
// crossing solves exactly instead of being stepped towards. An axis the ray does not move along
// reports a lambda past the end of the segment.
float ScreenAxisLambda(float axis0, float axisDelta,
                       float z0, float zDelta,
                       float projScale, float projOffset,
                       float target)
{
	float c        = 2.0 * target - 1.0;
	float constant = projScale * axis0 + (projOffset + c) * z0;
	float slope    = projScale * axisDelta + (projOffset + c) * zDelta;

	return abs(slope) > 1e-9 ? -constant / slope : 1e9;
}

// Lambda at which the ray leaves the tile it is in. `lambda` is where the ray is now, `uv` the
// scene uv of that position and `uvEnd` the uv of the far end of the segment, which gives the
// direction the ray travels.
//
// A border the ray is moving away from, or one it has already crossed, is not an exit: the
// projection of a straight ray is asymptotic, so a border it approaches but never reaches solves to
// a lambda behind the ray. Reporting that as the exit leaves the traversal creeping forward a
// fraction of a lambda per iteration instead of stepping over the tile, which is what kept every
// reflection from being found.
float TileExitLambda(vec3 origin, vec3 segment, float lambda, vec2 uv, vec2 uvEnd,
                     vec2 tileOrigin, vec2 tileSize)
{
	float targetX = uvEnd.x > uv.x ? tileOrigin.x + tileSize.x : tileOrigin.x;
	float targetY = uvEnd.y > uv.y ? tileOrigin.y + tileSize.y : tileOrigin.y;

	float lambdaX = ScreenAxisLambda(origin.x, segment.x, origin.z, segment.z,
	                                 ssrPass.projParams.x, ssrPass.projParams.z, targetX);
	float lambdaY = ScreenAxisLambda(origin.y, segment.y, origin.z, segment.z,
	                                 ssrPass.projParams.y, ssrPass.projParams.w, AxisTarget(targetY));

	lambdaX = lambdaX > lambda ? lambdaX : 1e9;
	lambdaY = lambdaY > lambda ? lambdaY : 1e9;

	return min(lambdaX, lambdaY);
}

// Depth window a tile of `level` holds: .x the nearest surface, .y the farthest one, both positive
// linear view depth. A tile that holds no surface reports (.x = SSR_NO_SURFACE, .y = 0), which no
// ray window can meet. Level 0 is the g buffer texel itself.
vec2 HiZRange(int level, vec2 uv)
{
	if (level <= 0)
	{
		float depth = SceneLinearDepth(uv);
		return depth > 0.0 ? vec2(depth, depth) : vec2(SSR_NO_SURFACE, 0.0);
	}

	if (level == 1)
	{
		return texture(s_hiz1, uv).xy;
	}

	if (level == 2)
	{
		return texture(s_hiz2, uv).xy;
	}

	if (level == 3)
	{
		return texture(s_hiz3, uv).xy;
	}

	return texture(s_hiz4, uv).xy;
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

	// What the depth pyramid holds at the first level, the 4 x 4 texel tiles: red the nearest
	// surface, green the farthest one, both scaled by a fixed range. Black means the level holds no
	// surface at all, which is what tells a pyramid that was never written from a hit test that
	// refuses to fire.
	if (DebugViewEnabled() && DebugViewMode() == SSR_DEBUG_TILE_DEPTH)
	{
		vec2 tileRange = texture(s_hiz1, v_texture).xy;
		fragColor      = vec4(tileRange.x / 50.0, tileRange.y / 50.0, 0.0, 1.0);
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

	float stepCount   = max(ssrPass.flags.y, 1.0);
	float pixelWorld  = 2.0 / (ssrPass.projParams.y * ssrPass.screenParams.w);
	float maxDistance = ssrPass.params.y;

	// The ray as a view space segment, and its depth as a function of the position along it. The view
	// depth is linear in lambda, so every depth the march compares against has an exact lambda and no
	// comparison has to be made at a sample the march happened to land on.
	vec3  segment    = rayDir * maxDistance;
	vec2  uvEnd      = ViewToUv(viewPos + segment);
	float depthStart = -viewPos.z;
	float depthSlope = -(viewPos + segment).z - depthStart;

	// The depth buffer holds nothing outside the frame, so the traversal is limited to the part of the
	// ray that is on screen. A border crossing is solved, not stepped towards.
	float lambdaEnd = 1.0;

	if (uvEnd.x < 0.0 || uvEnd.x > 1.0)
	{
		lambdaEnd = min(lambdaEnd,
		                ScreenAxisLambda(viewPos.x, segment.x, viewPos.z, segment.z,
		                                 ssrPass.projParams.x, ssrPass.projParams.z,
		                                 uvEnd.x < 0.0 ? 0.0 : 1.0));
	}

	if (uvEnd.y < 0.0 || uvEnd.y > 1.0)
	{
		lambdaEnd = min(lambdaEnd,
		                ScreenAxisLambda(viewPos.y, segment.y, viewPos.z, segment.z,
		                                 ssrPass.projParams.y, ssrPass.projParams.w,
		                                 AxisTarget(uvEnd.y < 0.0 ? 0.0 : 1.0)));
	}

	lambdaEnd = clamp(lambdaEnd, 0.0, 1.0);

	// The march walks the pyramid from the finest tile it is allowed to resolve up to the coarsest one
	// and back down:
	//
	// - A tile whose depth window the ray's own depth window does not meet holds nothing the ray can
	//   cross, so the ray steps over the whole tile and tries a coarser one. That is where the budget
	//   goes: an iteration that steps over a 64 pixel tile covers 64 pixels of the screen.
	// - A tile the two windows do meet is descended into, until a tile of `leafPixels` pixels decides.
	//   There the nearest surface the tile holds is the surface the ray runs into, wherever inside the
	//   tile it happens to be, and the position along the ray where its depth reaches that surface
	//   solves directly instead of being searched for.
	//
	// Both halves matter. Meeting windows is what keeps the march from walking over geometry between
	// two samples, whatever the geometry's depth inside the tile is, and a tile sized step is what
	// keeps a long ray from having to walk the screen pixel by pixel.
	//
	// The finest tile follows the step count instead of being a single texel: the step count already
	// says how finely the reflection is meant to be resolved, `stepCount` tiles have to cover the
	// frame, and descending below that only spends the budget on the tiles a grazing ray keeps
	// meeting (the floor it skims) instead of on the ray. A tile is still many times finer than the
	// fixed screen space step this march replaces, which is what used to jump over the geometry a
	// reflection was looking for, and the plane refinement of the hit recovers the sub pixel
	// placement anyway.
	float stepPixels = clamp(length(ssrPass.screenParams.zw) / max(stepCount, 1.0), 2.0, 64.0);
	float tileScale  = max(ssrPass.hizParams.y, 1.0);
	float maxLevel   = max(ssrPass.hizParams.x, 0.0);
	int   leafLevel  = clamp(int(floor(log2(stepPixels) * 0.5 + 0.5)), 1, int(maxLevel));

	int   level      = leafLevel;
	float cellPixels = pow(tileScale, float(leafLevel));

	// Start past the tile the ray leaves. That tile holds the surface the ray started on, and a
	// grazing ray overlaps it by construction, so testing it can only produce a self hit.
	//
	// The tile is the one the ray's own origin falls in, not the one this fragment is in: pushing the
	// origin off the surface moves it by up to a few pixels on screen, which is more than a fine tile
	// is wide. Starting from the fragment's own uv instead leaves the ray outside the tile it is
	// supposed to leave, the exit solves to a lambda behind the ray, and the traversal then creeps
	// forward a fraction of a lambda per iteration.
	vec2  startUv     = ViewToUv(viewPos) + sign(uvEnd - ViewToUv(viewPos)) * (0.25 * ssrPass.screenParams.xy);
	vec2  startTile   = cellPixels * ssrPass.screenParams.xy;
	vec2  startOrigin = floor(startUv / startTile) * startTile;
	float lambda      = clamp(TileExitLambda(viewPos, segment, 0.0, startUv, uvEnd, startOrigin, startTile), 0.0, lambdaEnd);

	float hitLambda   = 0.0;
	float confidence  = 0.0;
	float hitDistance = 0.0;
	float hitError    = 0.0;
	float iterations  = 0.0;
	float peakLevel   = 0.0;
	vec2  hitUV       = vec2(0.0);

	for (int i = 0; i < SSR_MAX_STEPS; ++i)
	{
		if (float(i) >= stepCount || lambda >= lambdaEnd)
		{
			break;
		}

		iterations = float(i) + 1.0;
		peakLevel  = max(peakLevel, float(level));

		vec3 pos   = viewPos + segment * lambda;
		vec2 rayUV = ViewToUv(pos);

		if (rayUV.x < 0.0 || rayUV.x > 1.0 || rayUV.y < 0.0 || rayUV.y > 1.0)
		{
			break;
		}

		vec2 cellSize   = cellPixels * ssrPass.screenParams.xy;

		// The tile is read a quarter of a texel along the ray's direction. A ray that lands exactly on
		// the border it has just crossed rounds back into the tile it came from, whose far border is
		// the one it is standing on: the exit then solves to the current lambda, is filtered as "not
		// ahead of the ray", and the traversal stalls there for the rest of the budget. A quarter of a
		// texel is far too small to step over anything.
		vec2 probeUv    = rayUV + sign(uvEnd - rayUV) * (0.25 * ssrPass.screenParams.xy);
		vec2 cellOrigin = floor(probeUv / cellSize) * cellSize;
		float cellExit  = min(lambdaEnd, TileExitLambda(viewPos, segment, lambda, rayUV, uvEnd, cellOrigin, cellSize));

		// A tile is never crossed backwards, and a float that lands on the exit anyway must not stall
		// the traversal.
		cellExit = max(cellExit, min(lambda + 1e-6, lambdaEnd));

		// Depth window the ray has while it is inside this tile, and the depth window of everything
		// the tile holds. Empty tiles report a farthest depth of zero, which no ray window meets.
		float rayNear  = depthStart + depthSlope * lambda;
		float rayFar   = depthStart + depthSlope * cellExit;
		vec2  tileRange = HiZRange(level, cellOrigin + cellSize * 0.5);
		bool  meets     = tileRange.y > 0.0 && tileRange.x <= rayFar && tileRange.y >= rayNear;

		if (meets && level > leafLevel)
		{
			// Something inside this tile is inside the ray's depth window: look at the tiles inside it.
			level--;
			cellPixels /= tileScale;
			continue;
		}

		if (meets)
		{
			// The nearest surface this tile holds is the one the ray runs into. The view depth is
			// linear in lambda, so where the ray reaches it solves directly instead of being searched
			// for.
			float lambdaHit = lambda;
			if (rayFar > rayNear)
			{
				lambdaHit = lambda + (cellExit - lambda) * ((tileRange.x - rayNear) / (rayFar - rayNear));
			}

			lambdaHit          = clamp(lambdaHit, lambda, cellExit);
			vec3 hitViewPos    = viewPos + segment * lambdaHit;
			vec2 candidateUV   = ViewToUv(hitViewPos);
			float candidateDep = SceneLinearDepth(candidateUV);

			// The surface the ray reached has to face the ray. A grazing ray leaves its own surface at
			// a narrow angle and meets the depth of that same surface again along the row; the depth
			// test can not tell that apart from a crossing, the orientation can.
			if (candidateDep > 0.0)
			{
				vec2 refinedUV = candidateUV;
				float hitDepth = candidateDep;
				vec3 refinedPos = hitViewPos;

				vec3 hitNorm = GeometricNormal(refinedUV, hitDepth, SSR_HIT_NORMAL_STEP);
				if (dot(rayDir, hitNorm) < 0.0)
				{
					// Refine onto the plane of the surface at the hit pixel, built from a wider depth
					// neighbourhood than one texel so the stored depth quantization does not dominate
					// its orientation. The result is continuous in the ray parameters and exact where
					// the surface is flat, so the hit slides smoothly instead of snapping to the depth
					// texel grid. The crossing stays as the fallback for a plane that is degenerate or
					// that projects off screen; a poor plane shows up as a large miss and fades out
					// through the confidence below.
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
									refinedUV  = planeUV;
									hitDepth   = planeDepth;
									refinedPos = planePos;
								}
							}
						}
					}

					// How far the ray ended up from the surface it crossed.
					float miss         = length(refinedPos - ReconstructViewPos(refinedUV, hitDepth));
					float thicknessNow = max(ssrPass.params.z, pixelWorld * hitDepth * SSR_THICKNESS_PIXELS);
					float conf         = 1.0 - smoothstep(0.0, thicknessNow, miss);
					conf              *= conf;

					// The stored shading normal, kept for the frontal self hit rejection below.
					vec3 shadingNorm = normalize(mat3(ssrPass.view) * decodeNormal(texture(s_normalDepth, refinedUV).rg));
					bool frontal     = all(lessThan(abs(refinedUV - uv), ssrPass.screenParams.xy * 4.0));

					if (conf > 0.0 && !(frontal && dot(rayDir, shadingNorm) >= 0.0))
					{
						hitUV       = refinedUV;
						hitLambda   = lambdaHit;
						hitDistance = length(segment) * lambdaHit;
						hitError    = miss;
						confidence  = conf;
						break;
					}
				}
			}

			// The tile held something the ray's depth window meets, but what it reached was not a
			// surface it can reflect: step over this tile and stay at this level. The tiles next to it
			// are as likely to hold something as this one was, and climbing back up here would only
			// have the traversal descend again.
			lambda = cellExit;
			continue;
		}

		// Nothing the ray's depth window meets is inside this tile: step over the whole tile, and try a
		// coarser one next so a long ray does not have to walk the screen a tile at a time.
		lambda = cellExit;
		if (level < int(maxLevel))
		{
			level++;
			cellPixels *= tileScale;
		}
	}

	if (confidence <= 0.0)
	{
		// No screen space reflection found: the pixel keeps the forward shaded colour, and the filter
		// pass sees the zero weight. The march debug views stay readable: the level says how far the
		// pyramid was allowed to climb, the iteration count says whether the budget was the limit.
		if (DebugViewEnabled())
		{
			int mode = DebugViewMode();
			if (mode == SSR_DEBUG_HIZ_LEVEL)
			{
				fragColor = vec4(vec3(peakLevel / max(maxLevel, 1.0)), 1.0);
				return;
			}
			if (mode == SSR_DEBUG_ITERATIONS)
			{
				fragColor = vec4(vec3(iterations / stepCount), 1.0);
				return;
			}

			fragColor = vec4(0.0, 0.0, 0.0, 1.0);
			return;
		}

		fragColor = vec4(0.0);
		return;
	}

	// The reflection cone picks the mip, the mip chain does the blur.
	float rayUV     = length(hitUV - uv);
	float incidence = dot(normalize(viewPos), normal);
	float lod       = ReflectionLod(roughness, rayUV / max(ssrPass.screenParams.z, ssrPass.screenParams.w), incidence);
	vec3 reflection = SampleReflection(hitUV, lod);

	// Gloss, hit confidence, the screen border and the range limit all fade the reflection out, so the
	// forward shaded environment reflection takes over exactly where this one is not trustworthy.
	//
	// No fade on the incidence angle. A shallow surface is exactly where a reflection is wanted, and a
	// threshold there is a threshold on a camera dependent value: lowering the camera puts a large part
	// of a floor below it at once, and the reflection disappears in a single frame.
	float gloss     = 1.0 - roughness / max(ssrPass.params.w, 0.0001);
	float rangeFade = 1.0 - smoothstep(ssrPass.params.y * 0.6, ssrPass.params.y, hitDistance);
	float weight    = clamp(ssrPass.params.x * gloss * confidence * EdgeFade(hitUV) * rangeFade, 0.0, 1.0);

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
		if (mode == SSR_DEBUG_HIZ_LEVEL)
		{
			fragColor = vec4(vec3(peakLevel / max(maxLevel, 1.0)), 1.0);
			return;
		}
		if (mode == SSR_DEBUG_ITERATIONS)
		{
			fragColor = vec4(vec3(iterations / stepCount), 1.0);
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
