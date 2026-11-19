<shader>
	<type name = "includeShader" />
	<include name = "vulkanCompatInc.shader" />
	<include name = "perDrawDataInc.shader" />
	<texture slot = "18" name = "s_probeSH0" />
	<texture slot = "19" name = "s_probeSH1" />
	<texture slot = "20" name = "s_probeSH2" />
	<source>
	<!--

#ifndef PROBE_GI_SHADER
#define PROBE_GI_SHADER

// Baked diffuse global illumination from a light probe volume.
//
// The bake stores, per probe, the irradiance arriving there divided by pi and expanded in
// spherical harmonics (see ToolKit/Resources/ProbeVolume.h). The runtime keeps the four L1
// coefficients, which is the low frequency part a diffuse surface can respond to.
//
// Storage is channel major: one texture per colour channel, each texel holding the four
// coefficients of that channel in rgba. The volume's z slices are stacked vertically, so a
// slice is a block of `res.y` rows and the bilinear fetch interpolates x and y for free; z is
// interpolated by hand between two slices, and `ty` is clamped inside its slice so the filter
// never reaches into the neighbouring one.

TK_SAMPLER_BINDING(18) uniform sampler2D s_probeSH0;
TK_SAMPLER_BINDING(19) uniform sampler2D s_probeSH1;
TK_SAMPLER_BINDING(20) uniform sampler2D s_probeSH2;

vec4 SampleProbeSliceChannel(sampler2D channel, float x, float ty, float z, float resY, float resZ)
{
	float v = (z * resY + ty) / (resY * resZ);
	return texture(channel, vec2(x, v));
}

// Returns E / pi at worldPos for the world normal n. Writes the volume weight the caller uses
// to blend the probe against the sky and the environment volumes; zero outside the box.
vec3 ProbeIrradiance(vec3 worldPos, vec3 n, out float weight)
{
	weight = 0.0;

	if (perDraw._drawCommand.probeParams.x < 0.5)
	{
		return vec3(0.0);
	}

	vec4 probeMin = perDraw._drawCommand.probeMin;
	vec4 probeMax = perDraw._drawCommand.probeMax;
	vec3 res = max(perDraw._drawCommand.probeRes.xyz, vec3(1.0));

	mat4 invTransform = mat4(perDraw._drawCommand.probeInvT0,
	                         perDraw._drawCommand.probeInvT1,
	                         perDraw._drawCommand.probeInvT2,
	                         perDraw._drawCommand.probeInvT3);

	vec3 local = (invTransform * vec4(worldPos, 1.0)).xyz;
	vec3 uvw = (local - probeMin.xyz) / max(probeMax.xyz - probeMin.xyz, vec3(1e-5));

	// Fade over half a cell at the boundary so the hand off to the sky does not step. The band is
	// measured in cells of the axis it belongs to: one cell taken from the smallest axis and applied
	// to all of them made a volume with few slices in y fade across a third of its height, so most
	// of a room was a sky and probe blend and the probe never owned the pixel it was baked for.
	vec3 edgeCells = min(uvw, 1.0 - uvw) * res;
	weight = clamp(2.0 * min(min(edgeCells.x, edgeCells.y), edgeCells.z), 0.0, 1.0);

	vec3 clamped = clamp(uvw, 0.0, 1.0);

	float ty = clamp(clamped.y * res.y - 0.5, 0.0, res.y - 1.0) + 0.5;
	float zf = clamp(clamped.z * res.z - 0.5, 0.0, res.z - 1.0);
	float z0 = floor(zf);
	float z1 = min(z0 + 1.0, res.z - 1.0);
	float tz = zf - z0;

	vec4 c0 = mix(SampleProbeSliceChannel(s_probeSH0, clamped.x, ty, z0, res.y, res.z),
	              SampleProbeSliceChannel(s_probeSH0, clamped.x, ty, z1, res.y, res.z),
	              tz);
	vec4 c1 = mix(SampleProbeSliceChannel(s_probeSH1, clamped.x, ty, z0, res.y, res.z),
	              SampleProbeSliceChannel(s_probeSH1, clamped.x, ty, z1, res.y, res.z),
	              tz);
	vec4 c2 = mix(SampleProbeSliceChannel(s_probeSH2, clamped.x, ty, z0, res.y, res.z),
	              SampleProbeSliceChannel(s_probeSH2, clamped.x, ty, z1, res.y, res.z),
	              tz);

	// Channel major to coefficient major.
	vec3 s0 = vec3(c0.x, c1.x, c2.x);
	vec3 s1 = vec3(c0.y, c1.y, c2.y);
	vec3 s2 = vec3(c0.z, c1.z, c2.z);
	vec3 s3 = vec3(c0.w, c1.w, c2.w);

	vec3 irradiance = s0 * 0.282095 +
	                  s1 * (0.488603 * n.y) +
	                  s2 * (0.488603 * n.z) +
	                  s3 * (0.488603 * n.x);

	// An L1 projection of a fully occluded direction can undershoot zero, and a negative
	// irradiance subtracts light.
	return max(irradiance, vec3(0.0)) * perDraw._drawCommand.probeParams.y;
}

#endif
	-->
	</source>
</shader>
