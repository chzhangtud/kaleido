#version 450

#extension GL_EXT_mesh_shader: require

layout(location = 0) out vec4 outputColor;

layout(location = 0) in vec4 color;
layout(location = 1) in vec2 uv;

layout(binding = 0, set = 1) uniform sampler2D textures[];

#define TRIANGLE_NORMAL 0

#if TRIANGLE_NORMAL
layout(location = 1) perprimitiveEXT in vec3 triangleNormal;
#endif

void main()
{
#if TRIANGLE_NORMAL
    outputColor = vec4(triangleNormal * 0.5 + vec3(0.5), 1.0);
#else
    outputColor = color;
#endif
}