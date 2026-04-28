#version 460

#extension GL_EXT_shader_16bit_storage: require
#extension GL_EXT_shader_8bit_storage: require
#extension GL_GOOGLE_include_directive: require
#extension GL_EXT_nonuniform_qualifier: require

#include "mesh.h"
#include "math.h"

#define DEBUG 0

layout(push_constant) uniform block
{
	Globals globals;
};

layout(constant_id = 2) const bool POST = false;
layout(binding = 1) readonly buffer Draws
{
	MeshDraw draws[];
};

#extension GL_EXT_mesh_shader: require

layout(location = 0) out vec4 gbuffer[2];
// Per-pixel material table index; ~0u = no transmission resolve (opaque / empty).
layout(location = 2) out uint gbufferMaterialIndex;

layout(location = 0) in flat uint drawId;
layout(location = 1) in vec2 uv;
layout(location = 2) in vec3 normal;
layout(location = 3) in vec4 tangent;
layout(location = 4) in vec3 wpos;
layout(location = 5) in flat uint meshletIndex;

layout(binding = 9) uniform sampler textureSampler;

layout(binding = 10) readonly buffer Materials
{
	Material materials[];
};

layout(binding = 0, set = 1) uniform texture2D textures[];

#define SAMP(id) sampler2D(textures[nonuniformEXT(id)], textureSampler)

uint hash(uint a)
{
   a = (a+0x7ed55d16) + (a<<12);
   a = (a^0xc761c23c) ^ (a>>19);
   a = (a+0x165667b1) + (a<<5);
   a = (a+0xd3a2646c) ^ (a<<9);
   a = (a+0xfd7046c5) + (a<<3);
   a = (a^0xb55a4f09) ^ (a>>16);
   return a;
}

#include "mesh_sg_generated.inc.glsl"

void main()
{
    MeshDraw meshDraw = draws[nonuniformEXT(drawId)];
	Material material = materials[nonuniformEXT(meshDraw.materialIndex)];

	// Opaque G-buffer passes (POST == false): do not rasterize blend/mask/transmission; those use the transparency pass only.
	if (!POST && (material.alphaMode != 0u || material.transmissionFactor > 1e-4))
		discard;

	if (globals.gbufferDebugMode == 1u)
	{
		if (POST)
		{
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
		}
		vec3 nrm = normalize(normal);
		vec2 encN = encodeOct(nrm) * 0.5 + 0.5;
		gbuffer[0] = vec4(0.0, 0.0, 0.0, 0.0);
		gbuffer[1] = vec4(encN, 0.045, 0.0);
		gbufferMaterialIndex = POST ? uint(meshDraw.materialIndex) : 0xffffffffu;
		return;
	}

	if (globals.gbufferDebugMode == 2u)
	{
		if (POST)
		{
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
		}
		float debandM = gradientNoise(gl_FragCoord.xy) * 2 - 1;
		uint h = hash(meshletIndex);
		vec3 viz = vec3(float(h & 255u), float((h >> 8u) & 255u), float((h >> 16u) & 255u)) / 255.0;
		vec3 nrmM = normalize(normal);
		vec2 encNM = encodeOct(nrmM) * 0.5 + 0.5 + debandM * (0.5 / 1023);
		gbuffer[0] = vec4(tosrgb(vec4(viz, 1.0)).rgb, 0.0);
		gbuffer[1] = vec4(encNM, 0.045, 0.0);
		gbufferMaterialIndex = POST ? uint(meshDraw.materialIndex) : 0xffffffffu;
		return;
	}

	float deband = gradientNoise(gl_FragCoord.xy) * 2 - 1;

	vec4 albedo = material.baseColorFactor;
	if (material.albedoTexture > 0)
		albedo *= fromsrgb(texture(SAMP(material.albedoTexture), uv));
	if (material.texturePad[0] != 0u)
	{
		SGParams sgParams;
		sgParams.p0 = uintBitsToFloat(material.materialPad[0]);
		sgParams.p1 = uintBitsToFloat(material.materialPad[1]);
		sgParams.p2 = uintBitsToFloat(material.materialPad[2]);
		sgParams.p3 = 0.0;
		albedo.rgb = sg_eval_base_color(uv, wpos, normal, globals.globalTimeSeconds, sgParams);
	}

	if (material.occlusionTexture > 0)
	{
		float occ = texture(SAMP(material.occlusionTexture), uv).r;
		albedo.rgb *= mix(1.0, occ, material.shadingParams.y);
	}

	vec3 nmap = vec3(0, 0, 1);
	if (material.normalTexture > 0)
		nmap = texture(SAMP(material.normalTexture), uv).rgb * 2 - 1;
	nmap.xy *= material.shadingParams.x;

	vec4 pbrSample = vec4(1.0);
	if (material.pbrTexture > 0)
	{
		// glTF metallic-roughness texture is authored in linear space.
		if (material.workflow == 1u)
			pbrSample = texture(SAMP(material.pbrTexture), uv);
		else
			pbrSample = fromsrgb(texture(SAMP(material.pbrTexture), uv));
	}

	vec3 emissive = material.emissiveFactor * material.shadingParams.w;
	if (material.emissiveTexture > 0)
		emissive *= fromsrgb(texture(SAMP(material.emissiveTexture), uv).rgb);

	vec3 bitangent = cross(normal, tangent.xyz) * tangent.w;

	vec3 nrm = normalize(nmap.r * tangent.xyz + nmap.g * bitangent + nmap.b * normal);

	float emissivef = dot(emissive, vec3(0.3, 0.6, 0.1)) / (dot(albedo.rgb, vec3(0.3, 0.6, 0.1)) + 1e-3);

	float roughness = 1.0;
	float metallic = 0.0;

	if (material.workflow == 1u)
	{
		// glTF MR convention: G=roughness, B=metallic.
		roughness = clamp(material.pbrFactor.w * pbrSample.g, 0.045, 1.0);
		metallic = clamp(material.pbrFactor.z * pbrSample.b, 0.0, 1.0);
	}
	else
	{
		// Fallback for legacy specular-glossiness assets.
		vec4 specgloss = material.pbrFactor * pbrSample;
		float gloss = clamp(specgloss.a, 0.0, 1.0);
		roughness = clamp(1.0 - gloss, 0.045, 1.0);

		float f0Scalar = clamp(max(max(specgloss.r, specgloss.g), specgloss.b), 0.02, 0.98);
		metallic = clamp((f0Scalar - 0.04) / 0.96, 0.0, 1.0);
	}

	vec2 encNormal = encodeOct(nrm) * 0.5 + 0.5 + deband * (0.5 / 1023);

	gbuffer[0] = vec4(tosrgb(albedo).rgb, log2(1 + emissivef) / 5);
	gbuffer[1] = vec4(encNormal, roughness, metallic);
	gbufferMaterialIndex = POST ? uint(meshDraw.materialIndex) : 0xffffffffu;
	if (POST)
	{
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
	}

#if DEBUG
	uint mhash = hash(drawId);
	gbuffer[0] = vec4(float(mhash & 255), float((mhash >> 8) & 255), float((mhash >> 16) & 255), 255.0) / 255.0;
	gbufferMaterialIndex = POST ? uint(meshDraw.materialIndex) : 0xffffffffu;
	// gbuffer[0] = vec4(normal, 1.0);
#endif
}