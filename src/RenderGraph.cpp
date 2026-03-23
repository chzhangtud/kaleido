#include "RenderGraph.h"

#include <algorithm>
#include <numeric>
#include <queue>

namespace
{
std::string join(const std::vector<std::string>& parts, const std::string& sep)
{
	std::string result;
	for (size_t i = 0; i < parts.size(); ++i)
		result += (i ? sep : "") + parts[i];
	return result;
}

// Consumer C depends on: last producer before C in add order, or last producer if none (cycle).
size_t getProducerDependency(size_t consumer, const std::vector<size_t>& producers)
{
	size_t lastBefore = SIZE_MAX;
	size_t lastAny = SIZE_MAX;
	for (size_t p : producers)
	{
		if (p < consumer)
			lastBefore = (lastBefore == SIZE_MAX) ? p : std::max(lastBefore, p);
		lastAny = (lastAny == SIZE_MAX) ? p : std::max(lastAny, p);
	}
	return (lastBefore != SIZE_MAX) ? lastBefore : lastAny;
}

bool recordTouchesCycle(const RGResourceRecord& rec, const std::unordered_set<size_t>& cyclePasses)
{
	auto inCycle = [&](size_t idx) { return cyclePasses.count(idx) != 0; };
	bool hasProd = std::any_of(rec.producerPassIndices.begin(), rec.producerPassIndices.end(), inCycle);
	bool hasCons = std::any_of(rec.consumerPassIndices.begin(), rec.consumerPassIndices.end(), inCycle);
	return hasProd && hasCons;
}

bool hasUsage(TextureUsage usage, TextureUsage bit)
{
	return (static_cast<uint32_t>(usage) & static_cast<uint32_t>(bit)) != 0;
}

ResourceState inferReadState(const RenderResourceManager& rm, RGTextureHandle handle)
{
	const RGTextureDesc* desc = rm.GetTextureDesc(handle);
	if (!desc)
		return ResourceState::ShaderRead;
	if (hasUsage(desc->usage, TextureUsage::DepthStencil))
		return ResourceState::DepthStencilRead;
	// Shader sampling / storage wins over TransferSrc: many images declare TransferSrc for occasional copies
	// but are read in compute as GENERAL (e.g. depth pyramid).
	if (hasUsage(desc->usage, TextureUsage::Sampled) || hasUsage(desc->usage, TextureUsage::Storage))
		return ResourceState::ShaderRead;
	if (hasUsage(desc->usage, TextureUsage::TransferSrc))
		return ResourceState::CopySrc;
	return ResourceState::ShaderRead;
}

ResourceState inferWriteState(const RenderResourceManager& rm, RGTextureHandle handle)
{
	const RGTextureDesc* desc = rm.GetTextureDesc(handle);
	if (!desc)
		return ResourceState::ShaderWrite;
	if (hasUsage(desc->usage, TextureUsage::ColorAttachment))
		return ResourceState::ColorAttachment;
	if (hasUsage(desc->usage, TextureUsage::DepthStencil))
		return ResourceState::DepthStencilWrite;
	if (hasUsage(desc->usage, TextureUsage::TransferDst))
		return ResourceState::CopyDst;
	return ResourceState::ShaderWrite;
}

bool isReadState(ResourceState state)
{
	return state == ResourceState::ShaderRead || state == ResourceState::DepthStencilRead || state == ResourceState::CopySrc;
}

const char* stateToString(ResourceState state)
{
	switch (state)
	{
	case ResourceState::Undefined:         return "Undefined";
	case ResourceState::ShaderRead:        return "ShaderRead";
	case ResourceState::ShaderWrite:       return "ShaderWrite";
	case ResourceState::ColorAttachment:   return "ColorAttachment";
	case ResourceState::DepthStencil:      return "DepthStencil";
	case ResourceState::DepthStencilRead:  return "DepthStencilRead";
	case ResourceState::DepthStencilWrite: return "DepthStencilWrite";
	case ResourceState::CopySrc:           return "CopySrc";
	case ResourceState::CopyDst:           return "CopyDst";
	case ResourceState::Present:           return "Present";
	default:                               return "Unknown";
	}
}
}  // namespace

void RGPassBuilder::readTexture(RGTextureHandle handle)
{
	m_pass.readTextures.push_back(handle);
	m_pass.readTextureStates.push_back(ResourceState::Undefined);
}

void RGPassBuilder::readTexture(RGTextureHandle handle, ResourceState state)
{
	m_pass.readTextures.push_back(handle);
	m_pass.readTextureStates.push_back(state);
}

