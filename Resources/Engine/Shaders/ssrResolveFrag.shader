<shader>
	<type name = "fragmentShader" />
	<include name = "vulkanCompatInc.shader" />
	<include name = "ssrCommonInc.shader" />
<include name = "normalEncodingInc.shader" />
	<texture slot = "0" name = "s_trace" />
	<texture slot = "1" name = "s_history" />
	<texture slot = "2" name = "s_normalDepth" />
<texture slot = "3" name = "s_hit" />
<texture slot = "4" name = "s_velCur" />
<texture slot = "5" name = "s_velPrev" />
<texture slot = "6" name = "s_hitPrev" />
	<source>
	<!--

precision highp float;
precision highp int;

TK_LOC(2) in vec2 v_texture;

layout (location = 0) out vec4 fragColor;

TK_SAMPLER_BINDING(0) uniform sampler2D s_trace;       // rgb: this frame's reflection, a: its weight.
TK_SAMPLER_BINDING(1) uniform sampler2D s_history;     // rgb: accumulated reflection, a: its weight.
TK_SAMPLER_BINDING(2) uniform sampler2D s_normalDepth; // rg: world normal, b: linear depth, a: roughness.
TK_SAMPLER_BINDING(3) uniform sampler2D s_hit;         // xy: hit uv, z: view depth of the hit, w: pdf of the sample.
TK_SAMPLER_BINDING(4) uniform sampler2D s_velCur;     // xy: this frame's screen position of the object, in UV.
TK_SAMPLER_BINDING(5) uniform sampler2D s_velPrev;    // xy: the previous frame's screen position of it.
TK_SAMPLER_BINDING(6) uniform sampler2D s_hitPrev;    // xy: where the previous frame's ray landed.

// Taps per axis. The trace gives one sample per pixel, so the resolve is what turns the individual
// samples of neighbouring pixels into a single value; three by three is the smallest kernel that
// does that without erasing the contact hardening the per pixel lobe produced.
#define SSR_RESOLVE_TAPS 1

// Width of the kernel at zero roughness, in texels. A rough surface reflects a cone and its samples
// are noisier, so the kernel widens with it: the reflection is low frequency there anyway, and a
// wider average is what keeps the per frame noise out of the accumulation the temporal resolve does.
#define SSR_RESOLVE_ROUGH_SPREAD 2.0

// Extra kernel width while the view moves, in texels. A reprojected reflection follows the surface it
// belongs to, not the reflected content, so the history is stale exactly when the camera moves. The
// resolve covers that with its own average instead of with the history.
#define SSR_RESOLVE_MOTION_SPREAD 1.0

