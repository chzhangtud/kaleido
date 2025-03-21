#version 450

#extension GL_EXT_shader_16bit_storage: require
#extension GL_EXT_shader_8bit_storage: require
#extension GL_EXT_shader_explicit_arithmetic_types: require
#extension GL_EXT_mesh_shader: require

layout(local_size_x = 32, local_size_y = 1, local_size_z = 1) in;
layout(triangles, max_vertices = 64, max_primitives = 126) out;

#define DEBUG 1

struct Vertex
{
	float16_t vx, vy, vz, vw;
    uint8_t nx, ny, nz, nw;
	float16_t tu, tv;
};

layout(binding = 0) readonly buffer Vertices
{
    Vertex vertices[];
};

struct Meshlet
{
	uint vertices[64];
	uint8_t indices[126 * 3]; // up to 126 triangles
	uint8_t triangleCount;
	uint8_t vertexCount;
};

layout(binding = 1) readonly buffer Meshlets
{
    Meshlet meshlets[];
};

layout(location = 0) out vec4 color[];

layout(location = 1) perprimitiveEXT out vec3 triangleNormal[];

uint hash( uint a)
{
   a = (a+0x7ed55d16) + (a<<12);
   a = (a^0xc761c23c) ^ (a>>19);
   a = (a+0x165667b1) + (a<<5);
   a = (a+0xd3a2646c) ^ (a<<9);
   a = (a+0xfd7046c5) + (a<<3);
   a = (a^0xb55a4f09) ^ (a>>16);
   return a;
}

void main()
{
    uint mi = gl_WorkGroupID.x;

    uint ti = gl_LocalInvocationID.x;

#if DEBUG
    uint mhash = hash(mi);
    vec3 mcolor = vec3(float(mhash & 255), float((mhash >> 8) & 255),  float((mhash >> 16) & 255)) / 255.0;
#endif

    SetMeshOutputsEXT(meshlets[mi].vertexCount, meshlets[mi].triangleCount);
    
    for (uint i = ti; i < uint(meshlets[mi].vertexCount); i += 32)
    {
        uint vi = meshlets[mi].vertices[i];

        Vertex v = vertices[vi];

        vec3 position = vec3(v.vx, v.vy, v.vz);
        vec3 normal = vec3(v.nx, v.ny, v.nz) / 127.0 - 1.0;
        vec2 texcoord = vec2(v.tu, v.tv);

        gl_MeshVerticesEXT[i].gl_Position = vec4(position * 0.5 + vec3(0.0, 0.0, 0.5), 1.0);

        color[i] = vec4(normal * 0.5 + vec3(0.5), 1.0);
#if DEBUG
        color[i] = vec4(mcolor, 1.0);
#endif
    }
    
    for (uint i = ti; i < uint(meshlets[mi].triangleCount); i += 32)
    {
        uint vi0 = meshlets[mi].vertices[uint(meshlets[mi].indices[3 * i + 0])];
        uint vi1 = meshlets[mi].vertices[uint(meshlets[mi].indices[3 * i + 1])];
        uint vi2 = meshlets[mi].vertices[uint(meshlets[mi].indices[3 * i + 2])];
        
        vec3 position0 = vec3(vertices[vi0].vx, vertices[vi0].vy, vertices[vi0].vz);
        vec3 position1 = vec3(vertices[vi1].vx, vertices[vi1].vy, vertices[vi1].vz);
        vec3 position2 = vec3(vertices[vi2].vx, vertices[vi2].vy, vertices[vi2].vz);

        vec3 normal = normalize(cross(position1 - position0, position2 - position0));

        triangleNormal[i] = normal;
    }
    
    for (uint i = ti; i < meshlets[mi].triangleCount; i += 32)
    {
        // Notice: In GL_NV_mesh_shader people can use writePackedPrimitiveIndices4x8NV for saving packed indices more tightly (4 uint8_t index -> uint), but this doesn't seem to have significant impact on fps here.
        gl_PrimitiveTriangleIndicesEXT[i] = uvec3(meshlets[mi].indices[3*i], meshlets[mi].indices[3*i+1], meshlets[mi].indices[3*i+2]);
    }
}
