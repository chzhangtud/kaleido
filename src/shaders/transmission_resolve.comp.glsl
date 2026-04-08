#version 460

#extension GL_EXT_shader_16bit_storage : require
#extension GL_EXT_shader_8bit_storage : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_GOOGLE_include_directive : require

#include "mesh.h"
#include "math.h"

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

struct TransmissionResolveData
{
	vec3 cameraPosition;
	float padCam;
	mat4 inverseViewProjection;
	vec2 imageSize;
	float refractionWorldDistance;
	float padTail;
};

layout(push_constant) uniform block
{
	TransmissionResolveData resolveData;
};

layout(binding = 0) uniform writeonly image2D outImage;
layout(binding = 1) uniform sampler2D sceneColorHDR;
layout(binding = 2) uniform sampler2D gbufferImage0;
layout(binding = 3) uniform sampler2D gbufferImage1;
layout(binding = 4) uniform sampler2D depthImage;
layout(binding = 5) uniform usampler2D materialIndexImage;
layout(binding = 6) readonly buffer Materials
{
	Material materials[];
};
layout(binding = 7) uniform sampler textureSampler;
layout(binding = 0, set = 1) uniform texture2D textures[];

#define SAMP(id) sampler2D(textures[nonuniformEXT(id)], textureSampler)

shared mat4 gViewProjection;

void main()
{
	if (gl_LocalInvocationIndex == 0u)
		gViewProjection = inverse(resolveData.inverseViewProjection);
	barrier();

	uvec2 pos = gl_GlobalInvocationID.xy;
	if (pos.x >= uint(resolveData.imageSize.x) || pos.y >= uint(resolveData.imageSize.y))
		return;

	vec2 uv = (vec2(pos) + 0.5) / resolveData.imageSize;
	vec4 bg = texture(sceneColorHDR, uv);

	uint mid = texture(materialIndexImage, uv).r;
	if (mid == 0xffffffffu)
	{
		imageStore(outImage, ivec2(pos), bg);
		return;
	}

	Material material = materials[mid];
	float T = clamp(material.transmissionFactor, 0.0, 1.0);
	if (material.transmissionTexture > 0u)
		T *= texture(SAMP(material.transmissionTexture), uv).r;
	if (T <= 1e-4)
	{
		imageStore(outImage, ivec2(pos), bg);
		return;
	}

	float depth = texture(depthImage, uv).r;
	vec4 clip = vec4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, depth, 1.0);
	vec4 wposh = resolveData.inverseViewProjection * clip;
	vec3 wpos = wposh.xyz / wposh.w;
	vec3 V = normalize(resolveData.cameraPosition - wpos);

	vec4 gbuffer0 = texture(gbufferImage0, uv);
	vec4 gbuffer1 = texture(gbufferImage1, uv);
	vec3 normal = decodeOct(gbuffer1.rg * 2.0 - 1.0);
	if (dot(normal, V) < 0.0)
		normal = -normal;

	float ior = max(material.ior, 1.001);
	vec3 Tdir = refract(-V, normal, 1.0 / ior);
	vec3 refrCol = bg.rgb;
	if (dot(Tdir, Tdir) > 1e-8)
	{
		vec3 wRefr = wpos + Tdir * resolveData.refractionWorldDistance;
		vec4 clipR = gViewProjection * vec4(wRefr, 1.0);
		vec3 ndc = clipR.xyz / max(abs(clipR.w), 1e-5);
		vec2 refrUv = vec2(ndc.x * 0.5 + 0.5, (1.0 - ndc.y) * 0.5);
		refrUv = clamp(refrUv, vec2(0.0), vec2(1.0));
		refrCol = texture(sceneColorHDR, refrUv).rgb;
	}

	vec3 baseLin = fromsrgb(gbuffer0.rgb);
	vec3 outRgb = mix(bg.rgb, refrCol * baseLin, T);
	imageStore(outImage, ivec2(pos), vec4(outRgb, 1.0));
}
