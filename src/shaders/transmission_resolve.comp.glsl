#version 460

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

// Minimal stub: copy opaque-lit HDR to the display target. Refraction / volume / SSR will sample extra inputs here later.
struct TransmissionResolveData
{
	vec2 imageSize;
	vec2 pad;
};

layout(push_constant) uniform block
{
	TransmissionResolveData resolveData;
};

layout(binding = 0) uniform writeonly image2D outImage;
layout(binding = 1) uniform sampler2D sceneColorHDR;

void main()
{
	uvec2 pos = gl_GlobalInvocationID.xy;
	if (pos.x >= uint(resolveData.imageSize.x) || pos.y >= uint(resolveData.imageSize.y))
		return;

	vec2 uv = (vec2(pos) + 0.5) / resolveData.imageSize;
	vec4 color = texture(sceneColorHDR, uv);
	imageStore(outImage, ivec2(pos), color);
}