void RGPassBuilder::readTextureFromPreviousFrame(RGTextureHandle handle)
{
	m_pass.readTexturesFromPreviousFrame.push_back(handle);
}

void RGPassBuilder::writeTexture(RGTextureHandle handle, RGLoadStoreOp op)
{
	m_pass.writeTextures.push_back({ handle, op, ResourceState::Undefined });
}

void RGPassBuilder::writeTexture(RGTextureHandle handle, ResourceState state, RGLoadStoreOp op)
{
	m_pass.writeTextures.push_back({ handle, op, state });
}

void RGPassBuilder::readExternalTexture(const std::string& name)
{
	m_pass.readExternalTextures.push_back(name);
	m_pass.readExternalTextureStates.push_back(ResourceState::Undefined);
}

void RGPassBuilder::readExternalTexture(const std::string& name, ResourceState state)
{
	m_pass.readExternalTextures.push_back(name);
	m_pass.readExternalTextureStates.push_back(state);
}

void RGPassBuilder::writeExternalTexture(const std::string& name, RGLoadStoreOp op)
{
	m_pass.writeExternalTextures.push_back({ name, op, ResourceState::Undefined });
}

void RGPassBuilder::writeExternalTexture(const std::string& name, ResourceState state, RGLoadStoreOp op)
{
	m_pass.writeExternalTextures.push_back({ name, op, state });
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

void RenderGraph::buildResourceDependencyMap(
    std::map<uint32_t, RGResourceRecord>& outTextureRecords,
    std::map<std::string, RGResourceRecord>& outExternalTextureRecords) const
{
	outTextureRecords.clear();
	outExternalTextureRecords.clear();

	for (size_t passIdx = 0; passIdx < m_passes.size(); ++passIdx)
	{
		const RGPass& pass = m_passes[passIdx];

		for (RGTextureHandle h : pass.readTextures)
			if (h.IsValid())
				outTextureRecords[h.id].consumerPassIndices.push_back(passIdx);

		for (const RGTextureWrite& w : pass.writeTextures)
			if (w.handle.IsValid())
				outTextureRecords[w.handle.id].producerPassIndices.push_back(passIdx);

		for (const std::string& name : pass.readExternalTextures)
			if (!name.empty())
				outExternalTextureRecords[name].consumerPassIndices.push_back(passIdx);

		for (const RGExternalTextureWrite& w : pass.writeExternalTextures)
			if (!w.name.empty())
				outExternalTextureRecords[w.name].producerPassIndices.push_back(passIdx);
	}

	static bool s_loggedOnce = false;
	if (!s_loggedOnce)
	{
		s_loggedOnce = true;
		LOGI("RenderGraph: Pass inputs/outputs:");
		for (size_t i = 0; i < m_passes.size(); ++i)
		{
			const RGPass& p = m_passes[i];
			std::vector<std::string> inputs, outputs;
			for (RGTextureHandle h : p.readTextures)
				inputs.push_back("tex:" + std::to_string(h.id));
			for (RGTextureHandle h : p.readTexturesFromPreviousFrame)
				inputs.push_back("tex:" + std::to_string(h.id) + "(prev)");
			for (const std::string& n : p.readExternalTextures)
				inputs.push_back("ext:\"" + n + "\"");
			for (const RGTextureWrite& w : p.writeTextures)
				outputs.push_back("tex:" + std::to_string(w.handle.id));
			for (const RGExternalTextureWrite& w : p.writeExternalTextures)
				outputs.push_back("ext:\"" + w.name + "\"");
			std::string inStr = join(inputs, ", ");
			std::string outStr = join(outputs, ", ");
			LOGI("  [%zu] %s: inputs=[%s], outputs=[%s]", i, p.name.c_str(),
			    inStr.empty() ? "(none)" : inStr.c_str(),
			    outStr.empty() ? "(none)" : outStr.c_str());
		}
	}
}

std::vector<size_t> RenderGraph::getTopologicalOrder() const
{
	const size_t n = m_passes.size();
	if (n == 0)
		return {};

	std::map<uint32_t, RGResourceRecord> texRecords;
	std::map<std::string, RGResourceRecord> extRecords;
	buildResourceDependencyMap(texRecords, extRecords);

	std::vector<int> inDegree(n, 0);
	std::vector<std::vector<size_t>> outEdges(n);
	std::unordered_set<size_t> addedEdges;

	auto addEdge = [&](size_t from, size_t to)
	{
		if (from == to) return;
		if (addedEdges.insert(from * n + to).second)
		{
			outEdges[from].push_back(to);
			inDegree[to]++;
		}
	};

	auto addResourceDeps = [&](const RGResourceRecord& rec)
	{
		std::vector<size_t> prods = rec.producerPassIndices;
		std::sort(prods.begin(), prods.end());

		for (size_t i = 0; i + 1 < prods.size(); ++i)
			addEdge(prods[i], prods[i + 1]);

		for (size_t cons : rec.consumerPassIndices)
		{
			size_t dep = getProducerDependency(cons, prods);
			if (dep != SIZE_MAX)
				addEdge(dep, cons);
		}
	};

	for (const auto& [id, rec] : texRecords)
		addResourceDeps(rec);
	for (const auto& [name, rec] : extRecords)
		addResourceDeps(rec);

	// Kahn's algorithm
	std::vector<size_t> sorted;
	sorted.reserve(n);
	std::queue<size_t> q;
	for (size_t i = 0; i < n; ++i)
		if (inDegree[i] == 0)
			q.push(i);

	while (!q.empty())
	{
		size_t u = q.front();
		q.pop();
		sorted.push_back(u);
		for (size_t v : outEdges[u])
			if (--inDegree[v] == 0)
				q.push(v);
	}

	if (sorted.size() != n)
	{
		std::unordered_set<size_t> cyclePasses;
		std::vector<std::string> cycleNames, relatedResources;

		for (size_t i = 0; i < n; ++i)
			if (inDegree[i] > 0)
			{
				cyclePasses.insert(i);
				cycleNames.push_back(m_passes[i].name + " (idx=" + std::to_string(i) + ")");
			}

		for (const auto& [id, rec] : texRecords)
			if (recordTouchesCycle(rec, cyclePasses))
				relatedResources.push_back("texture(id=" + std::to_string(id) + ")");
		for (const auto& [name, rec] : extRecords)
			if (recordTouchesCycle(rec, cyclePasses))
				relatedResources.push_back("external:\"" + name + "\"");

		LOGE("RenderGraph cycle detected: %zu pass(es). Passes: %s. Related resources: %s",
		    cyclePasses.size(), join(cycleNames, ", ").c_str(), join(relatedResources, ", ").c_str());

		std::vector<size_t> fallback(n);
		std::iota(fallback.begin(), fallback.end(), 0);
		return fallback;
	}

	return sorted;
}

void RenderGraph::execute(RGPassContext& context) const
{
	std::vector<size_t> order = getTopologicalOrder();
	std::vector<std::vector<RGImageBarrier>> barriersBeforePass(m_passes.size());
	std::vector<std::vector<RGExternalImageBarrier>> externalBarriersBeforePass(m_passes.size());

	static bool s_orderLoggedOnce = false;
	if (!s_orderLoggedOnce)
	{
		s_orderLoggedOnce = true;
		LOGI("RenderGraph: Executing passes in topological order:");
		for (size_t idx : order)
			LOGI("  -> %s", m_passes[idx].name.c_str());
	}

	struct ResourcePassState
	{
		bool          touched = false;
		ResourceState beginState = ResourceState::Undefined;
		ResourceState endState = ResourceState::Undefined;
	};

	std::vector<std::unordered_map<uint32_t, ResourcePassState>> passStates(m_passes.size());
	std::vector<std::unordered_map<std::string, ResourcePassState>> passExternalStates(m_passes.size());
	for (size_t passIdx : order)
	{
		const RGPass& pass = m_passes[passIdx];
		auto& states = passStates[passIdx];
		auto& externalStates = passExternalStates[passIdx];

		for (size_t i = 0; i < pass.readTextures.size(); ++i)
		{
			RGTextureHandle handle = pass.readTextures[i];
			if (!handle.IsValid())
				continue;

			ResourceState state = ResourceState::Undefined;
			if (i < pass.readTextureStates.size())
				state = pass.readTextureStates[i];
			if (state == ResourceState::Undefined && context.resourceManager)
				state = inferReadState(*context.resourceManager, handle);

			ResourcePassState& s = states[handle.id];
			if (!s.touched)
			{
				s.touched = true;
				s.beginState = state;
				s.endState = state;
			}
			else
			{
				if (isReadState(s.endState))
					s.endState = state;
			}
		}

		// Cross-frame reads must still get layout barriers (same as readTexture).
		for (RGTextureHandle handle : pass.readTexturesFromPreviousFrame)
		{
			if (!handle.IsValid())
				continue;

			ResourceState state = ResourceState::ShaderRead;
			if (context.resourceManager)
				state = inferReadState(*context.resourceManager, handle);

			ResourcePassState& s = states[handle.id];
			if (!s.touched)
			{
				s.touched = true;
				s.beginState = state;
				s.endState = state;
			}
			else
			{
				if (isReadState(s.endState))
					s.endState = state;
			}
		}

		for (const RGTextureWrite& w : pass.writeTextures)
		{
			if (!w.handle.IsValid())
				continue;

			ResourceState state = w.state;
			if (state == ResourceState::Undefined && context.resourceManager)
				state = inferWriteState(*context.resourceManager, w.handle);

			ResourcePassState& s = states[w.handle.id];
			if (!s.touched)
			{
				s.touched = true;
				s.beginState = state;
				s.endState = state;
			}
			else
			{
				s.endState = state;
			}
		}

		for (size_t i = 0; i < pass.readExternalTextures.size(); ++i)
		{
			const std::string& name = pass.readExternalTextures[i];
			if (name.empty())
				continue;

			ResourceState state = (i < pass.readExternalTextureStates.size()) ? pass.readExternalTextureStates[i] : ResourceState::Undefined;
			if (state == ResourceState::Undefined)
				state = ResourceState::ShaderRead;

			ResourcePassState& s = externalStates[name];
			if (!s.touched)
			{
				s.touched = true;
				s.beginState = state;
				s.endState = state;
			}
			else
			{
				if (isReadState(s.endState))
					s.endState = state;
			}
		}

		for (const RGExternalTextureWrite& w : pass.writeExternalTextures)
		{
			if (w.name.empty())
				continue;

			ResourceState state = w.state;
			if (state == ResourceState::Undefined)
				state = ResourceState::ShaderWrite;

			ResourcePassState& s = externalStates[w.name];
			if (!s.touched)
			{
				s.touched = true;
				s.beginState = state;
				s.endState = state;
			}
			else
			{
				s.endState = state;
			}
		}
	}

	std::unordered_map<uint32_t, ResourceState> lastKnownState;
	std::unordered_map<std::string, ResourceState> lastKnownExternalState;
	for (size_t passIdx : order)
	{
		const auto& currStates = passStates[passIdx];
		const auto& currExternalStates = passExternalStates[passIdx];
		for (const auto& [resourceId, currState] : currStates)
		{
			if (!currState.touched)
				continue;

			ResourceState oldState = ResourceState::Undefined;
			auto it = lastKnownState.find(resourceId);
			if (it != lastKnownState.end())
				oldState = it->second;

			if (oldState != currState.beginState)
			{
				RGImageBarrier b{};
				b.handle.id = resourceId;
				b.oldState = oldState;
				b.newState = currState.beginState;
				barriersBeforePass[passIdx].push_back(b);
			}

			lastKnownState[resourceId] = currState.endState;
		}

		for (const auto& [name, currState] : currExternalStates)
		{
			if (!currState.touched)
				continue;

			ResourceState oldState = ResourceState::Undefined;
			auto it = lastKnownExternalState.find(name);
			if (it != lastKnownExternalState.end())
				oldState = it->second;

			if (oldState != currState.beginState)
			{
				RGExternalImageBarrier b{};
				b.name = name;
				b.oldState = oldState;
				b.newState = currState.beginState;
				externalBarriersBeforePass[passIdx].push_back(std::move(b));
			}

			lastKnownExternalState[name] = currState.endState;
		}
	}

	for (size_t idx : order)
	{
		const auto& barriers = barriersBeforePass[idx];
		const auto& externalBarriers = externalBarriersBeforePass[idx];

		if (context.enableBarrierDebugLog)
		{
			if (!barriers.empty() || !externalBarriers.empty())
				LOGD("RG Barrier before pass '%s': %zu internal, %zu external",
				    m_passes[idx].name.c_str(), barriers.size(), externalBarriers.size());
			for (const RGImageBarrier& b : barriers)
				LOGD("  internal tex:%u %s -> %s", b.handle.id, stateToString(b.oldState), stateToString(b.newState));
			for (const RGExternalImageBarrier& b : externalBarriers)
				LOGD("  external \"%s\" %s -> %s", b.name.c_str(), stateToString(b.oldState), stateToString(b.newState));
		}

		if (!barriers.empty() && context.insertImageBarriers)
			context.insertImageBarriers(context.commandBuffer, barriers);
		if (!externalBarriers.empty() && context.insertExternalImageBarriers)
			context.insertExternalImageBarriers(context.commandBuffer, externalBarriers);

		if (m_passes[idx].execute)
			m_passes[idx].execute(context);
	}
}
