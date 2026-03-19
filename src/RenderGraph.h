#pragma once

#include <functional>
#include <string>
#include <vector>

#include "RenderResourceManager.h"

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
};

struct RGExternalTextureWrite
{
	std::string  name;
	RGLoadStoreOp op{};
};

struct RGPassContext
{
	RenderResourceManager* resourceManager = nullptr;
	VkCommandBuffer        commandBuffer = VK_NULL_HANDLE;
	uint64_t               frameIndex = 0;
};

struct RGPass
{
	std::string                         name;
	std::vector<RGTextureHandle>        readTextures;
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
	void writeTexture(RGTextureHandle handle, RGLoadStoreOp op = {});
	void readExternalTexture(const std::string& name);
	void writeExternalTexture(const std::string& name, RGLoadStoreOp op = {});

private:
	RGPass& m_pass;
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

private:
	std::vector<RGPass> m_passes;
};
