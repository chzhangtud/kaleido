#pragma once

#include "../src/shader_graph_types.h"

inline SGNode MakeNode(int id, SGNodeOp op)
{
	SGNode n{};
	n.id = id;
	n.op = op;
	return n;
}

inline SGNode MakeNodeWithValues(int id, SGNodeOp op, std::initializer_list<float> values, const char* text = "")
{
	SGNode n = MakeNode(id, op);
	n.values.assign(values.begin(), values.end());
	n.text = text ? text : "";
	return n;
}

inline ShaderGraphAsset BuildTimeNoiseExampleGraph()
{
	ShaderGraphAsset g{};
	g.nodes = {
		MakeNode(1, SGNodeOp::InputUV),
		MakeNodeWithValues(2, SGNodeOp::InputTime, { 4.0f }),
		MakeNode(3, SGNodeOp::ComposeVec3),
		MakeNode(4, SGNodeOp::NoisePerlin3D),
		MakeNodeWithValues(5, SGNodeOp::Remap, { -1.0f, 1.0f, 0.0f, 1.0f }),
		MakeNode(6, SGNodeOp::ComposeVec3),
		MakeNode(7, SGNodeOp::OutputSurface),
	};
	g.edges = {
		{ 1, 0, 3, 0 }, // uv -> compose.x
		{ 1, 1, 3, 1 }, // uv -> compose.y
		{ 2, 0, 3, 2 }, // time -> compose.z
		{ 3, 0, 4, 0 }, // vec3 -> noise
		{ 4, 0, 5, 0 }, // noise -> remap value
		{ 5, 0, 6, 0 }, // remap -> color.r
		{ 5, 0, 6, 1 }, // remap -> color.g
		{ 5, 0, 6, 2 }, // remap -> color.b
		{ 6, 0, 7, 0 }, // color -> output
	};
	return g;
}

inline ShaderGraphAsset BuildTinyCycleGraphForTest()
{
	ShaderGraphAsset g{};
	g.nodes = {
		MakeNode(10, SGNodeOp::InputTime),
		MakeNode(20, SGNodeOp::Mul),
		MakeNode(30, SGNodeOp::OutputSurface),
	};
	g.edges = {
		{ 10, 0, 20, 0 },
		{ 20, 0, 20, 1 },
		{ 20, 0, 30, 0 },
	};
	return g;
}
