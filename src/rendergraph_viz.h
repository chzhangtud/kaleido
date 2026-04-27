#pragma once

#include <cstdint>
#include <string>
#include <vector>

class RenderGraph;

struct RenderGraphVizPassNode
{
	std::string id;
	std::string name;
	uint32_t index = 0;
	uint32_t topoOrder = 0;
};

struct RenderGraphVizResourceNode
{
	std::string id;
	std::string kind;
	std::string name;
};

struct RenderGraphVizEdge
{
	std::string from;
	std::string to;
	std::string type;
	std::string state;
};

struct RenderGraphVizSnapshot
{
	uint64_t frameIndex = 0;
	std::vector<RenderGraphVizPassNode> passes;
	std::vector<RenderGraphVizResourceNode> resources;
	std::vector<RenderGraphVizEdge> edges;
};

RenderGraphVizSnapshot BuildRenderGraphVizSnapshot(const RenderGraph& rg, uint64_t frameIndex);
bool SerializeRenderGraphVizToJson(const RenderGraphVizSnapshot& snapshot, std::string& outJson, std::string* outError);
bool SerializeRenderGraphVizToDot(const RenderGraphVizSnapshot& snapshot, std::string& outDot, std::string* outError);
bool DeserializeRenderGraphVizFromJson(const std::string& json, RenderGraphVizSnapshot& outSnapshot, std::string& outError);
