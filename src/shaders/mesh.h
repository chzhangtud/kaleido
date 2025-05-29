#define TASK_WGSIZE 32
#define MESH_WGSIZE 32

struct Vertex
{
	float vx, vy, vz;
    uint8_t nx, ny, nz, nw;
	float16_t tu, tv;
};

struct Meshlet
{
	// vec4 keeps Mrshlet aligned 10 16 bytes which is important because C++ has an alignas() directive
	vec3 center;
	float radius;
	int8_t coneAxis[3];
	int8_t coneCutoff;

	uint vertexOffset;
	uint triangleOffset;
	uint8_t vertexCount;
	uint8_t triangleCount;
};

struct Globals
{
	mat4 projection;
	int lodEnabled;
};

struct DrawCullData
{
	float P00, P11, znear, zfar; // symmetric projection parameters
	float frustum[4]; // data for left/right/top/bottom frustum planes
	float lodBase, lodStep; // lod distance i = base * pow(step, i)
	float pyramidWidth, pyramidHeight; // depth pyramid size in texels

	uint drawCount;
	int cullingEnabled;
	int lodEnabled;
	int occlusionEnabled;
};

struct MeshLod
{
	uint indexOffset;
	uint indexCount;
	uint meshletOffset;
	uint meshletCount;
};

struct Mesh
{
	vec3 center;
	float radius;

	uint vertexOffset;
	uint vertexCount;
	uint lodCount;
	uint placeHolder;

	MeshLod lods[8];
};

struct MeshDraw
{
	vec3 position;
	float scale;
	vec4 orientation;

	uint meshIndex;
	uint vertexOffset; // == meshes[meshIndex].vertexOffser, helps data locaclity in mesh shader
};

struct MeshDrawCommand
{
	uint drawId;

	// used by traditional raster
	uint vertexCount;
    uint instanceCount;
    uint firstVertex;
    uint firstInstance;

	// used by mesh shading path
	uint taskCount;
	uint groupCountX;
	uint groupCountY;
	uint groupCountZ;
};

struct MeshTaskPayload {
	uint drawId;
	uint meshletIndices[TASK_WGSIZE];	
};

vec3 rotateQuat(vec3 v, vec4 q)
{
	return v + 2.0 * cross(q.xyz, cross(q.xyz, v) + q.w * v);
}

