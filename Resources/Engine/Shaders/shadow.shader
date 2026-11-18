<shader>
	<type name = "includeShader" />
	<include name = "VSM.shader" />
	<include name = "textureUtil.shader" />
	<include name = "drawDataInc.shader" />
	<define name = "ShadowPCF" val="0,4,9,16" />
	<define name = "CastShadow" val="0,1" />
	<source>
	<!--
#ifndef SHADOW_SHADER
#define SHADOW_SHADER

// ShadowSettings::CastShadow arrives as a define so the whole sampling path can be compiled out
// when the scene casts no shadow. A caller that does not set it keeps the old behaviour.
#ifndef CastShadow
	#define CastShadow 1
#endif

#define TWO_PI 6.283185

// ---------------------------------------------------------------------------
// Interleaved Gradient Noise (Jorge Jimenez, 2014)
// Deterministic screen-space noise to break shadow banding.
// ---------------------------------------------------------------------------

float InterleavedGradientNoise(vec2 screenPos)
{
	vec3 magic = vec3(0.06711056, 0.00583715, 52.9829189);
	return fract(magic.z * fract(dot(screenPos, magic.xy)));
}

vec2 ShadowDitherJitter(float texelSize)
{
	float noise = InterleavedGradientNoise(gl_FragCoord.xy);
	float angle = noise * TWO_PI;
	return vec2(cos(angle), sin(angle)) * texelSize * noise;
}

// ---------------------------------------------------------------------------
// Shadow Atlas Lookup
// ---------------------------------------------------------------------------

void ShadowAtlasLut(in float size, in vec2 startCoord, in int queriedMap, out int layer, out vec2 targetCoord)
{
	float mapsPerRow = floor(graphicConstants.shadowAtlasSize / size);
	float mapsPerLayer = mapsPerRow * mapsPerRow;

	float mapIndex = float( queriedMap );

	layer = int( floor( mapIndex / mapsPerLayer ) );
	float indexInLayer = mapIndex - float(layer) * mapsPerLayer;

	float row = floor(indexInLayer / mapsPerRow);
	float col = indexInLayer - row * mapsPerRow;

	targetCoord = startCoord + vec2(col, row) * size;
}

// ---------------------------------------------------------------------------
// Filament-style EVSM evaluation
// ---------------------------------------------------------------------------

float EvaluateEVSM
(
	vec2 positiveMoments,
	float positiveWarpedDepth,
	float shadowBias,
	float lightBleedReduction
)
{
	// Filament-style EVSM evaluation (2-component, positive warp only)
	float pw = positiveWarpedDepth;
	float posMinVariance = shadowBias * (pw * pw);
	float posContrib = ChebyshevUpperBound(positiveMoments, pw, posMinVariance, lightBleedReduction);

	return posContrib;
}

// ---------------------------------------------------------------------------
// Bilinear PCF tap helpers
// Each texture() with linear filtering already interpolates 2x2 texels.
// By placing samples at sub-texel offsets we cover larger effective kernels.
//
//  4 samples -> ~3x3 kernel  (4 bilinear taps at +-0.5 texel)
//  9 samples -> ~5x5 kernel  (9 bilinear taps, center + 4 edges + 4 corners)
// 16 samples -> ~7x7 kernel  (16 bilinear taps in a 4x4 grid)
//
// All loops are manually unrolled for GPU performance.
// ---------------------------------------------------------------------------

