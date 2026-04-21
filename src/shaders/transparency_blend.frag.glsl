#version 460

#extension GL_EXT_shader_16bit_storage: require
#extension GL_EXT_shader_8bit_storage: require
#extension GL_GOOGLE_include_directive: require
#extension GL_EXT_nonuniform_qualifier: require

#include "mesh.h"
#include "math.h"

layout(push_constant) uniform block
{
	Globals globals;
};

// Keeps specialization layout compatible with mesh.frag (cluster/task post pipelines pass three constants).
layout(constant_id = 2) const bool SPEC_SLOT_POST_COMPAT = true;

layout(binding = 1) readonly buffer Draws
{
	MeshDraw draws[];
};

layout(location = 0) in flat uint drawId;
layout(location = 1) in vec2 uv;
layout(location = 2) in vec3 normal;
layout(location = 3) in vec4 tangent;
layout(location = 4) in vec3 wpos;

layout(binding = 9) uniform sampler textureSampler;

layout(binding = 10) readonly buffer Materials
{
	Material materials[];
};

layout(binding = 0, set = 1) uniform texture2D textures[];

#define SAMP(id) sampler2D(textures[nonuniformEXT(id)], textureSampler)

layout(location = 0) out vec4 outColor;

void main()
{
	MeshDraw meshDraw = draws[nonuniformEXT(drawId)];
	Material material = materials[nonuniformEXT(meshDraw.materialIndex)];

	vec4 albedo = material.baseColorFactor;
	if (material.albedoTexture > 0)
		albedo *= fromsrgb(texture(SAMP(material.albedoTexture), uv));

	if (material.alphaMode == 1u)
	{
		if (albedo.a < material.shadingParams.z)
			discard;
	}
	else if (material.alphaMode == 2u)
	{
		if (albedo.a < 0.5)
			discard;
	}

	vec3 nmap = vec3(0, 0, 1);
	if (material.normalTexture > 0)
		nmap = texture(SAMP(material.normalTexture), uv).rgb * 2 - 1;
	nmap.xy *= material.shadingParams.x;

	vec3 bitangent = cross(normal, tangent.xyz) * tangent.w;
	vec3 nrm = normalize(nmap.r * tangent.xyz + nmap.g * bitangent + nmap.b * normal);

	vec3 emissive = material.emissiveFactor * material.shadingParams.w;
	if (material.emissiveTexture > 0)
		emissive *= fromsrgb(texture(SAMP(material.emissiveTexture), uv).rgb);

	vec3 sunDir = normalize(globals.sunDirection);
	float NdotL = max(dot(nrm, sunDir), 0.0);
	vec3 linRgb = fromsrgb(albedo.rgb) * (0.07 + 2.5 * NdotL) + emissive;
	vec3 srgbOut = tonemap(linRgb);

	float T = clamp(material.transmissionFactor, 0.0, 1.0);
	if (material.transmissionTexture > 0u)
		T *= texture(SAMP(material.transmissionTexture), uv).r;

	float alpha = albedo.a;
	if (T > 1e-4)
		alpha = max(alpha, T);

	outColor = vec4(srgbOut, alpha);
}
