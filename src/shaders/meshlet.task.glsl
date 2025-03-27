#version 450

#extension GL_EXT_shader_16bit_storage: require
#extension GL_EXT_shader_8bit_storage: require
#extension GL_EXT_shader_explicit_arithmetic_types: require
#extension GL_EXT_mesh_shader: require

#extension GL_GOOGLE_include_directive: require

#include "mesh.h"

layout(local_size_x = 32, local_size_y = 1, local_size_z = 1) in;

#define CULL 1

layout(binding = 1) readonly buffer Meshlets
{
    Meshlet meshlets[];
};

taskPayloadSharedEXT TaskPayload payload;

bool coneCull(vec4 cone, vec3 view)
{
    return dot(cone.xyz, view) < cone.w - 0.05; // 0.05 is EPSILON offset.
}

shared uint meshletCount;

void main()
{
    uint mgi = gl_WorkGroupID.x;
    uint ti = gl_LocalInvocationID.x;
    uint mi = mgi * 32 + ti;

#if CULL
    meshletCount = 0;
    memoryBarrierShared();
    
    if (!coneCull(meshlets[mi].cone, vec3(0.0, 0.0, 1.0)))
    {
        uint index = atomicAdd(meshletCount, 1);

        payload.meshletIndices[index] = mi;
    }
    
    memoryBarrierShared();

    if (ti == 0)
    {
        EmitMeshTasksEXT(meshletCount, 1, 1);
    }
#else
    payload.meshletIndices[ti] = mi;
    if (ti == 0)
    {
        EmitMeshTasksEXT(32, 1, 1);
    }
#endif
}
