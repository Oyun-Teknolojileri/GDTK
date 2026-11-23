<shader>
	<type name = "includeShader" />
	<include name = "vulkanCompatInc.shader" />
	<include name = "perDrawDataInc.shader" />
	<texture slot = "6" name = "s_probeSH" viewType = "2darray" />
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
// Storage is channel major: one array layer per colour channel, each layer holding the four
// coefficients of that channel in rgba. The volume's z slices are stacked vertically inside a layer,
// so a slice is a block of `res.y` rows and the bilinear fetch interpolates x and y for free; z is
// interpolated by hand between two slices, and `ty` is clamped inside its slice so the filter
// never reaches into the neighbouring one.
//
// A fourth layer is uploaded only when the bake had probes it could not reach: its red channel is
// the coverage of the probe, one where the bake gave it a value, zero where geometry stood between
// it and every traced neighbour. A probe left at zero would darken every surface that shares its
// cell, so the sampled irradiance is divided by the sampled coverage - the trilinear weight of the
// probes that do hold something, which is the same as dropping the empty ones and renormalising the
// rest. A volume with nothing walled off is uploaded with three layers and probeParams.w is zero,
// which makes the coverage one and saves a quarter of the texture. The unbound default of the
// sampler is white, so a program that never binds a volume keeps reading coverage one.

TK_SAMPLER_BINDING(6) uniform sampler2DArray s_probeSH;

vec4 SampleProbeSliceChannel(float x, float ty, float z, float resY, float resZ, int channel)
{
	float v = (z * resY + ty) / (resY * resZ);
	return texture(s_probeSH, vec3(x, v, float(channel)));
}

float SampleProbeCoverage(float x, float ty, float z, float resY, float resZ)
{
	float v = (z * resY + ty) / (resY * resZ);
	return texture(s_probeSH, vec3(x, v, 3.0)).r;
}

// Returns E / pi at worldPos for the world normal n. Writes the volume weight the caller uses
// to blend the probe against the sky and the environment volumes; zero outside the box.
//
// geoNormal is the interpolated surface normal, not the normal mapped one: the optional offset
// below moves the lookup away from the surface, and a perturbed normal can push it into the
// geometry it was meant to leave. The weight keeps using the unshifted position.
vec3 ProbeIrradiance(vec3 worldPos, vec3 n, vec3 geoNormal, out float weight)
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

	vec3 size = max(probeMax.xyz - probeMin.xyz, vec3(1e-5));
	vec3 local = (invTransform * vec4(worldPos, 1.0)).xyz;
	vec3 uvw = (local - probeMin.xyz) / size;

	// Half a cell along the normal, transformed into the volume's local space so a rotated or
	// scaled node needs no special case. Only the lookup moves: the fade below stays on the real
	// position, so the coverage of the volume does not shrink with the offset.
	vec3 uvwSample = uvw;
	if (perDraw._drawCommand.probeRes.w > 0.0)
	{
		vec3 cell   = size / res;
		float scale = perDraw._drawCommand.probeRes.w;
		vec3 nLocal = normalize((invTransform * vec4(geoNormal, 0.0)).xyz);
		uvwSample += nLocal * (scale * dot(cell, abs(nLocal))) / size;
	}

	// Fade over half a cell at the boundary so the hand off to the sky does not step. The band is
	// measured in cells of the axis it belongs to: one cell taken from the smallest axis and applied
	// to all of them made a volume with few slices in y fade across a third of its height, so most
	// of a room was a sky and probe blend and the probe never owned the pixel it was baked for.
	vec3 edgeCells = min(uvw, 1.0 - uvw) * res;
	weight = clamp(2.0 * min(min(edgeCells.x, edgeCells.y), edgeCells.z), 0.0, 1.0);

	vec3 clamped = clamp(uvwSample, 0.0, 1.0);

	float ty = clamp(clamped.y * res.y - 0.5, 0.0, res.y - 1.0) + 0.5;
	float zf = clamp(clamped.z * res.z - 0.5, 0.0, res.z - 1.0);
	float z0 = floor(zf);
	float z1 = min(z0 + 1.0, res.z - 1.0);
	float tz = zf - z0;

	vec4 c0 = mix(SampleProbeSliceChannel(clamped.x, ty, z0, res.y, res.z, 0),
	              SampleProbeSliceChannel(clamped.x, ty, z1, res.y, res.z, 0),
	              tz);
	vec4 c1 = mix(SampleProbeSliceChannel(clamped.x, ty, z0, res.y, res.z, 1),
	              SampleProbeSliceChannel(clamped.x, ty, z1, res.y, res.z, 1),
	              tz);
	vec4 c2 = mix(SampleProbeSliceChannel(clamped.x, ty, z0, res.y, res.z, 2),
	              SampleProbeSliceChannel(clamped.x, ty, z1, res.y, res.z, 2),
	              tz);

	// Same address, same filter, one layer further out: the trilinear weight of the probes that
	// hold a value. The blocked ones contribute their zeros to the sum above and nothing here.
	//
	// That layer is only uploaded when the volume has a probe the bake could not reach, because
	// otherwise it is a layer of ones that costs a quarter of the texture. probeParams.w says
	// which shape this volume was given; three layers means every probe holds a value.
	float coverage = 1.0;
	if (perDraw._drawCommand.probeParams.w > 0.5)
	{
		coverage = mix(SampleProbeCoverage(clamped.x, ty, z0, res.y, res.z),
		               SampleProbeCoverage(clamped.x, ty, z1, res.y, res.z),
		               tz);
	}

	// Channel major to coefficient major.
	vec3 s0 = vec3(c0.x, c1.x, c2.x);
	vec3 s1 = vec3(c0.y, c1.y, c2.y);
	vec3 s2 = vec3(c0.z, c1.z, c2.z);
	vec3 s3 = vec3(c0.w, c1.w, c2.w);

	vec3 irradiance = s0 * 0.282095 +
	                  s1 * (0.488603 * n.y) +
	                  s2 * (0.488603 * n.z) +
	                  s3 * (0.488603 * n.x);

	// Dividing by the coverage renormalises what is left, so a probe the bake could not reach
	// behaves as an absent probe rather than as a black one. The guard keeps a cell whose corners
	// are all blocked from scaling a rounding error into a light.
	irradiance /= max(coverage, 1e-3);

	// An L1 projection of a fully occluded direction can undershoot zero, and a negative
	// irradiance subtracts light.
	return max(irradiance, vec3(0.0)) * perDraw._drawCommand.probeParams.y;
}

#endif
	-->
	</source>
</shader>
