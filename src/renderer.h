#pragma once

#if defined(_WIN32)
#include <iostream>
#include <stdio.h>
#include <algorithm>
#include <stdarg.h>
#include <functional>

#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#endif

#include "common.h"
#include "device.h"
#include "swapchain.h"
#include "resources.h"
#include "texture.h"
#include "shaders.h"
#include "math.h"
#include "config.h"
#include "GuiRenderer.h"
#include "ProfilingTools.h"
#include "scene.h"

static bool meshShadingEnabled = true;
static bool cullingEnabled = true;
static bool lodEnabled = true;
static bool occlusionEnabled = true;
static bool clusterOcclusionEnabled = true;
static bool taskShadingEnabled = false;
static bool shadingEnabled = true;
static bool shadowblurEnabled = true;
static bool shadowCheckerboard = false;
static int shadowQuality = 1;

static int debugGuiMode = 1;
static int debugLodStep = 0;
static bool reloadShaders = false;
static uint32_t reloadShadersColor = 0xffffffff;
static double reloadShadersTimer = 0;

static std::shared_ptr<Scene> scene = nullptr;

#define SHADER_PATH "shaders/"

VkSemaphore createSemaphore(VkDevice device);

VkCommandPool createCommandPool(VkDevice device, uint32_t familyIndex);

VkFence createFence(VkDevice device);

VkQueryPool createQueryPool(VkDevice device, uint32_t queryCount, VkQueryType queryType);

struct MeshDrawCommand
{
	uint32_t drawId;
	VkDrawIndexedIndirectCommand indirect; // 5 uint32_t
};

struct MeshTaskCommand
{
	uint32_t drawId;
	uint32_t taskOffset;
	uint32_t taskCount;
	uint32_t lateDrawVisibility;
	uint32_t meshletVisibilityOffset;
};

struct alignas(16) CullData
{
	mat4 view;

	float P00, P11, znear, zfar;       // symmetric projection parameters
	float frustum[4];                  // data for left/right/top/bottom frustum planes
	float lodTarget;                   // lod target error at z=1
	float pyramidWidth, pyramidHeight; // depth pyramid size in texels

	uint32_t drawCount;
	int cullingEnabled;
	int lodEnabled;
	int occlusionEnabled;
	int clusterOcclusionEnabled;
	int clusterBackfaceEnabled;

	uint32_t postPass;
};

struct alignas(16) Globals
{
	mat4 projection;
	CullData cullData;
	float screenWidth, screenHeight;
};

struct alignas(16) ShadowData
{
	vec3 sunDirection;
	float sunJitter;

	mat4 inverseViewProjection;
	vec2 imageSize;
	unsigned int checkerboard;
};

struct alignas(16) ShadeData
{
	vec3 cameraPosition;
	float pad0;
	vec3 sunDirection;
	float pad1;

	mat4 inverseViewProjection;

	vec2 imageSize;
};

static bool mousePressed = false;
static bool firstMouse = true;
static float cameraSpeed = 5.0f;
static float lastX = 400, lastY = 300;
static float pitch = 0.0f;
static float yaw = 0.0f;
static float roll = 0.0f;

static bool enableDollyZoom = false;
static float soRef = 5.0f;
static glm::vec3 cameraOriginForDolly = glm::vec3(0.0f);

// deprecated
float halfToFloat(uint16_t v);

struct alignas(16) TextData
{
	int offsetX, offsetY;
	int scale;
	unsigned int color;

	char data[112];
};

void buildBLAS(VkDevice device, const std::vector<Mesh>& meshes, const Buffer& vb, const Buffer& ib, std::vector<VkAccelerationStructureKHR>& blas, std::vector<VkDeviceSize>& compactedSizes, Buffer& blasBuffer, VkCommandPool commandPool, VkCommandBuffer commandBuffer, VkQueue queue, const VkPhysicalDeviceMemoryProperties& memoryProperties);

void compactBLAS(VkDevice device, std::vector<VkAccelerationStructureKHR>& blas, const std::vector<VkDeviceSize>& compactedSizes, Buffer& blasBuffer, VkCommandPool commandPool, VkCommandBuffer commandBuffer, VkQueue queue, const VkPhysicalDeviceMemoryProperties& memoryProperties);

