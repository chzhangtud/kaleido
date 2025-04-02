struct Vertex
{
	float vx, vy, vz;
    uint8_t nx, ny, nz, nw;
	float16_t tu, tv;
};

struct Meshlet
{
	vec4 cone;
	uint vertexOffset;
	uint triangleOffset;
	uint8_t vertexCount;
	uint8_t triangleCount;
};

struct TaskPayload {
	uint meshletIndices[32];
};