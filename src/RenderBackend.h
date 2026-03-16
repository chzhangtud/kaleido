#pragma once

#include <cstdint>
#include <memory>

// Minimal render backend abstraction used by the RenderGraph.
// Concrete backends (Vulkan / D3D / Metal / etc.) should implement these interfaces.

enum class BufferUsage : uint32_t
{
	Unknown  = 0,
	Vertex   = 1u << 0,
	Index    = 1u << 1,
	Uniform  = 1u << 2,
	Storage  = 1u << 3,
	Indirect = 1u << 4,
};

inline BufferUsage operator|(BufferUsage lhs, BufferUsage rhs)
{
	return static_cast<BufferUsage>(static_cast<uint32_t>(lhs) | static_cast<uint32_t>(rhs));
}

enum class TextureFormat : uint32_t
{
	Unknown = 0,
	R8_UNorm,
	RG8_UNorm,
	RGBA8_UNorm,
	RGBA16_Float,
	RGBA32_Float,
	D24S8,
	D32_Float,
};

enum class TextureUsage : uint32_t
{
	Unknown          = 0,
	Sampled          = 1u << 0,
	ColorAttachment  = 1u << 1,
	DepthStencil     = 1u << 2,
	Storage          = 1u << 3,
};

inline TextureUsage operator|(TextureUsage lhs, TextureUsage rhs)
{
	return static_cast<TextureUsage>(static_cast<uint32_t>(lhs) | static_cast<uint32_t>(rhs));
}

struct BufferDesc
{
	uint64_t	size        = 0;
	BufferUsage	usage       = BufferUsage::Unknown;
	bool		hostVisible = false;
};

struct TextureDesc
{
	uint32_t		width        = 0;
	uint32_t		height       = 0;
	uint32_t		mipLevels    = 1;
	TextureFormat	format      = TextureFormat::Unknown;
	TextureUsage	usage       = TextureUsage::Unknown;
};

enum class ResourceState : uint32_t
{
	Undefined = 0,
	// Generic read/write states – concrete backends map these to API-specific states.
	ShaderRead,
	ShaderWrite,
	ColorAttachment,
	DepthStencilRead,
	DepthStencilWrite,
	CopySrc,
	CopyDst,
	Present,
};

struct TextureBarrier
{
	class ITexture*	texture     = nullptr;
	ResourceState	oldState    = ResourceState::Undefined;
	ResourceState	newState    = ResourceState::Undefined;
};

struct BufferBarrier
{
	class IBuffer*	buffer      = nullptr;
	ResourceState	oldState    = ResourceState::Undefined;
	ResourceState	newState    = ResourceState::Undefined;
};

class IBuffer
{
public:
	virtual ~IBuffer() = default;

	virtual const BufferDesc& GetDesc() const = 0;
};

class ITexture
{
public:
	virtual ~ITexture() = default;

	virtual const TextureDesc& GetDesc() const = 0;
};

class ICommandList
{
public:
	virtual ~ICommandList() = default;

	// Begin recording GPU commands.
	virtual void Begin() = 0;

	// Finish recording GPU commands.
	virtual void End() = 0;

	// Simple clear helpers for RenderGraph passes.
	virtual void ClearColor(ITexture* target, const float color[4]) = 0;
	virtual void ClearDepth(ITexture* target, float depth, uint8_t stencil = 0) = 0;

	// Basic resource barriers (exact mapping is backend-specific).
	virtual void ResourceBarrier(const TextureBarrier* textureBarriers, uint32_t textureBarrierCount,
	                             const BufferBarrier* bufferBarriers, uint32_t bufferBarrierCount) = 0;

	// Dispatch / draw entry points that RenderGraph can use without touching the native API.
	virtual void Dispatch(uint32_t threadGroupCountX,
	                      uint32_t threadGroupCountY,
	                      uint32_t threadGroupCountZ) = 0;
};

class IRenderDevice
{
public:
	virtual ~IRenderDevice() = default;

	// Resource creation.
	virtual std::unique_ptr<IBuffer>  CreateBuffer(const BufferDesc& desc, const void* initialData = nullptr) = 0;
	virtual std::unique_ptr<ITexture> CreateTexture(const TextureDesc& desc, const void* initialData = nullptr) = 0;

	// Command list.
	virtual std::unique_ptr<ICommandList> CreateCommandList() = 0;

	// Submit work to GPU and optionally wait.
	virtual void Submit(ICommandList* cmdList) = 0;
	virtual void WaitIdle() = 0;
};

