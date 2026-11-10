<shader>
	<type name = "includeShader" />
	<include name = "ssrPassDataInc.shader" />
	<source>
	<!--

#ifndef SSR_COMMON
#define SSR_COMMON

// View space position of a pixel from its positive linear view depth. Mirrors the reconstruction
// SSAOPass does, Vulkan's flipped texture v included.
vec3 SsrViewPos(vec2 uv, float linearDepth)
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
bool SsrProjectToUV(vec3 viewPos, out vec2 uv)
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

#endif

	-->
	</source>
</shader>
