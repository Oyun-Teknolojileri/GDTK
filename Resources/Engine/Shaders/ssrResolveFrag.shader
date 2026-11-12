<shader>
	<type name = "fragmentShader" />
	<include name = "vulkanCompatInc.shader" />
	<include name = "ssrCommonInc.shader" />
	<include name = "normalEncodingInc.shader" />
	<texture slot = "0" name = "s_trace" />
	<texture slot = "1" name = "s_avgRadiance" />
	<texture slot = "2" name = "s_history" />
	<texture slot = "3" name = "s_historyCount" />
	<texture slot = "4" name = "s_normalDepth" />
	<texture slot = "5" name = "s_normalDepthPrev" />
	<texture slot = "6" name = "s_hit" />
	<texture slot = "7" name = "s_velCur" />
	<texture slot = "8" name = "s_velPrev" />
	<source>
	<!--

precision highp float;
precision highp int;

TK_LOC(2) in vec2 v_texture;

// rgb: the accumulated reflection, a: its variance. The composite reads the rgb, the temporal resolve of
// the next frame reads both.
layout (location = 0) out vec4 fragColor;

// r: how many samples the accumulation holds for this pixel. It decides how much of a new frame comes
// through, in place of a fixed response.
layout (location = 1) out vec4 fragCount;

TK_SAMPLER_BINDING(0) uniform sampler2D s_trace;           // rgb: this frame's reflection, a: coverage.
TK_SAMPLER_BINDING(1) uniform sampler2D s_avgRadiance;     // 1/8 resolution average of the trace.
TK_SAMPLER_BINDING(2) uniform sampler2D s_history;         // rgb: accumulated reflection, a: variance.
TK_SAMPLER_BINDING(3) uniform sampler2D s_historyCount;    // r: accumulated sample count.
TK_SAMPLER_BINDING(4) uniform sampler2D s_normalDepth;     // rg: world normal, b: depth, a: roughness.
TK_SAMPLER_BINDING(5) uniform sampler2D s_normalDepthPrev; // The same g buffer, one frame ago.
TK_SAMPLER_BINDING(6) uniform sampler2D s_hit;             // xy: ray end uv, z: its depth, w: ray length.
TK_SAMPLER_BINDING(7) uniform sampler2D s_velCur;          // xy: this frame's screen position of the object.
TK_SAMPLER_BINDING(8) uniform sampler2D s_velPrev;         // xy: the previous frame's screen position of it.

// Samples the accumulation may hold, as a share of the configured maximum. A rough surface needs many
// samples to settle and a smooth one settles in a couple, so the cap follows the roughness: without it a
// rough surface carries a long history it can not use, and a smooth one takes too long to converge.
#define SSR_SAMPLES_FOR_ROUGHNESS(r) (1.0 - exp(-(r) * 100.0))

// Below this roughness the reflection is the mirror direction, sampled deterministically. There is
// nothing to average out, so the pixel takes this frame's sample as it is: that is what keeps a mirror
// free of both shimmer and ghosting, and it is the reference's mirror/glossy split.
#define SSR_MIRROR_ROUGHNESS 0.0001

// The spatial filter's window: the first few Halton(2,3) offsets stretched to +-3 texels, with the centre
// left out because it is accounted for separately. The reference filters with the same taps.
#define SSR_FILTER_TAPS 15

// Multiplier on the standard deviation of the local statistics. The reference uses 1.4; below 1 the clip
// is so tight that the accumulation can never hold anything this frame did not produce.
#define SSR_CLIP_SCALE 1.4

// Share of the 1/8 resolution average in the clipped box. It carries the low frequency the local window
// can not see, so a neighbourhood that happens to agree on a wrong value is still pulled back.
#define SSR_AVG_RADIANCE_WEIGHT 0.2

// Variance assumed for a pixel with no usable history: high enough that the filter trusts a single sample
// less, low enough that it does not reject the frame outright.
#define SSR_DEFAULT_VARIANCE 1.0

// How much of the accumulated history a pixel keeps when its reprojection matches perfectly. It trades
// convergence against how fast the reflection follows a change: 0.98 is a fifty frame equivalent, which is
// what a single sampled ray needs before it stops crawling.
#define SSR_HISTORY_RESPONSE_MAX 0.98

