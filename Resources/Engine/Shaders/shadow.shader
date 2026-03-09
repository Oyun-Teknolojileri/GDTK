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
	#if EVSM4
		vec4 occuluderAverage = vec4(0.0);
	#else
		vec2 occuluderAverage = vec2(0.0);
	#endif

	float layerZ = uvLayer.z;

	for (int i = 0; i < ShadowSampleCount; ++i)
	{
		vec2 offset = PoissonDisk[i % 16].xy * radius;

		vec2 sampleXY = ClampTextureCoordinates(uvLayer.xy + offset, coordStart, coordEnd);

		#if EVSM4
			occuluderAverage += texture(shadowAtlas, vec3(sampleXY, layerZ));
		#else
			occuluderAverage += texture(shadowAtlas, vec3(sampleXY, layerZ)).xy;
		#endif
	}

	occuluderAverage *= (1.0 / float(ShadowSampleCount));

	vec2 warpedDepth = WarpDepth(currDepth, EvsmExponents);

	vec2 depthScale = 100.0 * shadowBias * EvsmExponents * warpedDepth;
	vec2 minVariance = depthScale * depthScale;

	#if EVSM4
		float posContrib = ChebyshevUpperBound(occuluderAverage.xz, warpedDepth.x, minVariance.x, LBR);
		float negContrib = ChebyshevUpperBound(occuluderAverage.yw, warpedDepth.y, minVariance.y, LBR);
		return min(posContrib, negContrib);
	#else
		return ChebyshevUpperBound(occuluderAverage, warpedDepth.x, minVariance.x, LBR);
	#endif
}

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
		vec4 occuluderAverage = vec4(0.0);
	#else
		vec2 occuluderAverage = vec2(0.0);
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
			occuluderAverage += texture(shadowAtlas, texCoord);
		#else
			occuluderAverage += texture(shadowAtlas, texCoord).xy;
		#endif
	}

	occuluderAverage *= (1.0 / float(ShadowSampleCount));

	vec2 warpedDepth = WarpDepth(currDepth, EvsmExponents);

	vec2 depthScale = 100.0 * shadowBias * EvsmExponents * warpedDepth;
	vec2 minVariance = depthScale * depthScale;

	#if EVSM4
		float posContrib = ChebyshevUpperBound(occuluderAverage.xz, warpedDepth.x, minVariance.x, LBR);
		float negContrib = ChebyshevUpperBound(occuluderAverage.yw, warpedDepth.y, minVariance.y, LBR);
		return min(posContrib, negContrib);
	#else
		return ChebyshevUpperBound(occuluderAverage, warpedDepth.x, minVariance.x, LBR);
	#endif
}

#endif
	-->
	</source>
</shader>