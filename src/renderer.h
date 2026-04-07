#pragma once

#include <string>
#include <unordered_map>

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
#include "RenderResourceManager.h"
#include "texture.h"
#include "shaders.h"
#include "math.h"
#include "config.h"
#include "GuiRenderer.h"
#include "ProfilingTools.h"
#include "scene.h"
#include "scenert.h"

static bool meshShadingEnabled = true;
static bool cullingEnabled = true;
static bool lodEnabled = true;
static bool occlusionEnabled = true;
static bool clusterOcclusionEnabled = true;
static bool taskShadingEnabled = false;
static bool shadowEnabled = true;
static bool shadowblurEnabled = true;
static bool taaEnabled = true;
static float taaBlendAlpha = 0.1f;
static bool shadowCheckerboard = false;
static int shadowQuality = 1;
static bool animationEnabled = false;
static bool clusterRTEnabled = false;
static int debugGuiMode = 1;
static int debugLodStep = 0;
static bool reloadShaders = false;
static uint32_t reloadShadersColor = 0xffffffff;
static double reloadShadersTimer = 0;
static bool debugSleep = false;

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
	// 0 = lit G-buffer, 1 = debug wireframe (fragment + line rasterization), 2 = meshlet random color
	uint32_t gbufferDebugMode;
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
	int shadowEnabled;

	mat4 inverseViewProjection;

	vec2 imageSize;
};

struct alignas(16) TaaData
{
	vec2 imageSize;
	int historyValid;
	float blendAlpha;
};

static bool mousePressed = false;
static bool firstMouse = true;
static float cameraSpeed = 5.0f;
static float lastX = 400, lastY = 300;
static float pitch = 0.0f;
static float yaw = 0.0f;
static float roll = 0.0f;
static bool cameraDirty = true;

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

// Generic input handlers for camera control (platform-independent).
// On desktop they are normally driven by mouse, on Android by touch.
void OnPointerDown(float x, float y);
void OnPointerMove(float x, float y);
void OnPointerUp();

// Virtual stick input coming from Android (left: move, right: look).
void SetVirtualSticks(float moveX, float moveY, float lookX, float lookY);

void updateCamera();

inline static const size_t gbufferCount = 2;

struct RGPassContext;

// External images referenced by RenderGraph (map key is the logical name).
struct RGExternalImageRegistryEntry
{
	VkImage image = VK_NULL_HANDLE;
	TextureFormat format = TextureFormat::Unknown;
	TextureUsage usage = TextureUsage::Unknown;
};

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
	void SetRuntimeUiEnabled(bool enabled);
	bool IsRuntimeUiEnabled() const noexcept;
	void SetEditorViewportMode(bool enabled);
	bool IsEditorViewportMode() const noexcept;
	void RequestEditorSceneLoad(const std::string& scenePath);
	bool ConsumeEditorSceneLoadRequest(std::string& outScenePath);
	void ResetSceneResourcesForReload();

	bool DrawFrame();
	void Release();

	// Cleared each frame before registering externals used by the RenderGraph.
	void ClearRenderGraphExternalImages();
	void RegisterRenderGraphExternalImage(const std::string& name, VkImage image, TextureFormat format, TextureUsage usage);

