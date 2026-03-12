<shader>
	<type name = "includeShader" />
	<source>
	<!--
#ifndef PBR_COMMON_CODE
#define PBR_COMMON_CODE

const float PI = 3.14159265359;

float D_GGX(float NdotH, float a)
{
	float a2 = a * a;
	float f = (NdotH * a2 - NdotH) * NdotH + 1.0;
	return a2 / (PI * f * f);
}

float V_SmithGGXCorrelated(float NdotV, float NdotL, float a)
{
	float a2 = a * a;
	float GGXL = NdotV * sqrt((-NdotL * a2 + NdotL) * NdotL + a2);
	float GGXV = NdotL * sqrt((-NdotV * a2 + NdotV) * NdotV + a2);
	return 0.5 / (GGXV + GGXL);
}

vec3 F_Schlick(float cosTheta, vec3 F0)
{
	return F0 + (vec3(1.0) - F0) * pow(1.0 - cosTheta, 5.0);
}

vec3 F_SchlickRoughness(float cosTheta, vec3 F0, float roughness)
{
	return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(1.0 - cosTheta, 5.0);
}

float Fd_Lambert()
{
	return 1.0 / PI;
}

vec3 BaseReflectivityPBR(vec3 F0, vec3 albedo, float metallic)
{
	return mix(F0, albedo, metallic);
}

// Google Filament multiscattering energy compensation
// https://google.github.io/filament/Filament.html#toc4.7.2
// dfg is the BRDF LUT value (vec2), f0 is the base reflectivity
// dfg.x + dfg.y is the total energy (white furnace integral)
vec3 EnergyCompensation(vec2 dfg, vec3 f0)
{
	return 1.0 + f0 * (1.0 / (f0 * dfg.x + dfg.y) - 1.0);
}

struct PBRDots
{
	float NdotV;
	float NdotL;
	float NdotH;
	float HdotV;
};

vec3 CookTorranceBRDF(PBRDots dots, float roughness, vec3 F0, out vec3 fresnel)
{
	float D = D_GGX(dots.NdotH, roughness);
	float V = V_SmithGGXCorrelated(dots.NdotV, dots.NdotL, roughness);
	fresnel = F_Schlick(dots.HdotV, F0);
	return (D * V) * fresnel;
}

vec3 PBR(vec3 normal, vec3 fragToEye, vec3 albedo, float metallic, float roughness, vec3 lightDir, vec3 lightColor, vec3 energyCompensation)
{
	vec3 F0 = BaseReflectivityPBR(vec3(0.04), albedo, metallic);

	vec3 halfway = normalize(lightDir + fragToEye);

	PBRDots dots;
	dots.NdotV = abs(dot(normal, fragToEye)) + 1e-5;
	dots.NdotL = clamp(dot(normal, lightDir), 0.0, 1.0);
	dots.NdotH = clamp(dot(normal, halfway), 0.0, 1.0);
	dots.HdotV = clamp(dot(halfway, fragToEye), 0.0, 1.0);

	vec3 fresnel;
	vec3 Fr = CookTorranceBRDF(dots, roughness, F0, fresnel);

	Fr *= energyCompensation;

	vec3 kS = fresnel;
	vec3 kD = vec3(1.0) - kS;
	kD *= 1.0 - metallic;

	vec3 Fd = kD * albedo * Fd_Lambert();

	return (Fd + Fr) * lightColor * dots.NdotL;
}

#endif
	-->
	</source>
</shader>