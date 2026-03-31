#version 460

#extension GL_GOOGLE_include_directive: require

#include "math.h"
#define DEBUG 0

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

const float PI = 3.14159265;

struct ShadeData
{
	vec3 cameraPosition;
	vec3 sunDirection;
	int shadowsEnabled;

	mat4 inverseViewProjection;

	vec2 imageSize;
};

layout(push_constant) uniform block
{
	ShadeData shadeData;
};

layout(binding = 0) uniform writeonly image2D outImage;
layout(binding = 1) uniform sampler2D gbufferImage0;
layout(binding = 2) uniform sampler2D gbufferImage1;
layout(binding = 3) uniform sampler2D depthImage;

layout(binding = 4) uniform sampler2D shadowImage;

void main()
{
	uvec2 pos = gl_GlobalInvocationID.xy;
	vec2 uv = (vec2(pos) + 0.5) / shadeData.imageSize;

	vec4 gbuffer0 = texture(gbufferImage0, uv);
	vec4 gbuffer1 = texture(gbufferImage1, uv);
	float depth = texture(depthImage, uv).r;

	vec3 albedo = fromsrgb(gbuffer0.rgb);
	vec3 emissive = albedo * (exp2(gbuffer0.a * 5.0) - 1.0);
	vec3 normal = decodeOct(gbuffer1.rg * 2.0 - 1.0);

	float ndotl = max(dot(normal, shadeData.sunDirection), 0.0);

	vec4 clip = vec4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, depth, 1.0);
	vec4 wposh = shadeData.inverseViewProjection * clip;
	vec3 wpos = wposh.xyz / wposh.w;

	vec3 view = normalize(shadeData.cameraPosition - wpos);
	vec3 lightDir = normalize(shadeData.sunDirection);
	vec3 halfv = normalize(view + lightDir);

	float NdotL = max(dot(normal, lightDir), 0.0);
	float NdotV = max(dot(normal, view), 0.0);
	float NdotH = max(dot(normal, halfv), 0.0);
	float VdotH = max(dot(view, halfv), 0.0);

	// Read metallic-roughness parameters from the G-Buffer.
	float roughness = clamp(gbuffer1.b, 0.045, 1.0);
	float metallic = clamp(gbuffer1.a, 0.0, 1.0);
	float alpha = roughness * roughness;

	vec3 F0 = mix(vec3(0.04), albedo, metallic);

	// GGX normal distribution function
	float a2 = alpha * alpha;
	float denom = (NdotH * NdotH * (a2 - 1.0) + 1.0);
	float D = a2 / max(PI * denom * denom, 1e-5);

	// Smith GGX geometry term (Schlick approximation)
	float k = (alpha + 1.0);
	k = (k * k) * 0.125; // /8
	float Gv = NdotV / (NdotV * (1.0 - k) + k);
	float Gl = NdotL / (NdotL * (1.0 - k) + k);
	float G = Gv * Gl;

	// Schlick Fresnel approximation.
	vec3 F = F0 + (vec3(1.0) - F0) * pow(1.0 - VdotH, 5.0);

	vec3 specularBRDF = (D * G * F) / max(4.0 * NdotL * NdotV, 1e-4);

	vec3 kd = (vec3(1.0) - F) * (1.0 - metallic);

	// Disney Burley diffuse term (base lobe only).
	float FD90 = 0.5 + 2.0 * roughness * VdotH * VdotH;
	float FL = pow(1.0 - NdotL, 5.0);
	float FV = pow(1.0 - NdotV, 5.0);
	float disneyDiffuse = (1.0 + (FD90 - 1.0) * FL) * (1.0 + (FD90 - 1.0) * FV);
	vec3 diffuseBRDF = kd * albedo * (disneyDiffuse / PI);

	float shadow = 1.0;
	if (shadeData.shadowsEnabled == 1)
		shadow = texture(shadowImage, uv).r;

	float ambient = 0.07;
	float shadowAmbient = 0.05;
	float sunIntensity = 2.5;

	vec3 directLighting = (diffuseBRDF + specularBRDF) * NdotL * min(shadow + shadowAmbient, 1.0) * sunIntensity;
	vec3 ambientLighting = kd * albedo * ambient;

	vec3 outputColor = directLighting + ambientLighting + emissive;
	float deband = gradientNoise(vec2(pos)) * 2 - 1;

	vec4 finalOutput = vec4(tonemap(outputColor) + deband * (0.5 / 255), 1.0);
#if DEBUG
	finalOutput = gbuffer0;
#endif
	imageStore(outImage, ivec2(pos), finalOutput);
}