VkAccelerationStructureKHR buildTLAS(VkDevice device, Buffer& tlasBuffer, const std::vector<MeshDraw>& draws, const std::vector<VkAccelerationStructureKHR>& blas, VkCommandPool commandPool, VkCommandBuffer commandBuffer, VkQueue queue, const VkPhysicalDeviceMemoryProperties& memoryProperties);

#if defined(_WIN32)
void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
#endif

mat4 perspectiveProjection(float fovY, float aspectWbyH, float zNear);

mat4 perspectiveProjectionDollyZoom(float fovY, float aspectWbyH, float zNear, float so, float soRef);

vec4 normalizePlane(vec4 p);

uint32_t previousPow2(uint32_t v);

struct pcg32_random_t
{
	uint64_t state;
	uint64_t inc;
};

#define PCG32_INITIALIZER \
	{ \
		0x853c49e6748fea9bULL, 0xda3e39cb94b95bdbULL \
	}

uint32_t pcg32_random_r(pcg32_random_t* rng);

static pcg32_random_t rngstate = PCG32_INITIALIZER;

double rand01();

uint32_t rand32();

#if defined(_WIN32)
void mouse_callback(GLFWwindow* window, double xpos, double ypos);

void mouse_button_callback(GLFWwindow* window, int button, int action, int mods);
#endif

inline static const size_t gbufferCount = 2;

class VulkanContext
{
public:
	static const std::shared_ptr<VulkanContext>& GetInstance();
	static void DestroyInstance();

#if defined(WIN32)
	void InitVulkan();
#elif defined(__ANDROID__)
	void InitVulkan(ANativeWindow* _window);
#endif

	void SetScene(const std::shared_ptr<Scene>& _scene);
	const std::shared_ptr<Scene>& GetScene() const noexcept;
	void InitResources();

	bool DrawFrame();
	void Release();

private:
	inline static std::shared_ptr<VulkanContext> gInstance = nullptr;

public:
#if defined(WIN32)
	GLFWwindow* window{ nullptr };
#elif defined(__ANDROID__)
	ANativeWindow* window{ nullptr };
#endif

	VkInstance instance{ VK_NULL_HANDLE };
	VkDevice device{ VK_NULL_HANDLE };
	VkPhysicalDevice physicalDevice{ VK_NULL_HANDLE };
	VkPhysicalDeviceProperties props = {};
	VkCommandPool commandPool{ VK_NULL_HANDLE };
	VkCommandBuffer commandBuffer{ VK_NULL_HANDLE };
	VkQueue queue{ VK_NULL_HANDLE };
	VkPhysicalDeviceMemoryProperties memoryProperties{};
	Buffer scratch{};
	Swapchain swapchain{ VK_NULL_HANDLE };
	VkSurfaceKHR surface{ VK_NULL_HANDLE };
	uint32_t graphicsFamily{ 0u };
	VkFormat swapchainFormat;
	VkDescriptorPool descriptorPool{ VK_NULL_HANDLE };

	VkPipelineRenderingCreateInfo gbufferInfo;
	Image gbufferTargets[gbufferCount] = {};
	Image depthTarget = {};
	Image shadowTarget = {};
	Image shadowblurTarget = {};

	Image depthPyramid = {};
	VkImageView depthPyramidMips[16] = {};
	uint32_t depthPyramidWidth = 0;
	uint32_t depthPyramidHeight = 0;
	uint32_t depthPyramidLevels = 0;
	std::vector<VkDescriptorSet> depthreduceSets;

	VkFormat gbufferFormats[gbufferCount] = {};
	VkFormat depthFormat{ VK_FORMAT_D32_SFLOAT };
	std::vector<VkImageView> swapchainImageViews;

	VkDebugReportCallbackEXT debugCallback;

	VkDescriptorSetLayout textureSetLayout;

	ShaderSet shaderSet;

