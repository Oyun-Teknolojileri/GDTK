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
	return 0.5 / (lambdaV + lambdaL);
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
	return 1.0 + f0 * (1.0 / (f0 * dfg.x + dfg.y) - 1.0);
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

// ---------------------------------------------------------------------------
// Cook-Torrance specular BRDF
// ---------------------------------------------------------------------------

struct PBRDots
{
	float NdotV;
	float NdotL;
	float NdotH;
	float HdotV;
};

#endif
	-->
	</source>
</shader>