<shader>
	<type name = "fragmentShader" />
	<include name = "vulkanCompatInc.shader" />
	<include name = "taaPassDataInc.shader" />
	<texture slot = "0" name = "s_color" />
	<texture slot = "1" name = "s_depth" />
	<texture slot = "2" name = "s_history" />
	<source>
	<!--

precision highp float;
precision highp sampler2D;

TK_SAMPLER_BINDING(0) uniform sampler2D s_color;
TK_SAMPLER_BINDING(1) uniform sampler2D s_depth;
TK_SAMPLER_BINDING(2) uniform sampler2D s_history;

TK_LOC(2) in vec2 v_texture;

out vec4 fragColor;

/*
 * Temporal resolve of the frame against the history.
 *
 * The frame was rendered with a sub-pixel offset on the projection, so every pixel of s_color is
 * one sample of a surface at a slightly different screen position than the pixel grid suggests.
 * The history holds the accumulated result of the previous frames, and the question this shader
 * answers is where the surface a pixel shows was on screen last frame: the pixel's own depth is
 * pushed back through this frame's view projection, brought forward through the previous one, and
 * that position is read from the history.
 *
 * A history sample is only worth trusting when it shows the same surface. Two tests decide that:
 * the depth stored at the reprojected position has to match the depth the reprojection predicts,
 * and the sample itself has to sit inside the color range of the pixel's own neighborhood. The
 * first rejects what was hidden last frame, the second rejects what moved under its own power -
 * this pass has no per object motion, so a skinned mesh or an animated prop is not carried, it is
 * clamped away instead of being smeared over the frame.
 */

vec3 NeighborMin(vec2 uv, vec2 texel)
{
	vec3 result = texture(s_color, uv).rgb;
	for (int y = -1; y <= 1; ++y)
	{
		for (int x = -1; x <= 1; ++x)
		{
			result = min(result, texture(s_color, uv + vec2(float(x), float(y)) * texel).rgb);
		}
	}
	return result;
}

vec3 NeighborMax(vec2 uv, vec2 texel)
{
	vec3 result = texture(s_color, uv).rgb;
	for (int y = -1; y <= 1; ++y)
	{
		for (int x = -1; x <= 1; ++x)
		{
			result = max(result, texture(s_color, uv + vec2(float(x), float(y)) * texel).rgb);
		}
	}
	return result;
}

void main()
{
	vec2 uv       = v_texture.xy;
	vec2 texel    = taa.screenParams.xy;
	vec3 color    = texture(s_color, uv).rgb;
	float depth   = texture(s_depth, uv).r;

	// The history was dropped on purpose: the current frame is the answer.
	if (taa.params.y > 0.5)
	{
		fragColor = vec4(color, 1.0);
		return;
	}

	// Window depth to a clip space position, then through the reprojection into the previous
	// frame's clip space. w is the view depth there: behind the previous camera means there is
	// nothing to read.
	//
	// A depth of one is the far plane, which is where the sky sits. Its pixel has no surface to
	// reconstruct - the point is at infinity, and carrying it through the camera translation would
	// slide it - so it takes the rotation only matrix, which is exact for it. Without this the sky
	// side of every silhouette would keep the raw jittered frame and crawl against the geometry
	// next to it.
	vec4 ndc      = vec4(uv * 2.0 - 1.0, depth * taa.depthParams.x + taa.depthParams.y, 1.0);
	vec4 prevClip = (depth >= 1.0 ? taa.skyReprojection : taa.reprojection) * ndc;

	if (prevClip.w <= 0.0)
	{
		fragColor = vec4(color, 1.0);
		return;
	}

	vec2 prevUv = prevClip.xy / prevClip.w * 0.5 + 0.5;

	if (prevUv.x < 0.0 || prevUv.x > 1.0 || prevUv.y < 0.0 || prevUv.y > 1.0)
	{
		fragColor = vec4(color, 1.0);
		return;
	}

	// How much of this frame goes in. A pixel whose history does not describe the same surface
	// leans on the current frame harder, but does not throw the history away: at a silhouette the
	// reprojected sample regularly lands just across the edge, and answering those with the raw
	// jittered frame is what makes a high contrast edge crawl. The neighborhood clamp below is
	// what keeps the leftover history from becoming a ghost.
	float currentWeight = clamp(taa.params.x, 0.0, 1.0);

	// Disocclusion: the frame's own depth at the reprojected position is what is visible there now.
	// If it is not the surface the reprojection predicted, something else came in front of it and
	// the history describes a pixel that this frame's pixel is not.
	float prevDepth = texture(s_depth, prevUv).r * taa.depthParams.x + taa.depthParams.y;
	float predicted = prevClip.z / prevClip.w;

	if (abs(prevDepth - predicted) > taa.params.z)
	{
		currentWeight = 0.5;
	}

	vec3 history = texture(s_history, prevUv).rgb;
	if (any(isnan(history)) || any(isinf(history)))
	{
		fragColor = vec4(color, 1.0);
		return;
	}

	// Neighborhood clamp. The history is allowed to differ from the current pixel only as much as
	// the pixel's own neighborhood does, which is what bounds the length of a ghost.
	vec3 neighborhoodMin = NeighborMin(uv, texel);
	vec3 neighborhoodMax = NeighborMax(uv, texel);
	history              = clamp(history, neighborhoodMin, neighborhoodMax);

	fragColor = vec4(mix(history, color, currentWeight), 1.0);
}
	-->
	</source>
</shader>
