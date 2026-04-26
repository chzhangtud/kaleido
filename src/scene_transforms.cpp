#include "scene_transforms.h"

#include "scene.h"

#include <algorithm>
#include <cstdint>
#include <numeric>
#include <vector>

void ClearSceneTransformData(Scene& scene)
{
	scene.transformNodes.clear();
	scene.transformRootNodes.clear();
	scene.drawsForNode.clear();
	scene.transformsGpuDirty = false;
	scene.uiSelectedGltfNode.reset();
}

std::vector<uint32_t> ComputeTransformEvalOrder(const Scene& scene)
{
	const uint32_t n = uint32_t(scene.transformNodes.size());
	std::vector<uint32_t> order(n);
	std::iota(order.begin(), order.end(), 0u);

	auto depthOf = [&](uint32_t idx) -> uint32_t {
		uint32_t d = 0;
		int32_t p = scene.transformNodes[idx].parent;
		while (p >= 0)
		{
			++d;
			p = scene.transformNodes[uint32_t(p)].parent;
		}
		return d;
	};

	std::sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) {
		const uint32_t da = depthOf(a);
		const uint32_t db = depthOf(b);
		if (da != db)
			return da < db;
		return a < b;
	});
	return order;
}

void FlushSceneTransforms(Scene& scene)
{
	if (scene.transformNodes.empty())
		return;

	const std::vector<uint32_t> order = ComputeTransformEvalOrder(scene);
	for (const uint32_t i : order)
	{
		TransformNode& node = scene.transformNodes[i];
		const glm::mat4 parentWorld = (node.parent < 0) ? glm::mat4(1.f) : scene.transformNodes[uint32_t(node.parent)].world;
		const glm::mat4 newWorld = parentWorld * node.local;
		if (node.worldDirty || newWorld != node.world)
		{
			node.world = newWorld;
			if (i < scene.drawsForNode.size())
			{
				for (const uint32_t d : scene.drawsForNode[i])
				{
					if (d < scene.draws.size())
						scene.draws[d].world = newWorld;
				}
			}
		}
		node.worldDirty = false;
	}
}

void MarkTransformSubtreeDirty(Scene& scene, const uint32_t root)
{
	if (scene.transformNodes.empty() || root >= scene.transformNodes.size())
		return;

	std::vector<uint32_t> stack;
	stack.push_back(root);
	while (!stack.empty())
	{
		const uint32_t i = stack.back();
		stack.pop_back();
		scene.transformNodes[i].worldDirty = true;
		for (const uint32_t c : scene.transformNodes[i].children)
		{
			if (c < scene.transformNodes.size())
				stack.push_back(c);
		}
	}
	scene.transformsGpuDirty = true;
}
