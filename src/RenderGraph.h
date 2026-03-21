#pragma once

#include <functional>
#include <map>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "RenderResourceManager.h"
#include "common.h"

enum class RGLoadOp
{
	Load,
	Clear,
	DontCare
};

enum class RGStoreOp
{
	Store,
	DontCare
};

struct RGLoadStoreOp
{
	RGLoadOp  load = RGLoadOp::Load;
	RGStoreOp store = RGStoreOp::Store;
};

struct RGTextureWrite
{
	RGTextureHandle handle{};
	RGLoadStoreOp   op{};
	ResourceState   state = ResourceState::Undefined;
};

struct RGExternalTextureWrite
{
	std::string  name;
	RGLoadStoreOp op{};
	ResourceState state = ResourceState::Undefined;
};

struct RGImageBarrier
{
	RGTextureHandle handle{};
	ResourceState   oldState = ResourceState::Undefined;
	ResourceState   newState = ResourceState::Undefined;
};

struct RGPassContext
{
	RenderResourceManager* resourceManager = nullptr;
	VkCommandBuffer        commandBuffer = VK_NULL_HANDLE;
	uint64_t               frameIndex = 0;
	std::function<void(VkCommandBuffer, const std::vector<RGImageBarrier>&)> insertImageBarriers;
};

struct RGPass
{
	std::string                         name;
	std::vector<RGTextureHandle>        readTextures;
	std::vector<ResourceState>          readTextureStates;
	std::vector<RGTextureHandle>        readTexturesFromPreviousFrame;  // No producer dependency
	std::vector<RGTextureWrite>         writeTextures;
	std::vector<std::string>            readExternalTextures;
	std::vector<RGExternalTextureWrite> writeExternalTextures;
	std::function<void(RGPassContext&)> execute;
};

class RGPassBuilder
{
public:
	explicit RGPassBuilder(RGPass& pass)
	    : m_pass(pass)
	{
	}

	void readTexture(RGTextureHandle handle);
	void readTexture(RGTextureHandle handle, ResourceState state);
	void readTextureFromPreviousFrame(RGTextureHandle handle);  // Read from history, no producer dependency
	void writeTexture(RGTextureHandle handle, RGLoadStoreOp op = {});
	void writeTexture(RGTextureHandle handle, ResourceState state, RGLoadStoreOp op = {});
	void readExternalTexture(const std::string& name);
	void readExternalTexture(const std::string& name, ResourceState state);
	void writeExternalTexture(const std::string& name, RGLoadStoreOp op = {});
	void writeExternalTexture(const std::string& name, ResourceState state, RGLoadStoreOp op = {});

private:
	RGPass& m_pass;
};

// Resource dependency record: producer pass indices + consumer pass indices.
struct RGResourceRecord
{
	std::vector<size_t> producerPassIndices;
	std::vector<size_t> consumerPassIndices;
};

class RenderGraph
{
public:
	using SetupCallback = std::function<void(RGPassBuilder&)>;
	using ExecuteCallback = std::function<void(RGPassContext&)>;

	void clear();
	void addPass(const std::string& name, const SetupCallback& setup, const ExecuteCallback& execute);
	void execute(RGPassContext& context) const;

	const std::vector<RGPass>& getPasses() const { return m_passes; }

	// Build resource dependency map and print debug info (inputs/outputs per pass).
	void buildResourceDependencyMap(
	    std::map<uint32_t, RGResourceRecord>& outTextureRecords,
	    std::map<std::string, RGResourceRecord>& outExternalTextureRecords) const;

	// Topological sort of passes. Returns sorted pass indices. On cycle, logs error and returns add order.
	std::vector<size_t> getTopologicalOrder() const;

private:
	std::vector<RGPass> m_passes;
};