const vec2 HALTON_OFFSETS[SSR_FILTER_TAPS] = vec2[SSR_FILTER_TAPS](
	vec2(0.0, 1.0), vec2(-2.0, 1.0), vec2(2.0, -3.0), vec2(-3.0, 0.0), vec2(1.0, 2.0),
	vec2(-1.0, -2.0), vec2(3.0, 0.0), vec2(-3.0, 3.0), vec2(0.0, -3.0), vec2(-1.0, -1.0),
	vec2(2.0, 1.0), vec2(-2.0, -2.0), vec2(1.0, 0.0), vec2(0.0, 2.0), vec2(3.0, -1.0));

float Luminance(vec3 c)
{
	return max(dot(c, vec3(0.299, 0.587, 0.114)), 0.001);
}

vec3 WorldNormal(vec4 gBuffer)
{
	return normalize(mat3(ssrPass.view) * decodeNormal(gBuffer.rg));
}

// How much a neighbouring pixel is the same surface. The normal has to agree and the depth has to be
// within what a hole of this distance spans; both terms are ratios, so nothing is tuned per scene scale.
float SurfaceWeight(vec4 center, vec4 neighbour)
{
	if (neighbour.b <= 0.0)
	{
		return 0.0;
	}

	vec3  normal      = WorldNormal(center);
	vec3  otherNormal = WorldNormal(neighbour);
	float normalTerm  = pow(max(dot(normal, otherNormal), 0.0), 128.0);
	float depthTerm   = exp(-abs(center.b - neighbour.b) / max(center.b, 0.001) * 8.0);

	return normalTerm * depthTerm;
}

// Distance between two radiance values. The exponential bounds a firefly's vote instead of letting it
// drag the average on its own.
float RadianceWeight(vec3 center, vec3 other, float variance)
{
	return max(exp(-(0.6 + variance * 0.1) * length(center - other)), 0.01);
}

// Pulls a value into the box around `centre` with the given extent, along the line to the centre. Used to
// clip the history into what this frame produced.
vec3 ClipAabb(vec3 centre, vec3 extent, vec3 value)
{
	vec3  offset = value - centre;
	vec3  unit   = offset / extent;
	float reach  = max(abs(unit.x), max(abs(unit.y), abs(unit.z)));

	return reach > 1.0 ? centre + offset / reach : value;
}

