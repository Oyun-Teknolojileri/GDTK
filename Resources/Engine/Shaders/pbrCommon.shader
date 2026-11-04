<shader>
	<type name = "includeShader" />
	<source>
	<!--
#ifndef PBR_COMMON_CODE
#define PBR_COMMON_CODE

const float PI = 3.14159265359;

// ---------------------------------------------------------------------------
// Helper functions (Google Filament)
// ---------------------------------------------------------------------------

float pow5(float x)
{
	float x2 = x * x;
	return x2 * x2 * x;
}

float sq(float x)
{
	return x * x;
}

// ---------------------------------------------------------------------------
// Normal Distribution Function
// Walter et al. 2007, "Microfacet Models for Refraction through Rough Surfaces"
// Google Filament implementation with fp16 overflow protection
// ---------------------------------------------------------------------------

float D_GGX(float roughness, float NoH, const vec3 h)
{
	float oneMinusNoHSquared = 1.0 - NoH * NoH;
	float a = NoH * roughness;
	float k = roughness / (oneMinusNoHSquared + a * a);
	float d = k * k * (1.0 / PI);
	return d;
}

// ---------------------------------------------------------------------------
// Visibility (masking-shadowing) functions
// Heitz 2014, "Understanding the Masking-Shadowing Function in Microfacet-Based BRDFs"
// ---------------------------------------------------------------------------

float V_SmithGGXCorrelated(float roughness, float NoV, float NoL)
{
	float a2 = roughness * roughness;
	float lambdaV = NoL * sqrt((NoV - a2 * NoV) * NoV + a2);
	float lambdaL = NoV * sqrt((NoL - a2 * NoL) * NoL + a2);
	return 0.5 / max(lambdaV + lambdaL, 0.0000077);
}

// Hammon 2017, "PBR Diffuse Lighting for GGX+Smith Microsurfaces"
float V_SmithGGXCorrelated_Fast(float roughness, float NoV, float NoL)
{
	return 0.5 / mix(2.0 * NoL * NoV, NoL + NoV, roughness);
}

// ---------------------------------------------------------------------------
// Fresnel
// Schlick 1994, "An Inexpensive BRDF Model for Physically-Based Rendering"
// ---------------------------------------------------------------------------

vec3 F_Schlick(vec3 f0, float f90, float VoH)
{
	return f0 + (f90 - f0) * pow5(1.0 - VoH);
}

vec3 F_Schlick(float cosTheta, vec3 F0)
{
	return F0 + (vec3(1.0) - F0) * pow(1.0 - cosTheta, 5.0);
}

vec3 F_SchlickRoughness(float cosTheta, vec3 F0, float roughness)
{
	return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(1.0 - cosTheta, 5.0);
}

// ---------------------------------------------------------------------------
// Diffuse BRDFs
// ---------------------------------------------------------------------------

float Fd_Lambert()
{
	return 1.0 / PI;
}

// Burley 2012, "Physically-Based Shading at Disney"
float Fd_Burley(float roughness, float NoV, float NoL, float LoH)
{
	float f90 = 0.5 + 2.0 * roughness * LoH * LoH;
	float lightScatter = 1.0 + (f90 - 1.0) * pow5(1.0 - NoL);
	float viewScatter  = 1.0 + (f90 - 1.0) * pow5(1.0 - NoV);
	return lightScatter * viewScatter * (1.0 / PI);
}

// ---------------------------------------------------------------------------
// Utility
// ---------------------------------------------------------------------------

vec3 BaseReflectivityPBR(vec3 F0, vec3 albedo, float metallic)
{
	return mix(F0, albedo, metallic);
}

// Google Filament multiscattering energy compensation
// https://google.github.io/filament/Filament.html#toc4.7.2
vec3 EnergyCompensation(vec2 dfg, vec3 f0)
{
	return 1.0 + f0 * (1.0 / max(f0 * dfg.x + dfg.y, 1e-3) - 1.0);
}

// ---------------------------------------------------------------------------
// Roughness remapping (Google Filament)
// ---------------------------------------------------------------------------

float perceptualRoughnessToRoughness(float perceptualRoughness)
{
	return perceptualRoughness * perceptualRoughness;
}

float roughnessToPerceptualRoughness(float roughness)
{
	return sqrt(roughness);
}

// ---------------------------------------------------------------------------
// Geometric Specular Anti-Aliasing
// Kaplanyan 2016, "Stable Specular Highlights"
// Tokuyoshi 2017, "Error Reduction and Simplification for Shading Anti-Aliasing"
// Tokuyoshi and Kaplanyan 2019, "Improved Geometric Specular Antialiasing"
// Google Filament implementation
// ---------------------------------------------------------------------------

// The variance scale is what decides how far the sampled normal may vary inside one pixel before the
// highlight stops being a point sample of a mirror. Filament's 0.15 is tuned for a normal map whose
// detail is smooth; this engine's assets carry flat (128,128,255) islands with hard edged borders, and
// a pixel crossing one of those borders sees a normal jump, which leaves a single pixel wide band of
// over filtered highlight that crawls with the camera. Measured on the city scene with the frame to
// frame difference of bright pixels (framebuffer 1088x612, 11 frame pairs): 0.15 gives 218 changed
// bright pixels per pair, 2.0 gives 90, 4.0 gives 82 and a static camera drops from 6.1 outliers to
// 0.1 at 2.0. Raising only SPECULAR_AA_THRESHOLD does nothing (0.25 to 1.0 measured -7%), the scale is
// what matters, so the cap stays where Filament puts it.
const float SPECULAR_AA_VARIANCE  = 2.00;
const float SPECULAR_AA_THRESHOLD = 0.25;

float specularAntiAliasing(float perceptualRoughness, const vec3 worldNormal)
{
	vec3 du = dFdx(worldNormal);
	vec3 dv = dFdy(worldNormal);

	float variance = SPECULAR_AA_VARIANCE * (dot(du, du) + dot(dv, dv));
	float roughness = perceptualRoughnessToRoughness(perceptualRoughness);
	float kernelRoughness = min(2.0 * variance, SPECULAR_AA_THRESHOLD);
	float squareRoughness = clamp(roughness * roughness + kernelRoughness, 0.0, 1.0);

	return roughnessToPerceptualRoughness(sqrt(squareRoughness));
}

// ---------------------------------------------------------------------------
// BRDF dispatch functions (Filament style)
// ---------------------------------------------------------------------------

float distribution(float roughness, float NoH, const vec3 h)
{
	return D_GGX(roughness, NoH, h);
}

float visibility(float roughness, float NoV, float NoL)
{
	return V_SmithGGXCorrelated(roughness, NoV, NoL);
}

vec3 fresnel(vec3 f0, float LoH)
{
	float f90 = clamp(dot(f0, vec3(50.0 * 0.33)), 0.0, 1.0);
	return F_Schlick(f0, f90, LoH);
}

float diffuse(float roughness, float NoV, float NoL, float LoH)
{
	return Fd_Lambert();
}

#endif
	-->
	</source>
</shader>