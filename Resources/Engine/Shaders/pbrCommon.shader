<shader>
	<type name = "includeShader" />
	<source>
	<!--
#ifndef PBR_COMMON_CODE
#define PBR_COMMON_CODE

const float PI = 3.14159265359;

float DistributionGGX(float NdotH, float roughness)
{
	float a = roughness * roughness;
	float a2 = a * a;
	float NdotH2 = NdotH * NdotH;

	float nom = a2;
	float denom = (NdotH2 * (a2 - 1.0) + 1.0);
	denom = PI * denom * denom;

	return nom / denom;
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
	float r = (roughness + 1.0);
	float k = (r * r) / 8.0;

	float nom = NdotV;
	float denom = NdotV * (1.0 - k) + k;

	return nom / denom;
}

float GeometrySmith(float NdotV, float NdotL, float roughness)
{
	float ggx2 = GeometrySchlickGGX(NdotV, roughness);
	float ggx1 = GeometrySchlickGGX(NdotL, roughness);

	return ggx1 * ggx2;
}

vec3 FresnelSchlick(float cosTheta, vec3 F0)
{
	float f = clamp(1.0 - cosTheta, 0.0, 1.0);
	float f2 = f * f;
	float f5 = f2 * f2 * f;
	return F0 + (1.0 - F0) * f5;
}

vec3 FresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness)
{
	float f = clamp(1.0 - cosTheta, 0.0, 1.0);
	float f2 = f * f;
	float f5 = f2 * f2 * f;
	return F0 + (max(vec3(1.0 - roughness), F0) - F0) * f5;
}

vec3 BaseReflectivityPBR(vec3 F0, vec3 albedo, float metallic)
{
	return mix(F0, albedo, metallic);
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
	float NDF = DistributionGGX(dots.NdotH, roughness);
	float geometry = GeometrySmith(dots.NdotV, dots.NdotL, roughness);
	fresnel = FresnelSchlick(dots.HdotV, F0);
	vec3 numerator = NDF * geometry * fresnel;
	float denominator = 4.0 * dots.NdotV * dots.NdotL + 0.0001;
	return numerator / denominator;
}

vec3 PBR(vec3 normal, vec3 fragToEye, vec3 albedo, float metallic, float roughness, vec3 lightDir, vec3 lightColor)
{
	vec3 F0 = BaseReflectivityPBR(vec3(0.04), albedo, metallic);

	vec3 halfway = normalize(lightDir + fragToEye);

	PBRDots dots;
	dots.NdotV = max(dot(normal, fragToEye), 0.0);
	dots.NdotL = max(dot(normal, lightDir), 0.0);
	dots.NdotH = max(dot(normal, halfway), 0.0);
	dots.HdotV = max(dot(halfway, fragToEye), 0.0);

	vec3 fresnel;
	vec3 specular = CookTorranceBRDF(dots, roughness, F0, fresnel);

	vec3 kS = fresnel;
	vec3 kD = vec3(1.0) - kS;
	kD *= 1.0 - metallic;

	return (kD * albedo / PI + specular) * lightColor * dots.NdotL;
}

#endif
	-->
	</source>
</shader>