#pragma once

#include <cstdint>
#include <vector>

struct Scene;

void ClearSceneTransformData(Scene& scene);

// Returns node indices sorted by (parent depth ascending, node index) for correct world evaluation.
std::vector<uint32_t> ComputeTransformEvalOrder(const Scene& scene);

void FlushSceneTransforms(Scene& scene);

void MarkTransformSubtreeDirty(Scene& scene, uint32_t nodeIndex);