	// pipelines
	std::function<void()> pipelinesReloadedCallback;
	VkPipelineCache pipelineCache = 0;
	VkPipeline debugtextPipeline = 0;
	VkPipeline drawcullPipeline = 0;
	VkPipeline drawculllatePipeline = 0;
	VkPipeline taskcullPipeline = 0;
	VkPipeline taskculllatePipeline = 0;
	VkPipeline tasksubmitPipeline = 0;
	VkPipeline clustersubmitPipeline = 0;
	VkPipeline clustercullPipeline = 0;
	VkPipeline clusterculllatePipeline = 0;
	VkPipeline depthreducePipeline = 0;
	VkPipeline meshPipeline = 0;
	VkPipeline meshpostPipeline = 0;
	VkPipeline meshtaskPipeline = 0;
	VkPipeline meshtasklatePipeline = 0;
	VkPipeline meshtaskpostPipeline = 0;
	VkPipeline clusterPipeline = 0;
	VkPipeline clusterpostPipeline = 0;
	VkPipeline blitPipeline = 0;
	VkPipeline shadePipeline = 0;
	VkPipeline shadowlqPipeline = 0;
	VkPipeline shadowhqPipeline = 0;
	VkPipeline shadowfillPipeline = 0;
	VkPipeline shadowblurPipeline = 0;

	// Program
	Program debugtextProgram{};
	Program drawcullProgram{};
	Program tasksubmitProgram{};
	Program clustersubmitProgram{};
	Program clustercullProgram{};
	Program depthreduceProgram{};
	Program meshProgram{};
	Program meshtaskProgram{};
	Program clusterProgram{};
	Program blitProgram{};
	Program shadeProgram{};
	Program shadowProgram{};
	Program shadowfillProgram{};
	Program shadowblurProgram{};

	// TODO: The following descriptor sets can be just temporary.
	// for cull
	std::vector<VkDescriptorSet> drawcullSets{ VK_NULL_HANDLE };
	std::vector<VkDescriptorSet> tasksubmitSets{ VK_NULL_HANDLE };

	// for render
	std::vector<VkDescriptorSet> clustercullSets{ VK_NULL_HANDLE };
	std::vector<VkDescriptorSet> clustersubmitSets{ VK_NULL_HANDLE };
	std::vector<VkDescriptorSet> clusterSets{ VK_NULL_HANDLE };
	std::vector<VkDescriptorSet> meshtaskSets{ VK_NULL_HANDLE };
	std::vector<VkDescriptorSet> meshSets{ VK_NULL_HANDLE };
	std::vector<VkDescriptorSet> shadowblurSets{ VK_NULL_HANDLE };

	// synchronization
	VkFence frameFence{ VK_NULL_HANDLE };
	VkSemaphore acquireSemaphore{ VK_NULL_HANDLE };
	std::vector<VkSemaphore> releaseSemaphores{ VK_NULL_HANDLE };

	//
	VkQueryPool queryPoolTimestamp{ VK_NULL_HANDLE };
	uint64_t timestampResults[20];
#if defined(WIN32)
	VkQueryPool queryPoolPipeline{ VK_NULL_HANDLE };
	uint64_t pipelineResults[3];
#endif

	// Buffer
	Buffer mb{};
	Buffer mtb{};
	Buffer vb{};
	Buffer ib{};
	Buffer mlb{};
	Buffer mvdb{};
	Buffer midb{};
	Buffer db{};
	Buffer dvb{};
	Buffer dcb{};
	Buffer dccb{};
	Buffer mvb{};
	Buffer cib{};
	Buffer ccb{};
	Buffer blasBuffer{};
	Buffer tlasBuffer{};

	// Ray tracing
	std::vector<VkAccelerationStructureKHR> blas;
	VkAccelerationStructureKHR tlas{ VK_NULL_HANDLE };

	// Sampler
	VkSampler textureSampler{ VK_NULL_HANDLE };
	VkSampler readSampler{ VK_NULL_HANDLE };
	VkSampler depthSampler{ VK_NULL_HANDLE };

	// Flags
	bool meshShadingSupported{ false };
	bool raytracingSupported{ false };
	bool pushDescriptorSupported{ false };
	bool dvbCleared{ false };
	bool mvbCleared{ false };

	uint32_t meshletVisibilityBytes{ 0u };

	double lastFrame{ 0.0 };

	std::shared_ptr<Scene> scene;
};

class Renderer
{
public:
	Renderer();
	~Renderer();

	bool DrawFrame();

private:
	float lastFrame{ 0.0f };
};