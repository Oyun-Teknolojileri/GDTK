<shader>
	<type name = "includeShader" />
	<include name = "cameraDataInc.shader" />
	<source>
	<!--
#ifndef TEMPORAL_INC
#define TEMPORAL_INC

// Helpers for a pass that accumulates its own result over the last frames. A screen space effect
// answers its question once per pixel per frame, so without reusing the frames before it the answer
// is resampled from scratch as soon as the camera or the geometry moves, which reads as noise and
// shimmer. These are the parts every such pass shares; the history target itself and the range a
// sample is clamped into are the pass's own business.

// Scene uv of a view space position in the previous frame, or false when it was not on screen then
// and there is nothing to carry over for that pixel.
bool PreviousFrameUv(vec3 viewPos, out vec2 prevUv)
{
	vec4 clip = camera.prevProjectionView * vec4(viewPos, 1.0);

	if (abs(clip.w) < 0.00001)
	{
		prevUv = vec2(0.0);
		return false;
	}

	prevUv = clip.xy / clip.w * 0.5 + 0.5;
#ifdef VULKAN
	prevUv.y = 1.0 - prevUv.y;
#endif
	return prevUv.x >= 0.0 && prevUv.x <= 1.0 && prevUv.y >= 0.0 && prevUv.y <= 1.0;
}

// History sample clamped into the range this frame spans around the pixel (`rangeMin` / `rangeMax`,
// one component per channel of the history). The previous frames are worth reusing only while they
// still describe the same surface: a sample outside that range belongs to a surface that has just
// appeared, or one the camera has just uncovered, and carrying it over would trail the old frame
// behind the new one.
vec4 ClampToNeighbourhood(vec4 history, vec4 rangeMin, vec4 rangeMax)
{
	return clamp(history, rangeMin, rangeMax);
}

#endif // TEMPORAL_INC
	-->
	</source>
</shader>
