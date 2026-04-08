#include "../config.h"

struct Vertex
{
	float16_t vx, vy, vz;
	uint16_t tp; // packed tangent: 8-8 octahedral
	uint np;     // packed normal: 10-10-10-2 vector + bitangent sign
	float16_t tu, tv;
};

struct Meshlet
{
	// vec4 keeps Mrshlet aligned 10 16 bytes which is important because C++ has an alignas() directive
	vec3 center;
	float radius;
	int8_t coneAxis[3];
	int8_t coneCutoff;

	uint dataOffset;
	uint baseVertex;
	uint8_t vertexCount;
	uint8_t triangleCount;
	uint8_t shortRefs;
};

struct CullData
{
	mat4 view;

	float P00, P11, znear, zfar;		// symmetric projection parameters
	float frustum[4];					// data for left/right/top/bottom frustum planes
	float lodTarget;					// lod target error at z=1
	float pyramidWidth, pyramidHeight;	// depth pyramid size in texels

	uint drawCount;
	int cullingEnabled;
	int lodEnabled;
	int occlusionEnabled;
	int clusterOcclusionEnabled;
	int clusterBackfaceEnabled;

	uint postPass;
};

struct Globals
{
	mat4 projection;
	CullData cullData;
	float screenWidth, screenHeight;
	// 0 = lit, 1 = debug wireframe, 2 = meshlet random color
	uint gbufferDebugMode;
};

struct MeshLod
{
	uint indexOffset;
	uint indexCount;
	uint meshletOffset;
	uint meshletCount;
	float error;
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

struct Material
{
	uint albedoTexture;
	uint normalTexture;
	uint pbrTexture;
	uint emissiveTexture;
	uint occlusionTexture;
	uint texturePad[3];

	vec4 baseColorFactor;
	vec4 pbrFactor; // MR: (unused, unused, metallic, roughness), SG: (specular.rgb, glossiness)
	vec3 emissiveFactor;
	uint workflow; // 0: default, 1: metallic-roughness, 2: specular-glossiness fallback

	vec4 shadingParams; // x: normal scale, y: occlusion strength, z: alpha cutoff, w: emissive strength
	uint alphaMode;     // 0 opaque, 1 mask, 2 blend
	uint transmissionTexture;
	float transmissionFactor;
	float ior;
};

struct MeshDraw
{
	vec3 position;
	float scale;
	vec4 orientation;

	uint meshIndex;
	uint meshletVisibilityOffset;
	uint postPass;
	uint flags;

	uint materialIndex;
};

struct MeshDrawCommand
{
	uint drawId;

	// VkDrawIndexedIndirectCommand
	uint indexCount;
	uint instanceCount;
	uint firstIndex;
	uint vertexOffset;
	uint firstInstance;
};

struct MeshTaskCommand
{
	uint drawId;
	uint taskOffset;
	uint taskCount;
	uint lateDrawVisibility;
	uint meshletVisibilityOffset;
};

struct MeshTaskPayload
{
	uint clusterIndices[TASK_WGSIZE];	
};
