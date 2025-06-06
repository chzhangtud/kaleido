#version 450

#extension GL_EXT_shader_16bit_storage: require
#extension GL_EXT_shader_8bit_storage: require
#extension GL_EXT_shader_explicit_arithmetic_types: require
#extension GL_EXT_mesh_shader: require
#extension GL_GOOGLE_include_directive: require
#extension GL_KHR_shader_subgroup_ballot: require
#extension GL_ARB_shader_draw_parameters: require

#include "mesh.h"

layout(local_size_x = TASK_WGSIZE, local_size_y = 1, local_size_z = 1) in;

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

taskPayloadSharedEXT MeshTaskPayload payload;

bool coneCull(vec3 center, float radius, vec3 cone_axis, float cone_cutoff, vec3 camera_position)
{
    return dot(camera_position - center, cone_axis) >= cone_cutoff * length(center - camera_position) + radius;
}

#if CULL
shared int sharedCount;
#endif

void main()
{
    uint drawId = drawCommands[gl_DrawIDARB].drawId;
	MeshDraw meshDraw = draws[drawId];
    payload.drawId = drawId;

    Mesh mesh = meshes[meshDraw.meshIndex];

    vec3 lodCenter = mesh.center *  meshDraw.scale +  meshDraw.position;
	float lodRadius = mesh.radius * meshDraw.scale;

    float lodDistance = log2(max(1.0, distance(lodCenter, vec3(0.0)) - lodRadius));
	uint lodIndex = clamp(uint(lodDistance), 0, mesh.lodCount - 1);
    lodIndex = globals.lodEnabled == 1 ? lodIndex : 0;

    uint mgi = gl_GlobalInvocationID.x;
    uint mi = mgi + mesh.lods[lodIndex].meshletOffset;

#if CULL
    sharedCount = 0;
	barrier(); // for sharedCount
	
    vec3 center = rotateQuat(meshlets[mi].center, meshDraw.orientation) * meshDraw.scale + meshDraw.position;
	float radius = meshlets[mi].radius * meshDraw.scale;
	vec3 coneAxis = rotateQuat(vec3(int(meshlets[mi].coneAxis[0]) / 127.0,
									int(meshlets[mi].coneAxis[1]) / 127.0,
									int(meshlets[mi].coneAxis[2]) / 127.0), meshDraw.orientation);
	float coneCutoff = int(meshlets[mi].coneCutoff) / 127.0;
	
    bool visible = mgi < drawCommands[gl_DrawIDARB].taskCount;

	// backface cone culling
	visible = visible && !coneCull(center, radius, coneAxis, coneCutoff, vec3(0, 0, 0));
	// the left/top/right/bottom plane culling utilizes frustum symmetry to cull against two planes at the same time
	visible = visible && center.z * globals.frustum[1] - abs(center.x) * globals.frustum[0] > -radius;
	visible = visible && center.z * globals.frustum[3] - abs(center.y) * globals.frustum[2] > -radius;
	// the near/far plane culling uses camera space Z directly
	// note: because we use an infinite projection matrix, this may cull meshlets that belong to a mesh that straddles the "far" plane; we could optionally remove the far check to be conservative
	visible = visible && center.z + radius > globals.znear && center.z - radius < globals.zfar;

    if (visible)
    {
        uint index = atomicAdd(sharedCount, 1);
        payload.meshletIndices[index] = mi;
    }

    barrier(); // for sharedCount

	EmitMeshTasksEXT(sharedCount, 1, 1);
#else
    payload.meshletIndices[gl_LocalInvocationIndex] = mi;
    uint count = min(TASK_WGSIZE, drawCommands[gl_DrawIDARB].taskCount - gl_WorkGroupID.x * TASK_WGSIZE);

    EmitMeshTasksEXT(count, 1, 1);
#endif
}