float Luminance(vec3 c)
{
	return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

// Bounded version of a sample, used for the average. A single sample that caught a specular
// highlight of the reflected scene is orders of magnitude brighter than its neighbours; averaging it
// in raw makes the reflection pulse. The compression is undone after the average, so a neighbour
// hood of equal samples still averages to exactly that value.
vec3 Compress(vec3 c)
{
	return c / (1.0 + Luminance(c));
}

vec3 Expand(vec3 c)
{
	return c / max(1.0 - Luminance(c), 0.05);
}

// Specular BRDF term a tap is weighted by, in Unity's normalisation: D * G * pi / 4. The reference
// resolves with the same term divided by the pdf the sample was drawn with, which turns the taps into a
// proper estimate of the lobe instead of an average of whatever the rays happened to hit.
float BrdfWeight(vec3 V, vec3 L, vec3 N, float roughness)
{
	vec3  H     = normalize(L + V);
	float NdotH = clamp(dot(N, H), 0.0, 1.0);
	float NdotL = clamp(dot(N, L), 0.0, 1.0);
	float NdotV = clamp(dot(N, V), 0.0, 1.0);

	float lambdaV = NdotL * (NdotV * (1.0 - roughness) + roughness);
	float lambdaL = NdotV * (NdotL * (1.0 - roughness) + roughness);
	float G       = 0.5 / (lambdaV + lambdaL + 1e-5);

	float a2 = roughness * roughness;
	float d  = (NdotH * a2 - NdotH) * NdotH + 1.0;
	float D  = a2 / (SSR_PI * d * d + 1e-7);

	return (D * G) * (SSR_PI * 0.25);
}

// Pulls the history into the range this frame produced instead of clamping it onto the border of that
// range: a hard clamp parks the stale value on the edge of the box and shows up as a rim travelling with
// the change, while moving the point along the line to the box centre keeps the contrast of what is left.
vec4 ClipAabb(vec3 aabbMin, vec3 aabbMax, vec4 value)
{
	vec3 boxCenter = 0.5 * (aabbMax + aabbMin);
	vec3 boxExtent = max(0.5 * (aabbMax - aabbMin), vec3(1e-6));

	vec3  offset = value.rgb - boxCenter;
	vec3  unit   = offset / boxExtent;
	float reach  = max(abs(unit.x), max(abs(unit.y), abs(unit.z)));

	if (reach > 1.0)
	{
		return vec4(boxCenter + offset / reach, value.a);
	}

	return value;
}


// True when the reflection this pixel now carries is not the one the history holds. Three things can
// change it: the ray stopped hitting anything or started to, it hit at a very different distance, or the
// object it hit moved since the frame the history was written. The last one is what the velocity pass is
// for; all three mean the stored value belongs to something that is not there any more.
bool ReflectionChanged(vec2 uv, float hitDepth)
{
	float previousHitDepth = texture(s_hitPrev, uv).z;

	if ((hitDepth > 0.0) != (previousHitDepth > 0.0))
	{
		return true;
	}

	if (hitDepth > 0.0 && abs(hitDepth - previousHitDepth) / max(hitDepth, 0.01) > 0.1)
	{
		return true;
	}

	if (ssrPass.velocity.x > 0.5)
	{
		vec2 previousHitUV = texture(s_hitPrev, uv).xy;

		vec4 hitNow        = texture(s_velCur, previousHitUV);
		vec4 hitBefore     = texture(s_velPrev, previousHitUV);
		vec4 surfaceNow    = texture(s_velCur, uv);
		vec4 surfaceBefore = texture(s_velPrev, uv);

		// Zero means nothing was drawn there, so the pair only carries a motion when both frames drew the
		// object. An object that appeared or left is a change of its own.
		bool hitDrawn     = hitNow.a > 0.5;
		bool hitWasDrawn  = hitBefore.a > 0.5;
		bool surfDrawn    = surfaceNow.a > 0.5;
		bool surfWasDrawn = surfaceBefore.a > 0.5;

		if (hitDrawn != hitWasDrawn || surfDrawn != surfWasDrawn)
		{
			return true;
		}

		float motionLimit  = ssrPass.velocity.y * 0.5;
		float hitMotion    = hitDrawn ? length(hitNow.xy - hitBefore.xy) : 0.0;
		float surfaceMotion = surfDrawn ? length(surfaceNow.xy - surfaceBefore.xy) : 0.0;

		if (hitMotion > motionLimit || surfaceMotion > motionLimit)
		{
			return true;
		}
	}

	return false;
}

void main(){
	vec2 uv     = v_texture;
	float depth = texture(s_normalDepth, uv).b;

	// Debug view 9: the mask that decides whether the history is trusted, red where it is dropped. The
	// guard can only be tuned by looking at what it actually marks, and this shows it as a picture.
	if (ssrPass.flags.x > 0.5 && abs(ssrPass.flags.z - 9.0) < 0.5)
	{
		bool dropped = ReflectionChanged(uv, texture(s_hit, uv).z);

		fragColor = vec4(dropped ? vec3(1.0, 0.0, 0.0) : vec3(0.0, 0.35, 0.0), 1.0);
		return;
	}

	// Debug views are single samples of the trace, hand them through untouched so they keep reading
	// as data instead of being averaged into a gradient.
	if (ssrPass.flags.x > 0.5)
	{
		fragColor = texture(s_trace, uv);
		return;
	}

	if (depth <= 0.0)
	{
		fragColor = vec4(0.0);
		return;
	}

	float motion    = ssrPass.fades.w;
	float roughness = texture(s_normalDepth, uv).a;
	vec2  texel     = ssrPass.screenParams.xy *
	                  (1.0 + roughness * SSR_RESOLVE_ROUGH_SPREAD + motion * SSR_RESOLVE_MOTION_SPREAD);

	// What every tap is weighted against: the view position and normal of this pixel, and the direction
	// towards the eye.
	vec3 surfaceViewPos = SsrViewPos(uv, depth);
	vec3 surfaceNormal  = normalize(mat3(ssrPass.view) * decodeNormal(texture(s_normalDepth, uv).rg));
	vec3 towardEye      = normalize(-surfaceViewPos);
	float brdfRoughness = max(roughness, 0.05);

	vec4  sumColor    = vec4(0.0);
	float sumColorW   = 0.0;
	float sumAlphaW   = 0.0;
	float sumKernel   = 0.0;
	vec4  minimum     = vec4(1e6);
	vec4  maximum     = vec4(-1e6);

	for (int y = -SSR_RESOLVE_TAPS; y <= SSR_RESOLVE_TAPS; ++y)
	{
		for (int x = -SSR_RESOLVE_TAPS; x <= SSR_RESOLVE_TAPS; ++x)
		{
			vec2 offset = vec2(float(x), float(y));
			vec2 tapUV  = uv + offset * texel;

			vec4  tap      = texture(s_trace, tapUV);
			vec4  hit      = texture(s_hit, tapUV);
			float tapDepth = texture(s_normalDepth, tapUV).b;

			float kernel = max(0.0, 1.0 - length(offset) * 0.5);
			sumKernel   += kernel;

			if (tapDepth <= 0.0)
			{
				continue;
			}

			// A tap across a silhouette belongs to another surface: its reflection would smear over
			// the edge that surface sits on.
			float agree = 1.0 / (1.0 + abs(tapDepth - depth) * 8.0);
			float w     = kernel * agree;

			// The BRDF over pdf weighting the reference uses does not transfer as it stands: its pdf
			// belongs to the plain GGX sampler, this one draws the visible normal distribution, and the
			// ratio it produces swings far enough between neighbouring taps to ring the accumulation
			// into concentric bands. The taps are left equally weighted, which is the average the spatial
			// pass has always been, until the estimator is derived for this sampler.
			sumColor.rgb += Compress(tap.rgb) * tap.a * w;
			sumColorW    += tap.a * w;
			sumAlphaW    += tap.a * w;

			minimum = min(minimum, tap);
			maximum = max(maximum, tap);
		}
	}

	// Premultiplied average with dilation: dividing the accumulated weight by the whole kernel
	// spreads the weight of a hit outwards, so a reflection fades into its surroundings instead of
	// ending on the silhouette of the geometry that produced it. The sum is in the compressed space
	// above, so it comes back to linear here.
	float weight  = sumAlphaW > 0.0 ? sumAlphaW / sumKernel : 0.0;
	vec3  color   = sumColorW > 0.0 ? Expand(sumColor.rgb / sumColorW) : vec3(0.0);
	vec4  current = vec4(color, clamp(weight, 0.0, 1.0));

	if (sumColorW <= 0.0)
	{
		minimum = current;
		maximum = current;
	}

	// Reprojection: where the reflected content was on screen in the previous frame. A reflection moves
	// with the geometry it shows, not with the surface it lands on, so the history is followed through
	// the depth of the hit: the point at that depth along this pixel's ray is what the reflection is
	// showing, and that point is what moved. Reprojecting the surface instead leaves the history
	// behind the reflection whenever the camera moves, and the mismatch reads as shimmer.
	vec4  hitSample = texture(s_hit, uv);
	float hitDepth  = hitSample.z;

	// The point the history holds the reflection of is where the ray landed, not where this surface pixel
	// sits: the two are at different depths and the camera moves them by different amounts. Reprojecting
	// the surface ray's point at the hit distance is an approximation of that, and the gap it leaves is
	// what trails a moving camera; rebuilding the hit point and projecting it is the exact position.
	vec3 viewPos  = hitDepth > 0.0 ? SsrViewPos(hitSample.xy, hitDepth) : SsrViewPos(uv, depth);
	vec4 worldPos = ssrPass.inverseView * vec4(viewPos, 1.0);
	vec4 prevClip = ssrPass.prevViewProjection * worldPos;
	vec2 prevUV   = prevClip.xy / max(prevClip.w, 1e-6) * 0.5 + 0.5;
#ifdef VULKAN
	prevUV.y = 1.0 - prevUV.y;
#endif

	// Object motion: the reprojection above only knows where the camera put the point, so a reflection of
	// something that moved on its own stays behind the thing that left it. The velocity pass writes the
	// screen motion of the objects whose transform changed.
	//
	// The history is only warped for a motion small enough to land on the same content. Warping a large
	// one lands on screen where the reflection never was, and the empty sample it returns is what read as
	// a black shape left behind the moving object; a reflection whose content moved that much is not in
	// the history at all, so the history is dropped for the pixel for this frame instead.
	// Whether the reflection this pixel carries now is the one the history holds. When it is not, the
	// stored value is dropped for this frame instead of averaged in - that is what keeps a moving object
	// from leaving its old reflection behind.
	bool historyUntrusted = ssrPass.temporal.w < 0.5 && ReflectionChanged(uv, hitDepth);

	// Sub pixel motion of the object is still followed with the lookup: the reflection slides with the
	// thing it shows, and only a motion large enough to leave the content behind drops the history.
	if (!historyUntrusted && ssrPass.velocity.x > 0.5 && hitDepth > 0.0)
	{
		vec2  previousHitUV = texture(s_hitPrev, uv).xy;
		vec2  surfaceObject = texture(s_velCur, uv).xy - texture(s_velPrev, uv).xy;
		vec2  hitObject     = texture(s_velCur, previousHitUV).xy - texture(s_velPrev, previousHitUV).xy;
		float motionPixels  = length(hitObject) * ssrPass.velocity.y;

		if (motionPixels > 0.01 && motionPixels <= 0.5)
		{
			prevUV -= mix(surfaceObject, hitObject, clamp(motionPixels * 4.0, 0.0, 1.0));
		}
	}
	bool valid = prevClip.w > 0.0 && prevUV.x >= 0.0 && prevUV.x <= 1.0 && prevUV.y >= 0.0 && prevUV.y <= 1.0;

	// The history is never read on the frame it is reset: its content is whatever the target held
	// before, and a NaN in it would survive a mix with a zero weight (a value multiplied by zero is
	// still a value). Starting from the current frame keeps that frame exact.
	vec4 previous  = current;
	bool hasHistory = ssrPass.temporal.w < 0.5;

	if (hasHistory && valid)
	{
		previous = texture(s_history, clamp(prevUV, vec2(0.0), vec2(1.0)));

		// Neighbourhood clip: a reprojected value outside the range this frame produced is a stale
		// reflection of something the camera no longer sees, and blending it in would ghost.
		previous = ClipAabb(minimum.rgb, maximum.rgb, previous);
	}

	// Response: how much of the history survives follows how well this frame and the accumulation agree
	// on the luminance. Where they agree the weight sits at the upper bound - up to a full hold, which is
	// what keeps a still rough surface quiet - and where they disagree it falls to the lower bound and
	// lets the new frame through. A reflection that changes is therefore never ghosted, and one that does
	// not change is not re-averaged every frame just because the camera is still.
	float lumCurrent  = Luminance(current.rgb);
	float lumPrevious = Luminance(previous.rgb);
	float unbiasedDiff   = abs(lumCurrent - lumPrevious) / max(lumCurrent, max(lumPrevious, 0.2));
	float unbiasedWeight = 1.0 - unbiasedDiff;
	float feedback       = mix(ssrPass.temporal.x, ssrPass.temporal.y, unbiasedWeight * unbiasedWeight);

	if (!hasHistory || historyUntrusted)
	{
		// History dropped: a camera cut, a resize, the first frame of the pass, or a reflection whose
		// content moved out of the history. The trace is all this frame has.
		feedback = 0.0;
	}

	fragColor = mix(current, previous, clamp(feedback, 0.0, 1.0));
}

	-->
	</source>
</shader>
