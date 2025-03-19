#version 450

// #extension GL_EXT_mesh_shader: require

layout(location = 0) out vec4 outputColor;

layout(location = 0) in vec4 color;

// layout(location = 1) perprimitiveEXT in vec3 triangleNormal;

void main()
{
    // outputColor = vec4(triangleNormal * 0.5 + vec3(0.5), 1.0);
    outputColor = color;
}