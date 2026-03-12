<shader>
	<type name = "includeShader" />
	<include name = "VSM.shader" />
	<include name = "textureUtil.shader" />
	<include name = "drawDataInc.shader" />
	<define name = "EVSM4" val="0,1" />
	<define name = "ShadowSampleCount" val="1,5,9,16" />
	<source>
	<!--
#ifndef SHADOW_SHADER
#define SHADOW_SHADER

// ---------------------------------------------------------------------------
// Shadow Atlas Lookup
// ---------------------------------------------------------------------------

/*
* Given a shadow map size and start coordinates, finds the queried shadow map's layer and start coordinates.
*/
void ShadowAtlasLut(in float size, in vec2 startCoord, in int queriedMap, out int layer, out vec2 targetCoord)
{
	float mapsPerRow = floor(SHADOW_ATLAS_SIZE / size);
	float mapsPerLayer = mapsPerRow * mapsPerRow;

	float mapIndex = float(queriedMap);

	layer = int(floor(mapIndex / mapsPerLayer));
	float indexInLayer = mapIndex - float(layer) * mapsPerLayer;

	float row = floor(indexInLayer / mapsPerRow);
	float col = indexInLayer - row * mapsPerRow;

	targetCoord = startCoord + vec2(col, row) * size;
}

// ---------------------------------------------------------------------------
// Filament-style EVSM evaluation
// Computes the shadow contribution from warped depth moments
// ---------------------------------------------------------------------------

float EvaluateEVSM
(
	vec2 positiveMoments,
	float positiveWarpedDepth,
	float shadowBias,
	float lightBleedReduction
#if EVSM4
	, vec2 negativeMoments
	, float negativeWarpedDepth
#endif
)
{
	// Filament: dynamic variance based on derivative of warped depth
	// dpw/dz = 2 * c * pw, where c = exponent, pw = warped depth
	// minVariance = epsilon * pw² + 0.25 * (dpw/dz)² * texelGradient²
	// Simplified: we fold texel gradient into the bias parameter
	const float EPSILON = 0.002;

	// Positive warp
	float pw = positiveWarpedDepth;
	float dpwdz = 2.0 * VsmExponent * pw;
	float posMinVariance = EPSILON * (pw * pw) + shadowBias * shadowBias * (dpwdz * dpwdz);
	float posContrib = ChebyshevUpperBound(positiveMoments, pw, posMinVariance, lightBleedReduction);

#if EVSM4
	// Negative warp
	float nw = negativeWarpedDepth;
	float dnwdz = 2.0 * EvsmExponents.y * nw;
	float negMinVariance = EPSILON * (nw * nw) + shadowBias * shadowBias * (dnwdz * dnwdz);
	float negContrib = ChebyshevUpperBound(negativeMoments, nw, negMinVariance, lightBleedReduction);
	return min(posContrib, negContrib);
#else
	return posContrib;
#endif
}

// ---------------------------------------------------------------------------
// 2D Shadow Sampling (Directional + Spot)
// ---------------------------------------------------------------------------

float PCFFilterShadow2D
(
	sampler2DArray shadowAtlas,
	vec3 uvLayer,
	vec2 coordStart,
	vec2 coordEnd,
	float radius,
	float currDepth,
	float LBR,
	float shadowBias
)
{
	float layerZ = uvLayer.z;

	// Gather filtered moments from shadow atlas
#if EVSM4
	vec4 momentAccum = vec4(0.0);
#else
	vec2 momentAccum = vec2(0.0);
#endif

	for (int i = 0; i < ShadowSampleCount; ++i)
	{
		vec2 offset = PoissonDisk[i % 16].xy * radius;
		vec2 sampleXY = ClampTextureCoordinates(uvLayer.xy + offset, coordStart, coordEnd);

#if EVSM4
		momentAccum += texture(shadowAtlas, vec3(sampleXY, layerZ));
#else
		momentAccum += texture(shadowAtlas, vec3(sampleXY, layerZ)).xy;
#endif
	}

	momentAccum *= (1.0 / float(ShadowSampleCount));

	// Warp the current depth to match stored moments
	vec2 warpedDepth = WarpDepth(currDepth, EvsmExponents);

	return EvaluateEVSM
	(
		momentAccum.xy,
		warpedDepth.x,
		shadowBias,
		LBR
#if EVSM4
		, momentAccum.zw
		, warpedDepth.y
#endif
	);
}

// ---------------------------------------------------------------------------
// Omnidirectional Shadow Sampling (Point lights)
// ---------------------------------------------------------------------------

float PCFFilterOmni
(
	sampler2DArray shadowAtlas,
	vec2 startCoord,
	float shadowAtlasResRatio,
	int shadowAtlasLayer,
	vec3 dir,
	float radius,
	float currDepth,
	float LBR,
	float shadowBias
)
{
	float halfPixel = (1.0 / SHADOW_ATLAS_SIZE) * 0.5;
	float shadowMapSize = shadowAtlasResRatio * SHADOW_ATLAS_SIZE;

#if EVSM4
	vec4 momentAccum = vec4(0.0);
#else
	vec2 momentAccum = vec2(0.0);
#endif

	for (int i = 0; i < ShadowSampleCount; ++i)
	{
		vec3 offset = PoissonDisk[i % 16] * radius;
		vec3 texCoord = UVWToUVLayer(dir + offset * 50.0);

		int face = int(texCoord.z);

		int layer = 0;
		vec2 coord = vec2(0.0);
		ShadowAtlasLut(shadowMapSize, startCoord, face, layer, coord);
		coord /= SHADOW_ATLAS_SIZE;

		layer += shadowAtlasLayer;

		vec2 beginCoord = coord;
		vec2 endCoord = beginCoord + shadowAtlasResRatio;

		texCoord.xy = beginCoord + (shadowAtlasResRatio * texCoord.xy);
		texCoord.z = float(layer);

		texCoord.xy = clamp(texCoord.xy, beginCoord + halfPixel, endCoord - halfPixel);

#if EVSM4
		momentAccum += texture(shadowAtlas, texCoord);
#else
		momentAccum += texture(shadowAtlas, texCoord).xy;
#endif
	}

	momentAccum *= (1.0 / float(ShadowSampleCount));

	vec2 warpedDepth = WarpDepth(currDepth, EvsmExponents);

	return EvaluateEVSM
	(
		momentAccum.xy,
		warpedDepth.x,
		shadowBias,
		LBR
#if EVSM4
		, momentAccum.zw
		, warpedDepth.y
#endif
	);
}

#endif
	-->
	</source>
</shader>