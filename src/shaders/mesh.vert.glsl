#version 450

#extension GL_EXT_shader_16bit_storage: require
#extension GL_EXT_shader_8bit_storage: require
#extension GL_EXT_shader_explicit_arithmetic_types: require

#extension GL_GOOGLE_include_directive: require

#include "mesh.h"

layout(binding = 0) readonly buffer Vertices
{
    Vertex vertices[];
} vb;

layout(location = 0) out vec4 color;

void main()
{
    Vertex v = vb.vertices[gl_VertexIndex];

    vec3 position = vec3(v.vx, v.vy, v.vz);
    vec3 normal = vec3(v.nx, v.ny, v.nz) / 127.0 - 1.0;
    vec2 texcoord = vec2(v.tu, v.tv);

    gl_Position = vec4(position * 0.5 + vec3(0.0, 0.0, 0.5), 1.0);

    color = vec4(normal * 0.5 + vec3(0.5), 1.0);
}