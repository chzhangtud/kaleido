#version 450

#extension GL_EXT_shader_16bit_storage: require
#extension GL_EXT_shader_8bit_storage: require
#extension GL_EXT_shader_explicit_arithmetic_types: require
#extension GL_EXT_mesh_shader: require
#extension GL_GOOGLE_include_directive: require
#extension GL_KHR_shader_subgroup_ballot: require
#extension GL_ARB_shader_draw_parameters: require

#include "mesh.h"

layout(local_size_x = 32, local_size_y = 1, local_size_z = 1) in;

#define CULL 1

layout(push_constant) uniform block
{
    Globals globals;
};

layout(binding = 0) readonly buffer DrawCommands
{
    MeshDrawCommand drawCommands[];
};

layout(binding = 1) readonly buffer Draws
{
    MeshDraw draws[];
};

layout(binding = 2) readonly buffer Meshes
{
    Mesh meshes[];
};

layout(binding = 3) readonly buffer Meshlets
{
    Meshlet meshlets[];
};

taskPayloadSharedEXT TaskPayload payload;

bool coneCull(vec3 center, float radius, vec3 cone_axis, float cone_cutoff, vec3 camera_position)
{
    return dot(camera_position - center, cone_axis) >= cone_cutoff * length(center - camera_position) + radius;
}

void main()
{
    uint mgi = gl_WorkGroupID.x;
    uint ti = gl_LocalInvocationID.x;

    uint drawId = drawCommands[gl_DrawIDARB].drawId;
	MeshDraw meshDraw = draws[drawId];
    payload.drawId = drawCommands[gl_DrawIDARB].drawId;

    Mesh mesh = meshes[meshDraw.meshIndex];

    vec3 lodCenter = mesh.center *  meshDraw.scale +  meshDraw.position;
	float lodRadius = mesh.radius * meshDraw.scale;

    float lodDistance = log2(max(1.0, distance(lodCenter, vec3(0.0)) - lodRadius));
	uint lodIndex = clamp(uint(lodDistance), 0, mesh.lodCount - 1);
    lodIndex = globals.lodEnabled == 1 ? lodIndex : 0;

    uint mi = mgi * 32 + ti + mesh.lods[lodIndex].meshletOffset;

#if CULL
	vec3 center = rotateQuat(meshlets[mi].center, meshDraw.orientation) * meshDraw.scale + meshDraw.position;
	float radius = meshlets[mi].radius * meshDraw.scale;
	vec3 coneAxis = rotateQuat(vec3(int(meshlets[mi].coneAxis[0]) / 127.0,
									int(meshlets[mi].coneAxis[1]) / 127.0,
									int(meshlets[mi].coneAxis[2]) / 127.0), meshDraw.orientation);
	float coneCutoff = int(meshlets[mi].coneCutoff) / 127.0;
	bool accept = !coneCull(center, radius, coneAxis, coneCutoff, vec3(0.0, 0.0, 0.0));

    uvec4 ballot = subgroupBallot(accept);
    uint index = subgroupBallotExclusiveBitCount(ballot);

    if (accept)
        payload.meshletIndices[index] = mi;

    uint count = subgroupBallotBitCount(ballot);

    if (ti == 0)
    {
        EmitMeshTasksEXT(count, 1, 1);
    }
#else
    payload.meshletIndices[ti] = mi;
    if (ti == 0)
    {
        EmitMeshTasksEXT(32, 1, 1);
    }
#endif
}
