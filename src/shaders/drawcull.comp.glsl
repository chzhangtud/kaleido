#version 450

#extension GL_EXT_shader_16bit_storage: require
#extension GL_EXT_shader_8bit_storage: require
#extension GL_GOOGLE_include_directive: require
#extension GL_KHR_shader_subgroup_ballot: require

#include "mesh.h"

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

layout(push_constant) uniform block
{
	vec4 frustum[6];
	uint drawCount;
	int cullingEnabled;
	int lodEnabled;
};

layout(binding = 0) readonly buffer Draws
{
    MeshDraw draws[];
};

layout(binding = 1) readonly buffer Meshes
{
    Mesh meshes[];
};

layout(binding = 2) writeonly buffer DrawCommands
{
    MeshDrawCommand drawCommands[];
};

layout(binding = 3) buffer DrawCommandCount
{
    uint drawCommandCount;
};

void main()
{
    uint di = gl_GlobalInvocationID.x;

	if (di >= drawCount)
		return;

	uint meshIndex = draws[di].meshIndex;
	Mesh mesh = meshes[meshIndex];

	vec3 center = mesh.center *  draws[di].scale +  draws[di].position;
	float radius = mesh.radius * draws[di].scale;

	bool visible = true;
	for (int i = 0; i < 6; ++i)
		visible = visible && dot(frustum[i], vec4(center, 1.0)) > -radius;

	visible = cullingEnabled == 1 ? visible : true;

	if (visible)
	{
		uint dci = atomicAdd(drawCommandCount, 1);

		float lodDistance = log2(max(1.0, distance(center, vec3(0.0)) - radius));
		uint lodIndex = clamp(uint(lodDistance), 0, mesh.lodCount - 1);
		lodIndex = lodEnabled == 1 ? lodIndex : 0;

		MeshLod lod = mesh.lods[lodIndex];

		drawCommands[dci].drawId = di;
		drawCommands[dci].vertexCount = lod.indexCount;
		drawCommands[dci].instanceCount = 1;
		drawCommands[dci].firstVertex = lod.indexOffset;
		drawCommands[dci].firstInstance = 0;
		drawCommands[dci].groupCountX = (lod.meshletCount + 31) / 32;
		drawCommands[dci].groupCountY = 1;
		drawCommands[dci].groupCountZ = 1;
	}
}