void main()
{
	vec2  uv    = v_texture;
	vec4  gBuf  = texture(s_normalDepth, uv);
	float depth = gBuf.b;

	fragColor = vec4(0.0);
	fragCount = vec4(0.0);

	// Not a reflective surface: nothing to accumulate, and the composite leaves the pixel to the forward
	// shading.
	if (depth <= 0.0 || gBuf.a >= ssrPass.params.w)
	{
		return;
	}

	float roughness = gBuf.a;
	vec4  center    = texture(s_trace, uv);

	// A mirror is sampled along the exact mirror direction, so there is no stochastic noise to accumulate
	// and nothing to ghost: keeping a history for it is what put bands on a moving mirror. It is filtered
	// in screen space only, which is what removes the single pixel hit/miss speckle a mirror shows at a
	// grazing silhouette while leaving the reflection itself sharp.
	bool isMirror = roughness < SSR_MIRROR_ROUGHNESS;

	// --- reprojection --------------------------------------------------------------------------------
	// Surface candidate: where this pixel was last frame, plus the object's own motion.
	vec3 viewPos     = SsrViewPos(uv, depth);
	vec4 worldPos    = ssrPass.inverseView * vec4(viewPos, 1.0);
	vec4 prevClip    = ssrPass.prevViewProjection * worldPos;
	vec2 surfacePrevUV = prevClip.xy / max(abs(prevClip.w), 1e-6) * 0.5 + 0.5;
#ifdef VULKAN
	surfacePrevUV.y = 1.0 - surfacePrevUV.y;
#endif

	if (ssrPass.velocity.x > 0.5)
	{
		surfacePrevUV -= texture(s_velCur, uv).xy - texture(s_velPrev, uv).xy;
	}

	// Hit candidate: the point the ray ended at, projected with the previous frame's view projection. A
	// miss ends at the last position the march reached, so it has a candidate too - the environment sample
	// stands along that direction.
	vec4  hitSample = texture(s_hit, uv);
	vec2  hitPrevUV = surfacePrevUV;
	vec4  hitPrev   = vec4(0.0);
	bool  hitValid  = false;
	if (hitSample.z > 0.0)
	{
		vec3 hitView  = SsrViewPos(hitSample.xy, hitSample.z);
		vec4 hitWorld = ssrPass.inverseView * vec4(hitView, 1.0);
		vec4 hitClip  = ssrPass.prevViewProjection * hitWorld;
		hitPrevUV     = hitClip.xy / max(abs(hitClip.w), 1e-6) * 0.5 + 0.5;
#ifdef VULKAN
		hitPrevUV.y = 1.0 - hitPrevUV.y;
#endif
		hitPrev  = texture(s_normalDepthPrev, clamp(hitPrevUV, vec2(0.0), vec2(1.0)));
		hitValid = hitClip.w > 0.0 && all(greaterThan(hitPrevUV, vec2(0.0))) &&
		           all(lessThan(hitPrevUV, vec2(1.0)));
	}

	// Which candidate the history agrees with. The surface candidate is the accumulator's own pixel, so it
	// wins unless the hit candidate lands on a surface that is clearly the same thing - which is what
	// carries the parallax of a reflection whose content moved across a still surface.
	vec2 chosenUV   = surfacePrevUV;
	vec4 chosenPrev = texture(s_normalDepthPrev, clamp(surfacePrevUV, vec2(0.0), vec2(1.0)));

	if (hitValid)
	{
		vec3  normal   = WorldNormal(gBuf);
		float hitSim   = dot(normal, WorldNormal(hitPrev));
		float surfSim  = dot(normal, WorldNormal(chosenPrev));

		if (hitSim > 0.9999 && hitSim + 1.0e-3 > surfSim &&
		    abs(hitPrev.a - roughness) < abs(chosenPrev.a - roughness) + 1.0e-3)
		{
			chosenUV   = hitPrevUV;
			chosenPrev = hitPrev;
		}
	}

	// Disocclusion: how much the surface the history lands on is the surface this pixel is now. Both
	// terms are smooth, so a mismatch weights the history down instead of cutting it. A reset (a different
	// camera, a resize, the first frame) takes the history out of the picture for this frame.
	bool  inside       = all(greaterThan(chosenUV, vec2(0.0))) && all(lessThan(chosenUV, vec2(1.0)));
	float disocclusion = 0.0;
	if (ssrPass.temporal.w > 0.5)
	{
		inside = false;
	}

	if (inside && chosenPrev.b > 0.0)
	{
		vec3  normal       = WorldNormal(gBuf);
		vec3  normalPrev   = WorldNormal(chosenPrev);
		float normalTerm   = exp(-abs(1.0 - max(0.0, dot(normal, normalPrev))) * 1.4);
		float depthTerm    = exp(-abs(chosenPrev.b - depth) / max(depth, 0.001));
		disocclusion       = normalTerm * depthTerm;
	}

	// The history is read every frame and the disocclusion factor scales how fast it is replaced, instead of
	// deciding whether it may be read at all. With the threshold the factor is a switch: a pixel whose
	// factor sits around it drops its whole accumulation one frame and starts again from one sample the
	// next, and since one sample per frame is all a stochastic ray has, that showed as a reflection that
	// crawls. Scaled, a pixel converges to 1 / (1 - disocclusion) samples: a good match walks up to the cap,
	// a poor one slides down to a single frame, and nothing is discontinuous in between - which is what the
	// reference's own counter does, its threshold applies to a different decision.
	float cap      = max(8.0, ssrPass.denoiser.x * SSR_SAMPLES_FOR_ROUGHNESS(roughness));
	vec2  clampedUV = clamp(chosenUV, vec2(0.0), vec2(1.0));
	vec4  history   = texture(s_history, clampedUV);
	float count     = clamp(texture(s_historyCount, clampedUV).r * disocclusion + 1.0, 1.0, cap);
	float variance  = mix(SSR_DEFAULT_VARIANCE, history.a, clamp(disocclusion, 0.0, 1.0));

	// --- spatial filter of this frame's sample --------------------------------------------------------
	vec2 texel        = ssrPass.screenParams.xy;
	vec3 avgRadiance  = texture(s_avgRadiance, uv).rgb;

	float weightSum   = RadianceWeight(avgRadiance, center.rgb, variance);
	vec3  filtered    = center.rgb * weightSum;
	float squareSum   = dot(center.rgb, center.rgb) * weightSum;

	for (int i = 0; i < SSR_FILTER_TAPS; ++i)
	{
		// The quality setting caps the taps. The window is a fixed table, so a lower budget keeps the
		// near taps in the same order instead of reshuffling the pattern.
		if (float(i) >= ssrPass.quality.x)
		{
			break;
		}

		vec2 tapUV = uv + HALTON_OFFSETS[i] * texel;
		vec4 tap   = texture(s_normalDepth, tapUV);

		float weight = SurfaceWeight(gBuf, tap);
		if (weight <= 0.0)
		{
			continue;
		}

		vec4 tapTrace = texture(s_trace, tapUV);
		weight       *= RadianceWeight(avgRadiance, tapTrace.rgb, variance);

		weightSum += weight;
		filtered  += weight * tapTrace.rgb;
		squareSum += dot(tapTrace.rgb, tapTrace.rgb) * weight;
	}

	vec3  localMean     = filtered / max(weightSum, 1e-4);
	float localVariance = max(squareSum / max(weightSum, 1e-4) - dot(localMean, localMean), 0.0);
	vec3  newSignal     = localMean;

	// --- variance clipping and blend -----------------------------------------------------------------
	// A mirror has nothing to average and nothing to ghost: it takes this frame's sample as it is. The
	// filter above is built for the stochastic lobe and would smear a mirror reflection away.
	if (isMirror)
	{
		fragColor = vec4(center.rgb, 0.0);
		fragCount = vec4(1.0, 0.0, 0.0, 0.0);
		return;
	}

	// The local statistics bound what the history may contribute, widened towards the frame's average
	// radiance so the low frequency the window can not see is not clipped away. The mean leans the same
	// way, which keeps a pixel that sits next to a large change from holding its old value.
	vec3  mean     = mix(localMean, avgRadiance, SSR_AVG_RADIANCE_WEIGHT);
	vec3  colorStd = (vec3(sqrt(localVariance)) + vec3(length(localMean - avgRadiance))) *
	                 max(ssrPass.denoiser.z, 0.0) * SSR_CLIP_SCALE;
	vec3  extent   = max(colorStd, vec3(1e-4));

	// The history is clipped into that box, the new sample is clipped and pulled towards the average by how
	// few samples the pixel holds, and the two are blended by the sample count.
	vec3 clippedHistory = ClipAabb(mean, extent, history.rgb);

	newSignal = mix(newSignal, avgRadiance, 1.0 / max(count + 1.0, 1.0));
	newSignal = ClipAabb(avgRadiance, extent, newSignal);

	// How much of the history the result keeps, from the disocclusion factor itself. The counter is written
	// for the debug view but not trusted with the blend: it is a value read back from a target, and a target
	// that reads as one sample while it holds thirty cannot be told apart from a rejected history from here,
	// whereas the factor is computed from this frame's own g buffer.
	float blend  = mix(0.0, SSR_HISTORY_RESPONSE_MAX, smoothstep(0.0, 1.0, clamp(disocclusion, 0.0, 1.0)));
	vec3  result = mix(newSignal, clippedHistory, blend);

	// Temporal variance: how far the accumulated value sits from what this frame produced. The reference
	// uses it to decide which tiles need a new ray; here it feeds the debug view and stays available.
	float temporalVariance = pow(Luminance(result) - Luminance(clippedHistory), 2.0);
	float outVariance      = mix(temporalVariance, localVariance, 1.0 - blend);

	if (any(isnan(result)) || any(isinf(result)))
	{
		result      = vec3(0.0);
		outVariance = SSR_DEFAULT_VARIANCE;
	}

	fragColor = vec4(result, outVariance);
	fragCount = vec4(count, 0.0, 0.0, 0.0);
}

	-->
	</source>
</shader>
