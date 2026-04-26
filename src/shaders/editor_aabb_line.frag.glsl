#version 450

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform Push
{
	mat4 view;
	mat4 projection;
	vec4 lineColor;
} pc;

void main()
{
	outColor = pc.lineColor;
}
