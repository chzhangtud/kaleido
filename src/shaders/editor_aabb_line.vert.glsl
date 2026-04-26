#version 450

layout(location = 0) in vec3 inWorldPosition;

layout(push_constant) uniform Push
{
	mat4 view;
	mat4 projection;
	vec4 lineColor;
} pc;

void main()
{
	gl_Position = pc.projection * pc.view * vec4(inWorldPosition, 1.0);
}
