<shader>
	<type name = "includeShader" />
	<include name = "VSM.shader" />
	<include name = "textureUtil.shader" />
	<include name = "drawDataInc.shader" />
	<define name = "ShadowPCF" val="0,4,9,16" />
	<source>
	<!--
#ifndef SHADOW_SHADER
#define SHADOW_SHADER

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
	const float EPSILON = 0.002;

	float pw = positiveWarpedDepth;
	float dpwdz = 2.0 * VsmExponent * pw;
	float posMinVariance = EPSILON * (pw * pw) + shadowBias * shadowBias * (dpwdz * dpwdz);
	float posContrib = ChebyshevUpperBound(positiveMoments, pw, posMinVariance, lightBleedReduction);

	return posContrib;
}

// ---------------------------------------------------------------------------
// Bilinear PCF tap helpers
// Each texture() with linear filtering already interpolates 2x2 texels.
// By placing samles at sub-texel offsets we cover larger effective kernels.
//
//  4 samles -> ~3x3 kernel
//  9 samles -> ~5x5 kernel
// 16 samles -> ~7x7 kernel
// ---------------------------------------------------------------------------

#if ShadowPCF >= 4
vec2 ShadowPCFFilter(sampler2DArray atlas, vec3 uvLayer, vec2 coordStart, vec2 coordEnd, float texelSize)
{
	vec2 result = vec2(0.0);

#if ShadowPCF == 4
	vec2 offset = vec2(0.5) * texelSize;
	result += texture(atlas, vec3(clamp(uvLayer.xy + vec2(-offset.x,  offset.y), coordStart, coordEnd), uvLayer.z)).xy;
	result += texture(atlas, vec3(clamp(uvLayer.xy + vec2( offset.x,  offset.y), coordStart, coordEnd), uvLayer.z)).xy;
	result += texture(atlas, vec3(clamp(uvLayer.xy + vec2(-offset.x, -offset.y), coordStart, coordEnd), uvLayer.z)).xy;
	result += texture(atlas, vec3(clamp(uvLayer.xy + vec2( offset.x, -offset.y), coordStart, coordEnd), uvLayer.z)).xy;
	result *= 0.25;

#elif ShadowPCF == 9
	for (int y = -1; y <= 1; y++)
	{
		for (int x = -1; x <= 1; x++)
		{
			vec2 off = vec2(float(x), float(y)) * texelSize;
			result += texture(atlas, vec3(clamp(uvLayer.xy + off, coordStart, coordEnd), uvLayer.z)).xy;
		}
	}
	result /= 9.0;

#elif ShadowPCF == 16
	for (int y = 0; y < 4; y++)
	{
		for (int x = 0; x < 4; x++)
		{
			vec2 off = (vec2(float(x), float(y)) - 1.5) * texelSize;
			result += texture(atlas, vec3(clamp(uvLayer.xy + off, coordStart, coordEnd), uvLayer.z)).xy;
		}
	}
	result *= (1.0 / 16.0);
#endif

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
}

// ---------------------------------------------------------------------------
// Omnidirectional Shadow Sampling (Point lights)
// ---------------------------------------------------------------------------

float SampleShadowOmni
(
	sampler2DArray shadowAtlas,
	vec2 startCoord,
	float shadowAtlasResRatio,
	int shadowAtlasLayer,
	vec3 dir,
	float texelSize,
	float currDepth,
	float LBR,
	float shadowBias
)
{
	float halfPixel = (1.0 / graphicConstants.shadowAtlasSize) * 0.5;
	float shadowMapSize = shadowAtlasResRatio * graphicConstants.shadowAtlasSize;

	vec3 texCoord = UVWToUVLayer(dir);
	int face = int(texCoord.z);

	// Derive face 0's global slot index from its pixel coordinate,
	// then look up the absolute atlas coordinate for (baseIndex + face).
	float mapsPerRow = floor(graphicConstants.shadowAtlasSize / shadowMapSize);
	int baseIndex = int(startCoord.y / shadowMapSize) * int(mapsPerRow) + int(startCoord.x / shadowMapSize);

	int layer = 0;
	vec2 coord = vec2(0.0);
	ShadowAtlasLut(shadowMapSize, vec2(0.0), baseIndex + face, layer, coord);
	coord /= graphicConstants.shadowAtlasSize;

	layer += shadowAtlasLayer;

	vec2 beginCoord = coord;
	vec2 endCoord = beginCoord + shadowAtlasResRatio;

	texCoord.xy = beginCoord + (shadowAtlasResRatio * texCoord.xy);
	texCoord.xy = clamp(texCoord.xy, beginCoord + halfPixel, endCoord - halfPixel);

	vec2 jitter = ShadowDitherJitter(texelSize);
	vec2 jitteredXY = clamp(texCoord.xy + jitter, beginCoord + halfPixel, endCoord - halfPixel);

	vec3 sampleCoord = vec3(jitteredXY, float(layer));

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
}

#endif
	-->
	</source>
</shader>
