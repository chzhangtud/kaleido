#version 460

#extension GL_GOOGLE_include_directive: require

#include "math.h"

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

struct TaaData
{
	vec2 imageSize;
	int historyValid;
	float blendAlpha;
};

layout(push_constant) uniform block
{
	TaaData taaData;
};

layout(binding = 0) uniform sampler2D currentColor;
layout(binding = 1) uniform sampler2D historyColor;
layout(binding = 2) uniform writeonly image2D outImage;
layout(binding = 3) uniform writeonly image2D historyOut;

void main()
{
	uvec2 pos = gl_GlobalInvocationID.xy;
	if (pos.x >= uint(taaData.imageSize.x) || pos.y >= uint(taaData.imageSize.y))
		return;

	vec2 uv = (vec2(pos) + 0.5) / taaData.imageSize;
	vec3 cur = texture(currentColor, uv).rgb;

	vec3 cmin = cur;
	vec3 cmax = cur;
	for (int dy = -1; dy <= 1; ++dy)
	{
		for (int dx = -1; dx <= 1; ++dx)
		{
			vec2 suv = (vec2(pos) + vec2(dx, dy) + 0.5) / taaData.imageSize;
			vec3 s = texture(currentColor, suv).rgb;
			cmin = min(cmin, s);
			cmax = max(cmax, s);
		}
	}

	vec3 hist = texture(historyColor, uv).rgb;
	vec3 histClamped = clamp(hist, cmin, cmax);

	float a = (taaData.historyValid != 0) ? taaData.blendAlpha : 1.0;
	vec3 resolved = mix(histClamped, cur, a);

	vec4 outC = vec4(resolved, 1.0);
	imageStore(outImage, ivec2(pos), outC);
	imageStore(historyOut, ivec2(pos), outC);
}