#if ShadowPCF >= 4
vec2 ShadowPCFFilter(sampler2DArray atlas, vec3 uvLayer, vec2 coordStart, vec2 coordEnd, float texelSize)
{
	vec2 result = vec2(0.0);

#define SHADOW_TAP(ox, oy) texture(atlas, vec3(clamp(uvLayer.xy + vec2(ox, oy) * texelSize, coordStart, coordEnd), uvLayer.z)).xy

#if ShadowPCF == 4
	// 4 bilinear taps at +-0.5 texel offsets -> effective ~3x3 kernel
	result += SHADOW_TAP(-0.5, -0.5);
	result += SHADOW_TAP( 0.5, -0.5);
	result += SHADOW_TAP(-0.5,  0.5);
	result += SHADOW_TAP( 0.5,  0.5);
	result *= 0.25;

#elif ShadowPCF == 9
	// 9 bilinear taps -> effective ~5x5 Gaussian approximation
	// 4 corners at +-1.2 (weighted center of 2x2 blocks)
	// 4 edges at +-1.2 on one axis, 0 on the other
	// 1 center tap
	// Weights: corners 25/256 each, edges 30/256 each, center 36/256
	result += SHADOW_TAP(-1.2, -1.2) * 25.0;
	result += SHADOW_TAP( 1.2, -1.2) * 25.0;
	result += SHADOW_TAP(-1.2,  1.2) * 25.0;
	result += SHADOW_TAP( 1.2,  1.2) * 25.0;
	result += SHADOW_TAP( 0.0, -1.2) * 30.0;
	result += SHADOW_TAP( 0.0,  1.2) * 30.0;
	result += SHADOW_TAP(-1.2,  0.0) * 30.0;
	result += SHADOW_TAP( 1.2,  0.0) * 30.0;
	result += SHADOW_TAP( 0.0,  0.0) * 36.0;
	result *= (1.0 / 256.0);

#elif ShadowPCF == 16
	// 16 bilinear taps in a 4x4 grid -> effective ~7x7 kernel
	// Sub-texel offsets at +-0.5 and +-1.5 texels
	result += SHADOW_TAP(-1.5, -1.5);
	result += SHADOW_TAP(-0.5, -1.5);
	result += SHADOW_TAP( 0.5, -1.5);
	result += SHADOW_TAP( 1.5, -1.5);
	result += SHADOW_TAP(-1.5, -0.5);
	result += SHADOW_TAP(-0.5, -0.5);
	result += SHADOW_TAP( 0.5, -0.5);
	result += SHADOW_TAP( 1.5, -0.5);
	result += SHADOW_TAP(-1.5,  0.5);
	result += SHADOW_TAP(-0.5,  0.5);
	result += SHADOW_TAP( 0.5,  0.5);
	result += SHADOW_TAP( 1.5,  0.5);
	result += SHADOW_TAP(-1.5,  1.5);
	result += SHADOW_TAP(-0.5,  1.5);
	result += SHADOW_TAP( 0.5,  1.5);
	result += SHADOW_TAP( 1.5,  1.5);
	result *= (1.0 / 16.0);
#endif

#undef SHADOW_TAP

	return result;
}
#endif

// ---------------------------------------------------------------------------
// 2D Shadow Sampling (Directional + Spot)
// ---------------------------------------------------------------------------

float SampleShadow2D
(
	sampler2DArray shadowAtlas,
	vec3 uvLayer,
	vec2 coordStart,
	vec2 coordEnd,
	float texelSize,
	float currDepth,
	float LBR,
	float shadowBias
)
{
	float halfPixel = (1.0 / graphicConstants.shadowAtlasSize) * 0.5;
	vec2 clampMin = coordStart + halfPixel;
	vec2 clampMax = coordEnd - halfPixel;

	vec2 jitter = ShadowDitherJitter(texelSize);
	vec3 jitteredUV = vec3(clamp(uvLayer.xy + jitter, clampMin, clampMax), uvLayer.z);

#if CastShadow
	#if ShadowPCF >= 4
	vec2 moments = ShadowPCFFilter(shadowAtlas, jitteredUV, clampMin, clampMax, texelSize);
	#else
	vec2 moments = texture(shadowAtlas, jitteredUV).xy;
	#endif

	float warpedDepth = WarpDepth(currDepth);

	return EvaluateEVSM
	(
		moments.xy,
		warpedDepth,
		shadowBias,
		LBR
	);
#else
	// No shadow in this frame: nothing is sampled and the term is fully lit.
	return 1.0;
#endif
}

// Omnidirectional Shadow Sampling (Point lights)
//
// A point light renders its shadow map in one of two layouts, chosen per light and carried in
// shadowMode, so this function has to know both:
//
//   Dual Paraboloid  one map per hemisphere, two atlas slots. The scene is projected onto a
//                    paraboloid, radii divided by ParaboloidRimInv so the hemisphere sits inside
//                    about 87 percent of the map and the stretched rim is never read. A third of
//                    the atlas a cube needs, and the default.
//   Cube Map         six maps, one per cube face, six atlas slots. Sharper - no rim to crop - and
//                    the layout a light falls back to when it needs the quality.
//
// The two agree on everything after the direction is turned into (uv inside the map, slot offset
// inside the light's run), which is where they are joined below.
// ---------------------------------------------------------------------------

// Radii are divided by this before projecting, so the hemisphere occupies about 87 percent of the map
// and the rim, where the projection stretches without bound, stays outside the part that is sampled.
const float ParaboloidRimInv = 0.98039216;

// Matches PointShadowMode in EngineSettings.h. The project setting and a light's own override are
// resolved on the CPU and arrive here as LightCacheItem::CommonData::shadowMode.
const int PointShadowCubeMap = 1;

