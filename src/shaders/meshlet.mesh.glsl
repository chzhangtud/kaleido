#version 450

#extension GL_EXT_shader_16bit_storage: require
#extension GL_EXT_shader_8bit_storage: require
#extension GL_EXT_shader_explicit_arithmetic_types: require
#extension GL_EXT_mesh_shader: require
#extension GL_GOOGLE_include_directive: require

#include "mesh.h"
#include "math.h"

#define DEBUG 0
#define CULL MESH_CULL
#define TRIANGLE_NORMAL 0

layout(local_size_x = MESH_WGSIZE, local_size_y = 1, local_size_z = 1) in;
layout(triangles, max_vertices = MESH_MAXVTX, max_primitives = MESH_MAXTRI) out;

layout (constant_id = 1) const bool TASK = false;

layout(triangles, max_vertices = MESH_MAXVTX, max_primitives = MESH_MAXTRI) out;

layout(push_constant) uniform block
{
    Globals globals;
};

layout(binding = 0) readonly buffer TaskCommands
{
	MeshTaskCommand taskCommands[];
};

layout(binding = 1) readonly buffer Draws
{
    MeshDraw draws[];
};

layout(binding = 3) readonly buffer Meshlets
{
    Meshlet meshlets[];
};

layout(binding = 4) readonly buffer MeshletData
{
	uint meshletData[];
};

layout(binding = 4) readonly buffer MeshletData16
{
	uint16_t meshletData16[];
};

layout(binding = 4) readonly buffer MeshletData8
{
	uint8_t meshletData8[];
};

layout(binding = 5) readonly buffer Vertices
{
    Vertex vertices[];
};

layout(binding = 8) readonly buffer ClusterIndices
{
	uint clusterIndices[];
};

layout(location = 0) out flat uint out_drawId[];
layout(location = 1) out vec2 out_uv[];
layout(location = 2) out vec3 out_normal[];
layout(location = 3) out vec4 out_tangent[];
layout(location = 4) out vec3 out_wpos[];

#if TRIANGLE_NORMAL
layout(location = 1) perprimitiveEXT out vec3 triangleNormal[];
#endif

// only usable with task shader (TASK=true)
taskPayloadSharedEXT MeshTaskPayload payload;

uint hash(uint a)
{
   a = (a+0x7ed55d16) + (a<<12);
   a = (a^0xc761c23c) ^ (a>>19);
   a = (a+0x165667b1) + (a<<5);
   a = (a+0xd3a2646c) ^ (a<<9);
   a = (a+0xfd7046c5) + (a<<3);
   a = (a^0xb55a4f09) ^ (a>>16);
   return a;
}

#if CULL
shared vec3 vertexClip[MESH_MAXVTX];
#endif