private:
	void PrepareRenderGraphPassContext(RGPassContext& out, VkCommandBuffer commandBuffer, uint64_t frameIndex, uint32_t swapchainImageIndex);
	void BuildRuntimeUi(float deltaTime,
		double frameCPUAvg,
		double frameGPUAvg,
		double cullGPUTime,
		double pyramidGPUTime,
		double culllateGPUTime,
		double renderGPUTime,
		double renderlateGPUTime,
		double taaGPUTime);

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
	VkCommandPool commandPools[MAX_FRAMES]{ VK_NULL_HANDLE };
	VkCommandBuffer commandBuffers[MAX_FRAMES]{ VK_NULL_HANDLE };
	VkQueue queue{ VK_NULL_HANDLE };
	VkPhysicalDeviceMemoryProperties memoryProperties{};
	RenderResourceManager resourceManager;
	Buffer scratch{};
	Swapchain swapchain{ VK_NULL_HANDLE };
	VkSurfaceKHR surface{ VK_NULL_HANDLE };
	uint32_t graphicsFamily{ 0u };
	VkFormat swapchainFormat;
	VkDescriptorPool descriptorPool{ VK_NULL_HANDLE };

	VkPipelineRenderingCreateInfo gbufferInfo;
	RGTextureHandle gbufferTargetHandles[gbufferCount] = {};
	RGTextureHandle depthTargetHandle{};
	RGTextureHandle shadowTargetHandle{};
	RGTextureHandle shadowblurTargetHandle{};
	// Opaque-lit result before the transparency G-buffer pass; used as refraction background (HDR path later).
	RGTextureHandle sceneColorHDRHandle{};
	RGTextureHandle taaHistoryHandles[2] = {};

	RGTextureHandle depthPyramidHandle{};
	VkImageView depthPyramidMips[16] = {};
	uint32_t depthPyramidWidth = 0;
	uint32_t depthPyramidHeight = 0;
	uint32_t depthPyramidLevels = 0;
	std::vector<VkDescriptorSet> depthreduceSets[MAX_FRAMES];

	VkFormat gbufferFormats[gbufferCount] = {};
	VkFormat depthFormat{ VK_FORMAT_D32_SFLOAT };
	std::vector<VkImageView> swapchainImageViews;
	std::unordered_map<std::string, RGExternalImageRegistryEntry> rgExternalImageRegistry;

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
	VkPipeline meshWirePipeline = 0;
	VkPipeline meshpostWirePipeline = 0;
	VkPipeline meshtaskPipeline = 0;
	VkPipeline meshtasklatePipeline = 0;
	VkPipeline meshtaskpostPipeline = 0;
	VkPipeline meshtaskWirePipeline = 0;
	VkPipeline meshtasklateWirePipeline = 0;
	VkPipeline meshtaskpostWirePipeline = 0;
	VkPipeline clusterPipeline = 0;
	VkPipeline clusterpostPipeline = 0;
	VkPipeline clusterWirePipeline = 0;
	VkPipeline clusterpostWirePipeline = 0;
	VkPipeline finalPipeline = 0;
	VkPipeline taaPipeline = 0;
	VkPipeline shadowlqPipeline = 0;
	VkPipeline shadowhqPipeline = 0;
	VkPipeline shadowfillPipeline = 0;
	VkPipeline shadowblurPipeline = 0;

	std::vector<VkPipeline> pipelines;

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
	Program finalProgram{};
	Program taaProgram{};
	Program shadowProgram{};
	Program shadowfillProgram{};
	Program shadowblurProgram{};

	// TODO: The following descriptor sets can be just temporary.
	// for cull
	VkDescriptorSet drawcullSets[MAX_FRAMES][DESCRIPTOR_SET_PER_FRAME];
	VkDescriptorSet tasksubmitSets[MAX_FRAMES][DESCRIPTOR_SET_PER_FRAME];

	// for render
	VkDescriptorSet clustercullSets[MAX_FRAMES][DESCRIPTOR_SET_PER_FRAME];
	VkDescriptorSet clustersubmitSets[MAX_FRAMES][DESCRIPTOR_SET_PER_FRAME];
	VkDescriptorSet clusterSets[MAX_FRAMES][DESCRIPTOR_SET_PER_FRAME];
	VkDescriptorSet meshtaskSets[MAX_FRAMES][DESCRIPTOR_SET_PER_FRAME];
	VkDescriptorSet meshSets[MAX_FRAMES][DESCRIPTOR_SET_PER_FRAME];
	VkDescriptorSet shadowblurSets[MAX_FRAMES][DESCRIPTOR_SET_PER_FRAME];

	// synchronization
	VkFence frameFences[MAX_FRAMES]{ VK_NULL_HANDLE };
	VkSemaphore acquireSemaphores[MAX_FRAMES]{ VK_NULL_HANDLE };
	std::vector<VkSemaphore> releaseSemaphores[MAX_FRAMES];

	//
	VkQueryPool queryPoolsTimestamp[MAX_FRAMES]{};
	uint64_t timestampResults[25];
#if defined(WIN32)
	VkQueryPool queryPoolsPipeline[MAX_FRAMES]{};
	uint64_t pipelineResults[3];
#endif

	// Buffer
	Buffer mb{};
	Buffer mtb{};
	Buffer vb{};
	Buffer ib{};
	Buffer mlb{};
	Buffer mdb{};
	Buffer db{};
	Buffer dvb{};
	Buffer dcb{};
	Buffer dccb{};
	Buffer mvb{};
	Buffer cib{};
	Buffer ccb{};
	Buffer blasBuffer{};
	Buffer tlasBuffer{};
	Buffer tlasScratchBuffer{};
	Buffer tlasInstanceBuffer{};

	// Ray tracing
	std::vector<VkAccelerationStructureKHR> blas;
	std::vector<VkDeviceAddress> blasAddresses;
	VkAccelerationStructureKHR tlas{ VK_NULL_HANDLE };

	// Sampler
	VkSampler textureSampler{ VK_NULL_HANDLE };
	VkSampler readSampler{ VK_NULL_HANDLE };
	VkSampler depthSampler{ VK_NULL_HANDLE };

	// Flags
	bool tlasNeedsRebuild{ true };
	bool meshShadingSupported{ false };
	bool raytracingSupported{ false };
	bool clusterrtSupported = false;
	bool pushDescriptorSupported{ false };
	bool wireframeDebugSupported{ false };
	// ImGui combo index; must match push constant gbufferDebugMode values
	int gbufferDebugViewMode{ 0 };
	bool dvbCleared{ false };
	bool mvbCleared{ false };

	uint32_t meshletVisibilityBytes{ 0u };

	double lastFrame{ 0.0 };

	std::shared_ptr<Scene> scene;
	bool runtimeUiEnabled = true;
	bool editorViewportMode = false;
	uint32_t currentRenderWidth = 0;
	uint32_t currentRenderHeight = 0;
	uint32_t editorViewportWidth = 0;
	uint32_t editorViewportHeight = 0;
	RGTextureHandle editorViewportTargetHandle{};
	VkDescriptorSet editorViewportDescriptorSet = VK_NULL_HANDLE;
	struct PendingViewportDescriptorRelease
	{
		VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
		uint64_t safeAfterFrame = 0;
	};
	std::vector<PendingViewportDescriptorRelease> pendingViewportDescriptorReleases;
	uint64_t pendingTexturePoolPurgeAfterFrame = 0;
	bool editorSceneLoadRequested = false;
	std::string editorSceneLoadRequestPath;
	bool frameResourcesInitialized = false;
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