float SampleShadowOmni
(
	sampler2DArray shadowAtlas,
	vec2 startCoord,
	float shadowAtlasResRatio,
	int shadowAtlasLayer,
	vec3 dir,
	int shadowMode,
	float texelSize,
	float currDepth,
	float LBR,
	float shadowBias
)
{
	float halfPixel = (1.0 / graphicConstants.shadowAtlasSize) * 0.5;
	float shadowMapSize = shadowAtlasResRatio * graphicConstants.shadowAtlasSize;

	vec3 d = normalize(dir);

	// Where inside its map this direction lands, and which map of the light's run holds it.
	vec2 uvInMap;
	int slotOffset;

	if (shadowMode == PointShadowCubeMap)
	{
		// One map per face: UVWToUVLayer picks the dominant axis of the direction and returns the uv
		// to read with the face in z.
		vec3 faceUv = UVWToUVLayer(d);
		#ifdef VULKAN
		// Vulkan stores V=0 at the top, the negative viewport height trick flips the rendered image
		// relative to OpenGL, and UVWToUVLayer answers in the OpenGL convention.
		faceUv.y = 1.0 - faceUv.y;
		#endif

		uvInMap = faceUv.xy;
		slotOffset = int(faceUv.z);
	}
	else
	{
		// Paraboloid: the render side puts the camera at the light looking along -Z and turns it half a
		// turn about Y for the other half, so the covered hemisphere always has a negative z in that
		// map's view space. This mirrors that.
		//
		// The engine camera looks along +Z when it has no rotation, and its view flips z, so slot 0 -
		// drawn with no rotation - holds the half where the world z of the direction is positive, with z
		// flipped. Slot 1 is drawn with the camera turned half a turn about Y, which flips x instead.
		// Getting this backwards leaves x and y looking right while the shadow follows the light along
		// z, because the wrong half only mirrors z.
		int side = d.z >= 0.0 ? 0 : 1;
		vec3 v = side == 0 ? vec3(d.x, d.y, -d.z) : vec3(-d.x, d.y, d.z);

		// Same projection as the shadow vertex shader, rim scale included.
		//
		// The rows are read back mirrored: the shadow pass writes the map through the light's camera
		// view, whose vertical axis is the reverse of the one this sampler builds its direction in. The
		// cube path above compensates for the same reversal, but on Vulkan only, and the two layouts
		// cannot share the correction: without it here the shadow follows the light when the light is
		// moved up or down instead of sliding the other way along the wall.
		float denom = max(1.0 - v.z, 1e-4);
		vec2 proj = (v.xy / denom) * ParaboloidRimInv;
		uvInMap = proj * 0.5 + 0.5;
		uvInMap.y = 1.0 - uvInMap.y;

		slotOffset = side;
	}

	// Face 0's slot index from its pixel coordinate, then the absolute atlas coordinate of
	// (baseIndex + slotOffset): a light's maps are one run of consecutive slots.
	float mapsPerRow = floor(graphicConstants.shadowAtlasSize / shadowMapSize);
	int baseIndex = int(startCoord.y / shadowMapSize) * int(mapsPerRow) + int(startCoord.x / shadowMapSize);

	int layer = 0;
	vec2 coord = vec2(0.0);
	ShadowAtlasLut(shadowMapSize, vec2(0.0), baseIndex + slotOffset, layer, coord);
	coord /= graphicConstants.shadowAtlasSize;

	layer += shadowAtlasLayer;

	vec2 beginCoord = coord;
	vec2 endCoord = beginCoord + shadowAtlasResRatio;

	vec2 texCoord = beginCoord + (shadowAtlasResRatio * uvInMap);
	texCoord = clamp(texCoord, beginCoord + halfPixel, endCoord - halfPixel);

	vec2 jitter = ShadowDitherJitter(texelSize);
	vec2 jitteredXY = clamp(texCoord + jitter, beginCoord + halfPixel, endCoord - halfPixel);

	vec3 sampleCoord = vec3(jitteredXY, float(layer));

#if CastShadow
	#if ShadowPCF >= 4
	vec2 moments = ShadowPCFFilter(shadowAtlas, sampleCoord, beginCoord + halfPixel, endCoord - halfPixel, texelSize);
	#else
	vec2 moments = texture(shadowAtlas, sampleCoord).xy;
	#endif

	float warpedDepth = WarpDepth(currDepth);

	return EvaluateEVSM
	(
		moments.xy,
		warpedDepth,
		shadowBias,
		LBR
	);
#else
	// No shadow in this frame: nothing is sampled and the term is fully lit.
	return 1.0;
#endif
}

#endif
	-->
	</source>
</shader>