void main()
{
    uint ti = gl_LocalInvocationIndex;

    // we convert 3D index to 1D index using a fixed *256 factor, see clustersubmit.comp.glsl
	uint ci = TASK ? payload.clusterIndices[gl_WorkGroupID.x] : clusterIndices[gl_WorkGroupID.x + gl_WorkGroupID.y * 256 + gl_WorkGroupID.z * CLUSTER_TILE];

    if (ci == ~0)
	{
		SetMeshOutputsEXT(0, 0);
		return;
	}

	MeshTaskCommand	command = taskCommands[ci & 0xffffff];
	uint mi = command.taskOffset + (ci >> 24);

	MeshDraw meshDraw = draws[command.drawId];

#if DEBUG
    uint mhash = hash(mi);
    vec3 mcolor = vec3(float(mhash & 255), float((mhash >> 8) & 255),  float((mhash >> 16) & 255)) / 255.0;
#endif

    vec2 screen = vec2(globals.screenWidth, globals.screenHeight);

    uint vertexCount = uint(meshlets[mi].vertexCount);
	uint triangleCount = uint(meshlets[mi].triangleCount);

    SetMeshOutputsEXT(vertexCount, triangleCount);

    uint dataOffset = meshlets[mi].dataOffset;
    uint baseVertex = meshlets[mi].baseVertex;
    bool shortRefs = uint(meshlets[mi].shortRefs) == 1;
    uint vertexOffset = dataOffset;
    uint indexOffset = dataOffset + (shortRefs ? (vertexCount + 1) / 2 : vertexCount);
    uint triangleOffset = indexOffset * 4;
    
    for (uint i = ti; i < uint(meshlets[mi].vertexCount); i += MESH_WGSIZE)
    {
        uint vi = shortRefs ? uint(meshletData16[vertexOffset * 2 + i]) + baseVertex : meshletData[vertexOffset + i] + baseVertex;

        Vertex v = vertices[vi];

        vec3 position = vec3(v.vx, v.vy, v.vz);
        vec2 texcoord = vec2(v.tu, v.tv);

        vec3 normal;
		vec4 tangent;
		unpackTBN(v.np, uint(v.tp), normal, tangent);

        normal = rotateQuat(normal, meshDraw.orientation);
        tangent.xyz = rotateQuat(tangent.xyz, meshDraw.orientation);
		vec3 wpos = rotateQuat(position, meshDraw.orientation) * meshDraw.scale + meshDraw.position;
		vec4 clip = globals.projection * (globals.cullData.view * vec4(wpos, 1));
        gl_MeshVerticesEXT[i].gl_Position = clip;

        out_drawId[i] = command.drawId;
		out_uv[i] = texcoord;
		out_normal[i] = normal;
		out_tangent[i] = tangent;
        out_wpos[i] = wpos;
#if CULL
		vertexClip[i] = vec3((clip.xy / clip.w * 0.5 + vec2(0.5)) * screen, clip.w);
#endif
#if DEBUG
        out_normal[i] = mcolor;
#endif
    }

#if CULL
	barrier();
#endif

#if TRIANGLE_NORMAL
    for (uint i = ti; i < triangleCount); i += MESH_WGSIZE)
    {
        uint vi0 = meshletData[vertexOffset + meshletData8[triangleOffset + 3 * i + 0]];
        uint vi1 = meshletData[vertexOffset + meshletData8[triangleOffset + 3 * i + 1]];
        uint vi2 = meshletData[vertexOffset + meshletData8[triangleOffset + 3 * i + 2]];
        
        vec3 position0 = vec3(vertices[vi0].vx, vertices[vi0].vy, vertices[vi0].vz);
        vec3 position1 = vec3(vertices[vi1].vx, vertices[vi1].vy, vertices[vi1].vz);
        vec3 position2 = vec3(vertices[vi2].vx, vertices[vi2].vy, vertices[vi2].vz);

        vec3 normal = normalize(cross(position1 - position0, position2 - position0));

        triangleNormal[i] = normal;
    }
#endif
    
    for (uint i = ti; i < triangleCount;)
    {
        uint offset = triangleOffset + 3 * i;
        uint a = uint(meshletData8[offset]), b = uint(meshletData8[offset + 1]), c = uint(meshletData8[offset + 2]);
        gl_PrimitiveTriangleIndicesEXT[i] = uvec3(a, b, c);

#if CULL
		bool culled = false;

		vec2 pa = vertexClip[a].xy, pb = vertexClip[b].xy, pc = vertexClip[c].xy;

		// backface culling + zero-area culling
		vec2 eb = pb - pa;
		vec2 ec = pc - pa;

		culled = culled || (eb.x * ec.y <= eb.y * ec.x);

		// small primitive culling
		vec2 bmin = min(pa, min(pb, pc));
		vec2 bmax = max(pa, max(pb, pc));
		float sbprec = 1.0 / 256.0; // note: this can be set to 1/2^subpixelPrecisionBits

		// note: this is slightly imprecise (doesn't fully match hw behavior and is both too loose and too strict)
        // cull triangles like those ones in page 54,55 of https://www.gdcvault.com/play/1023109/Optimizing-theGraphics-Pipeline-With
		culled = culled || (round(bmin.x - sbprec) == round(bmax.x) || round(bmin.y) == round(bmax.y + sbprec));

		// the computations above are only valid if all vertices are in front of perspective plane
		culled = culled && (vertexClip[a].z > 0 && vertexClip[b].z > 0 && vertexClip[c].z > 0);

		gl_MeshPrimitivesEXT[i].gl_CullPrimitiveEXT = culled;
#endif

    #if MESH_MAXTRI <= MESH_WGSIZE
		break;
	#else
		i += MESH_WGSIZE;
	#endif
    }
}

