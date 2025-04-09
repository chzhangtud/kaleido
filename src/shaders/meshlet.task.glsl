#version 450

#extension GL_EXT_shader_16bit_storage: require
#extension GL_EXT_shader_8bit_storage: require
#extension GL_EXT_shader_explicit_arithmetic_types: require
#extension GL_EXT_mesh_shader: require

#extension GL_GOOGLE_include_directive: require

#extension GL_KHR_shader_subgroup_ballot: require

#include "mesh.h"

layout(local_size_x = 32, local_size_y = 1, local_size_z = 1) in;

#define CULL 1

layout(push_constant) uniform block
{
    MeshDraw meshDraw;
};

layout(binding = 1) readonly buffer Meshlets
{
    Meshlet meshlets[];
};

taskPayloadSharedEXT TaskPayload payload;

bool coneCull(vec4 cone, vec3 view)
{
    //Reference: https://github.com/zeux/meshoptimizer/blob/8764552531e55588a049b2d5f171db33200ac512/demo/main.cpp#L1126
    return dot(cone.xyz, -view) >= cone.w;
}

void main()
{
    uint mgi = gl_WorkGroupID.x;
    uint ti = gl_LocalInvocationID.x;
    uint mi = mgi * 32 + ti;

#if CULL
    // TODO: we assume that gl_SubgroupSize is 32 (same as workgroup size).
    bool accept = !coneCull(meshlets[mi].cone, vec3(0.0, 0.0, 1.0));
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
