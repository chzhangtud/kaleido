#include "RenderGraph.h"

void RGPassBuilder::readTexture(RGTextureHandle handle)
{
	m_pass.readTextures.push_back(handle);
}

void RGPassBuilder::writeTexture(RGTextureHandle handle, RGLoadStoreOp op)
{
	m_pass.writeTextures.push_back({ handle, op });
}

void RGPassBuilder::readExternalTexture(const std::string& name)
{
	m_pass.readExternalTextures.push_back(name);
}

void RGPassBuilder::writeExternalTexture(const std::string& name, RGLoadStoreOp op)
{
	m_pass.writeExternalTextures.push_back({ name, op });
}

void RenderGraph::clear()
{
	m_passes.clear();
}

void RenderGraph::addPass(const std::string& name, const SetupCallback& setup, const ExecuteCallback& execute)
{
	RGPass pass{};
	pass.name = name;
	pass.execute = execute;

	RGPassBuilder builder(pass);
	if (setup)
		setup(builder);

	m_passes.push_back(std::move(pass));
}

void RenderGraph::execute(RGPassContext& context) const
{
	for (size_t i = 0; i < m_passes.size(); ++i)
	{
		const RGPass& pass = m_passes[i];
		if (pass.execute)
			pass.execute(context);
	}
}
