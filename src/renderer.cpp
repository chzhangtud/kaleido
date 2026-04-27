#include "renderer.h"
#include "tools.h"
#include "RenderBackend.h"
#include "RenderGraph.h"
#include "editor_scene_io.h"
#include "scene_transforms.h"

#include <glm/gtc/matrix_transform.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>
#undef GLM_ENABLE_EXPERIMENTAL
#include "../external/stb/stb_image_write.h"
#include "../external/ktx_software/external/astc-encoder/Source/ThirdParty/tinyexr.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <string_view>
#include <vector>
#if defined(WIN32)
#include <windows.h>
#include <commdlg.h>
#endif

template <typename PushConstants, size_t PushDescriptors>
void dispatch(VkCommandBuffer commandBuffer, const Program& program, uint32_t threadCountX, uint32_t threadCountY, const PushConstants& pushConstants, const DescriptorInfo (&pushDescriptors)[PushDescriptors])
{
	assert(program.pushConstantSize == sizeof(pushConstants));
	assert(program.pushDescriptorCount == PushDescriptors);

	if (program.pushConstantStages)
		vkCmdPushConstants(commandBuffer, program.layout, program.pushConstantStages, 0, sizeof(pushConstants), &pushConstants);

#if defined(WIN32)
	if (program.pushDescriptorCount)
		vkCmdPushDescriptorSetWithTemplate(commandBuffer, program.updateTemplate, program.layout, 0, pushDescriptors);
#endif
	vkCmdDispatch(commandBuffer, getGroupCount(threadCountX, program.localSizeX), getGroupCount(threadCountY, program.localSizeY), 1);
}

namespace
{
bool PathEndsWithExtensionIgnoreCase(std::string_view path, std::string_view extWithDot)
{
	if (path.size() < extWithDot.size())
		return false;
	for (size_t i = 0; i < extWithDot.size(); ++i)
	{
		const char a = char(std::tolower(static_cast<unsigned char>(path[path.size() - extWithDot.size() + i])));
		const char b = extWithDot[i];
		if (a != b)
			return false;
	}
	return true;
}

enum GpuTimestampSlot : uint32_t
{
	TS_FrameBegin = 0,
	TS_FrameEnd = 1,
	TS_CullBegin = 2,
	TS_CullEnd = 3,
	TS_RenderBegin = 4,
	TS_RenderEnd = 5,
	TS_PyramidBegin = 6,
	TS_PyramidEnd = 7,
	TS_CullLateBegin = 8,
	TS_CullLateEnd = 9,
	TS_RenderLateBegin = 10,
	TS_RenderLateEnd = 11,
	TS_CullPostBegin = 12,
	TS_CullPostEnd = 13,
	TS_RenderPostBegin = 14,
	TS_RenderPostEnd = 15,
	TS_ShadowBegin = 16,
	TS_ShadowEnd = 17,
	TS_ShadowBlurEnd = 18,
	TS_ShadeBegin = 19,
	TS_ShadeEnd = 20,
	TS_TlasBegin = 21,
	TS_TlasEnd = 22,
	TS_TaaBegin = 23,
	TS_TaaEnd = 24
};

double getTimestampDurationMs(const uint64_t* timestamps, uint32_t beginSlot, uint32_t endSlot, double timestampPeriodNs)
{
	return double(timestamps[endSlot] - timestamps[beginSlot]) * timestampPeriodNs * 1e-6;
}

#if defined(WIN32)
bool ShowOpenSceneDialog(std::string& outPath)
{
	char filePath[MAX_PATH] = "";
	OPENFILENAMEA ofn{};
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = nullptr;
	ofn.lpstrFile = filePath;
	ofn.nMaxFile = MAX_PATH;
	ofn.lpstrFilter = "glTF Scene (*.gltf;*.glb)\0*.gltf;*.glb\0All Files (*.*)\0*.*\0";
	ofn.nFilterIndex = 1;
	ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;

	if (GetOpenFileNameA(&ofn) == TRUE)
	{
		outPath = filePath;
		return true;
	}

	return false;
}

bool ShowOpenSceneStateDialog(std::string& outPath)
{
	char filePath[MAX_PATH] = "";
	OPENFILENAMEA ofn{};
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = nullptr;
	ofn.lpstrFile = filePath;
	ofn.nMaxFile = MAX_PATH;
	ofn.lpstrFilter = "kaleido Scene (*.json)\0*.json\0All Files (*.*)\0*.*\0";
	ofn.nFilterIndex = 1;
	ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;

	if (GetOpenFileNameA(&ofn) == TRUE)
	{
		outPath = filePath;
		return true;
	}

	return false;
}

bool ShowSaveSceneStateDialog(std::string& outPath)
{
	char filePath[MAX_PATH] = "";
	OPENFILENAMEA ofn{};
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = nullptr;
	ofn.lpstrFile = filePath;
	ofn.nMaxFile = MAX_PATH;
	ofn.lpstrFilter = "kaleido Scene (*.json)\0*.json\0All Files (*.*)\0*.*\0";
	ofn.nFilterIndex = 1;
	ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY;
	ofn.lpstrDefExt = "json";

	if (GetSaveFileNameA(&ofn) == TRUE)
	{
		outPath = filePath;
		return true;
	}

	return false;
}

bool ShowSaveViewportDumpDialog(std::string& outPath, bool saveAsExr)
{
	char filePath[MAX_PATH] = "";
	OPENFILENAMEA ofn{};
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = nullptr;
	ofn.lpstrFile = filePath;
	ofn.nMaxFile = MAX_PATH;
	ofn.lpstrFilter = saveAsExr
	                      ? "OpenEXR (*.exr)\0*.exr\0All Files (*.*)\0*.*\0"
	                      : "PNG Image (*.png)\0*.png\0All Files (*.*)\0*.*\0";
	ofn.nFilterIndex = 1;
	ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY;
	ofn.lpstrDefExt = saveAsExr ? "exr" : "png";

	if (GetSaveFileNameA(&ofn) == TRUE)
	{
		outPath = filePath;
		return true;
	}

	return false;
}
#endif

bool IsSceneAssetPath(const std::string& path)
{
	const char* ext = strrchr(path.c_str(), '.');
	return ext && (strcmp(ext, ".gltf") == 0 || strcmp(ext, ".glb") == 0);
}

bool SaveViewportDumpPng(const std::string& path, uint32_t width, uint32_t height, const uint8_t* rgba)
{
	if (!rgba || width == 0 || height == 0)
		return false;
	const int ok = stbi_write_png(path.c_str(), int(width), int(height), 4, rgba, int(width * 4));
	return ok != 0;
}

bool SaveViewportDumpExr(const std::string& path, uint32_t width, uint32_t height, const uint8_t* rgba)
{
	if (!rgba || width == 0 || height == 0)
		return false;

	const size_t pixelCount = size_t(width) * size_t(height);
	std::vector<float> channelR(pixelCount);
	std::vector<float> channelG(pixelCount);
	std::vector<float> channelB(pixelCount);
	std::vector<float> channelA(pixelCount);
	for (size_t i = 0; i < pixelCount; ++i)
	{
		channelR[i] = float(rgba[i * 4 + 0]) / 255.0f;
		channelG[i] = float(rgba[i * 4 + 1]) / 255.0f;
		channelB[i] = float(rgba[i * 4 + 2]) / 255.0f;
		channelA[i] = float(rgba[i * 4 + 3]) / 255.0f;
	}

	EXRImage image;
	InitEXRImage(&image);
	image.num_channels = 4;
	std::vector<float*> exrChannelPtrs = { channelB.data(), channelG.data(), channelR.data(), channelA.data() };
	image.images = reinterpret_cast<unsigned char**>(exrChannelPtrs.data());
	image.width = int(width);
	image.height = int(height);

	EXRHeader header;
	InitEXRHeader(&header);
	header.num_channels = 4;
	header.channels = static_cast<EXRChannelInfo*>(malloc(sizeof(EXRChannelInfo) * header.num_channels));
	header.pixel_types = static_cast<int*>(malloc(sizeof(int) * header.num_channels));
	header.requested_pixel_types = static_cast<int*>(malloc(sizeof(int) * header.num_channels));
	if (!header.channels || !header.pixel_types || !header.requested_pixel_types)
	{
		free(header.channels);
		free(header.pixel_types);
		free(header.requested_pixel_types);
		LOGE("Save EXR failed: out of memory while preparing header.");
		return false;
	}

	strncpy_s(header.channels[0].name, sizeof(header.channels[0].name), "B", _TRUNCATE);
	strncpy_s(header.channels[1].name, sizeof(header.channels[1].name), "G", _TRUNCATE);
	strncpy_s(header.channels[2].name, sizeof(header.channels[2].name), "R", _TRUNCATE);
	strncpy_s(header.channels[3].name, sizeof(header.channels[3].name), "A", _TRUNCATE);
	for (int c = 0; c < header.num_channels; ++c)
	{
		header.pixel_types[c] = TINYEXR_PIXELTYPE_FLOAT;
		header.requested_pixel_types[c] = TINYEXR_PIXELTYPE_HALF;
	}
	header.compression_type = TINYEXR_COMPRESSIONTYPE_NONE;

	const char* err = nullptr;
	const int exrCode = SaveEXRImageToFile(&image, &header, path.c_str(), &err);
	free(header.channels);
	free(header.pixel_types);
	free(header.requested_pixel_types);
	if (exrCode != TINYEXR_SUCCESS)
	{
		if (err)
		{
			LOGE("SaveEXRImageToFile failed for %s: %s", path.c_str(), err);
			FreeEXRErrorMessage(err);
		}
		return false;
	}

	return true;
}

static std::string BuildGltfNodeLabel(const GltfDocumentOutline& doc, const GltfNodeOutline& node)
{
	std::string label = node.name;
	if (node.meshIndex >= 0)
	{
		label += " [mesh ";
		label += std::to_string(node.meshIndex);
		if (size_t(node.meshIndex) < doc.meshNames.size())
		{
			label += ": ";
			label += doc.meshNames[size_t(node.meshIndex)];
		}
		label += "]";
	}
	return label;
}

static uint32_t FillEditorOutlineMeshDrawCommands(const Scene& scene, const std::vector<uint32_t>& drawIndices, MeshDrawCommand* out, const uint32_t maxOut)
{
	uint32_t n = 0;
	for (const uint32_t di : drawIndices)
	{
		if (n >= maxOut || di >= scene.draws.size())
			continue;
		const MeshDraw& md = scene.draws[di];
		if (md.meshIndex >= scene.geometry.meshes.size())
			continue;
		const Mesh& mesh = scene.geometry.meshes[md.meshIndex];
		const MeshLod& lod0 = mesh.lods[0];
		if (lod0.indexCount == 0u)
			continue;
		MeshDrawCommand cmd{};
		cmd.drawId = di;
		cmd.indirect.indexCount = lod0.indexCount;
		cmd.indirect.instanceCount = 1u;
		cmd.indirect.firstIndex = lod0.indexOffset;
		cmd.indirect.vertexOffset = int32_t(mesh.vertexOffset);
		cmd.indirect.firstInstance = 0u;
		out[n++] = cmd;
	}
	return n;
}

static void DrawGltfOutlineNode(Scene& scene, const GltfDocumentOutline& doc, uint32_t nodeIdx)
{
	if (size_t(nodeIdx) >= doc.nodes.size())
		return;
	const GltfNodeOutline& node = doc.nodes[nodeIdx];
	const std::string label = BuildGltfNodeLabel(doc, node);

	ImGui::PushID(int(nodeIdx));
	const bool selected = scene.uiSelectedGltfNode.has_value() && *scene.uiSelectedGltfNode == nodeIdx;
	const ImGuiTreeNodeFlags baseFlags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
	if (node.children.empty())
	{
		ImGui::TreeNodeEx("gleaf", baseFlags | ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | (selected ? ImGuiTreeNodeFlags_Selected : 0), "%s", label.c_str());
		if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
		{
			scene.uiSelectedGltfNode = nodeIdx;
			scene.uiSelectedMaterialIndex.reset();
		}
	}
	else if (ImGui::TreeNodeEx("gnode", baseFlags | (selected ? ImGuiTreeNodeFlags_Selected : 0), "%s", label.c_str()))
	{
		if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
		{
			scene.uiSelectedGltfNode = nodeIdx;
			scene.uiSelectedMaterialIndex.reset();
		}
		for (uint32_t c : node.children)
			DrawGltfOutlineNode(scene, doc, c);
		ImGui::TreePop();
	}
	ImGui::PopID();
}

static bool IsNearlyEqual(float a, float b, float eps = 1e-5f)
{
	return fabsf(a - b) <= eps;
}

static bool MaterialFactorsDiffer(const PBRMaterial& a, const PBRMaterial& b)
{
	const vec4& c0 = a.data.baseColorFactor;
	const vec4& c1 = b.data.baseColorFactor;
	const vec4& p0 = a.data.pbrFactor;
	const vec4& p1 = b.data.pbrFactor;
	if (!IsNearlyEqual(c0.x, c1.x) || !IsNearlyEqual(c0.y, c1.y) || !IsNearlyEqual(c0.z, c1.z) || !IsNearlyEqual(c0.w, c1.w))
		return true;
	if (!IsNearlyEqual(p0.x, p1.x) || !IsNearlyEqual(p0.y, p1.y) || !IsNearlyEqual(p0.z, p1.z) || !IsNearlyEqual(p0.w, p1.w))
		return true;
	return !IsNearlyEqual(a.data.emissiveFactor[0], b.data.emissiveFactor[0]) ||
	       !IsNearlyEqual(a.data.emissiveFactor[1], b.data.emissiveFactor[1]) ||
	       !IsNearlyEqual(a.data.emissiveFactor[2], b.data.emissiveFactor[2]);
}

static void DrawMaterialDock(Scene& scene)
{
	if (!ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen))
		return;
	if (!scene.gltfDocument.loaded)
	{
		ImGui::TextDisabled("Load a .gltf/.glb and select a mesh node.");
		return;
	}
	if (!scene.uiSelectedGltfNode.has_value())
	{
		ImGui::TextDisabled("Select a node in Scene tree.");
		return;
	}

	const uint32_t nodeIdx = *scene.uiSelectedGltfNode;
	if (nodeIdx >= scene.drawsForNode.size() || scene.drawsForNode[nodeIdx].empty())
	{
		ImGui::TextDisabled("Selected node has no mesh draws.");
		return;
	}

	std::vector<uint32_t> materialIndices;
	materialIndices.reserve(scene.drawsForNode[nodeIdx].size());
	for (uint32_t drawIndex : scene.drawsForNode[nodeIdx])
	{
		if (drawIndex >= scene.draws.size())
			continue;
		const uint32_t materialIndex = scene.draws[drawIndex].materialIndex;
		if (std::find(materialIndices.begin(), materialIndices.end(), materialIndex) == materialIndices.end())
			materialIndices.push_back(materialIndex);
	}
	if (materialIndices.empty())
	{
		ImGui::TextDisabled("Selected node has no editable materials.");
		return;
	}

	uint32_t materialIndex = materialIndices.front();
	if (scene.uiSelectedMaterialIndex.has_value())
	{
		const auto it = std::find(materialIndices.begin(), materialIndices.end(), *scene.uiSelectedMaterialIndex);
		if (it != materialIndices.end())
			materialIndex = *it;
	}
	scene.uiSelectedMaterialIndex = materialIndex;

	if (materialIndices.size() > 1)
	{
		int comboIndex = 0;
		for (size_t i = 0; i < materialIndices.size(); ++i)
		{
			if (materialIndices[i] == materialIndex)
			{
				comboIndex = int(i);
				break;
			}
		}
		std::vector<std::string> labels;
		std::vector<const char*> cstrs;
		labels.reserve(materialIndices.size());
		cstrs.reserve(materialIndices.size());
		for (uint32_t idx : materialIndices)
		{
			labels.push_back("material #" + std::to_string(idx));
			cstrs.push_back(labels.back().c_str());
		}
		if (ImGui::Combo("Material", &comboIndex, cstrs.data(), int(cstrs.size())))
		{
			materialIndex = materialIndices[size_t(comboIndex)];
			scene.uiSelectedMaterialIndex = materialIndex;
		}
	}

	if (materialIndex >= scene.materialDb.entries.size() || materialIndex >= scene.materialDb.gpuMaterials.size())
	{
		ImGui::TextDisabled("Material index %u is out of range.", materialIndex);
		return;
	}
	PBRMaterial* pbr = dynamic_cast<PBRMaterial*>(scene.materialDb.entries[materialIndex].get());
	if (!pbr)
	{
		ImGui::TextDisabled("Material #%u is not a PBR material.", materialIndex);
		return;
	}

	bool changed = false;
	changed |= ImGui::ColorEdit4("Base Color", &pbr->data.baseColorFactor.x);

	if (pbr->data.workflow == 1)
	{
		changed |= ImGui::DragFloat("Metallic", &pbr->data.pbrFactor.z, 0.01f, 0.f, 1.f, "%.3f");
		changed |= ImGui::DragFloat("Roughness", &pbr->data.pbrFactor.w, 0.01f, 0.f, 1.f, "%.3f");
	}
	else
	{
		changed |= ImGui::DragFloat4("Spec/Gloss (pbrFactor)", &pbr->data.pbrFactor.x, 0.01f, 0.f, 1.f, "%.3f");
	}

	float emissive[3] = { pbr->data.emissiveFactor[0], pbr->data.emissiveFactor[1], pbr->data.emissiveFactor[2] };
	if (ImGui::ColorEdit3("Emissive", emissive))
	{
		pbr->data.emissiveFactor[0] = emissive[0];
		pbr->data.emissiveFactor[1] = emissive[1];
		pbr->data.emissiveFactor[2] = emissive[2];
		changed = true;
	}

	if (changed)
	{
		scene.materialDb.gpuMaterials[materialIndex] = pbr->ToGpuMaterial();
		scene.materialDb.gpuDirty = true;
	}
}

static void DrawGltfDocumentTree(Scene& scene)
{
	const GltfDocumentOutline& doc = scene.gltfDocument;
	if (!doc.loaded)
	{
		ImGui::TextDisabled("(Load a .gltf / .glb to view the asset hierarchy.)");
		return;
	}

	if (ImGui::TreeNode("gltf_root", "glTF asset"))
	{
		if (!doc.scenes.empty())
		{
			if (ImGui::TreeNode("gltf_scenes", "Scenes (%d)", int(doc.scenes.size())))
			{
				for (size_t si = 0; si < doc.scenes.size(); ++si)
				{
					const GltfSceneOutline& sc = doc.scenes[si];
					ImGui::PushID(int(si));
					std::string scLabel = sc.name;
					if (int(si) == doc.defaultSceneIndex)
						scLabel += " (default)";
					if (ImGui::TreeNodeEx("gsc", ImGuiTreeNodeFlags_None, "%s", scLabel.c_str()))
					{
						for (uint32_t root : sc.rootNodes)
							DrawGltfOutlineNode(scene, doc, root);
						ImGui::TreePop();
					}
					ImGui::PopID();
				}
				ImGui::TreePop();
			}
		}
		else
			ImGui::TextDisabled("No scenes in file.");

		ImGui::TreePop();
	}

	DrawMaterialDock(scene);

	if (scene.uiSelectedGltfNode.has_value() && !scene.transformNodes.empty() &&
	    *scene.uiSelectedGltfNode < scene.transformNodes.size())
	{
		const uint32_t ni = *scene.uiSelectedGltfNode;
		TransformNode& tn = scene.transformNodes[ni];
		glm::vec3 scale;
		glm::quat orientation;
		glm::vec3 translation;
		glm::vec3 skew;
		glm::vec4 perspective;
		glm::decompose(tn.local, scale, orientation, translation, skew, perspective);
		orientation = glm::normalize(orientation);
		static glm::vec3 s_inspectorEulerDeg(0.f);
		static uint32_t s_inspectorNode = ~0u;
		if (ni != s_inspectorNode)
		{
			s_inspectorEulerDeg = glm::degrees(glm::eulerAngles(orientation));
			s_inspectorNode = ni;
		}

		ImGui::Separator();
		ImGui::TextUnformatted("Node transform (local)");
		const bool trChanged = ImGui::DragFloat3("Translation", &translation.x, 0.01f);
		const bool rotChanged = ImGui::DragFloat3("Rotation (deg)", &s_inspectorEulerDeg.x, 0.5f);
		const bool scChanged = ImGui::DragFloat3("Scale", &scale.x, 0.01f);
		if (trChanged || rotChanged || scChanged)
		{
			const glm::mat4 rx = glm::rotate(glm::mat4(1.f), glm::radians(s_inspectorEulerDeg.x), glm::vec3(1.f, 0.f, 0.f));
			const glm::mat4 ry = glm::rotate(glm::mat4(1.f), glm::radians(s_inspectorEulerDeg.y), glm::vec3(0.f, 1.f, 0.f));
			const glm::mat4 rz = glm::rotate(glm::mat4(1.f), glm::radians(s_inspectorEulerDeg.z), glm::vec3(0.f, 0.f, 1.f));
			orientation = glm::normalize(glm::quat_cast(rz * ry * rx));
			tn.local = glm::translate(glm::mat4(1.f), translation) * glm::mat4_cast(orientation) * glm::scale(glm::mat4(1.f), scale);
			MarkTransformSubtreeDirty(scene, ni);
			FlushSceneTransforms(scene);
		}
	}

	ImGui::Separator();
	ImGui::Checkbox("Selection outline (subtree)", &scene.uiEnableSelectionOutline);
	ImGui::Checkbox("Show selected subtree AABB (union)", &scene.uiShowSelectedSubtreeAabb);
}

// Halton low-discrepancy sequence in [0, 1) for subpixel jitter (bases 2 and 3).
static float halton(uint32_t index, uint32_t base)
{
	float result = 0.f;
	float f = 1.f / float(base);
	uint32_t i = index;
	while (i > 0)
	{
		result += f * float(i % base);
		i /= base;
		f /= float(base);
	}
	return result;
}

// Maps abstract RenderGraph ResourceState to Vulkan pipeline stage, access, and image layout.
void mapResourceStateToVulkanLayout(ResourceState state, bool isDepth, bool preferGeneralRead,
    VkPipelineStageFlags2& stage, VkAccessFlags2& access, VkImageLayout& layout)
{
	switch (state)
	{
	case ResourceState::ColorAttachment:
		stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		access = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		layout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
		break;
	case ResourceState::DepthStencil:
	case ResourceState::DepthStencilWrite:
		stage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
		access = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		layout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
		break;
	case ResourceState::DepthStencilRead:
		stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
		access = VK_ACCESS_SHADER_READ_BIT;
		layout = isDepth ? VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		break;
	case ResourceState::ShaderRead:
		stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
		access = VK_ACCESS_SHADER_READ_BIT;
		layout = preferGeneralRead ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		break;
	case ResourceState::ShaderWrite:
		stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
		access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
		layout = VK_IMAGE_LAYOUT_GENERAL;
		break;
	case ResourceState::CopySrc:
		stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
		access = VK_ACCESS_TRANSFER_READ_BIT;
		layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		break;
	case ResourceState::CopyDst:
		stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
		access = VK_ACCESS_TRANSFER_WRITE_BIT;
		layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		break;
	case ResourceState::Present:
		stage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
		access = 0;
		layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		break;
	default:
		stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
		access = 0;
		layout = VK_IMAGE_LAYOUT_UNDEFINED;
		break;
	}
}

bool readProcessEnvFlag(const char* name)
{
#if defined(_WIN32)
	char* value = nullptr;
	size_t len = 0;
	errno_t err = _dupenv_s(&value, &len, name);
	if (err != 0 || value == nullptr)
		return false;
	const bool enabled = atoi(value) != 0;
	free(value);
	return enabled;
#else
	const char* value = getenv(name);
	return value && atoi(value) != 0;
#endif
}
} // namespace

VkSemaphore createSemaphore(VkDevice device)
{
	VkSemaphoreCreateInfo createInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };

	VkSemaphore semaphore = 0;
	VK_CHECK(vkCreateSemaphore(device, &createInfo, 0, &semaphore));

	return semaphore;
}

VkCommandPool createCommandPool(VkDevice device, uint32_t familyIndex)
{
	VkCommandPoolCreateInfo createInfo = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
	createInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
	createInfo.queueFamilyIndex = familyIndex;

	VkCommandPool commandPool = 0;
	VK_CHECK(vkCreateCommandPool(device, &createInfo, 0, &commandPool));

	return commandPool;
}

VkFence createFence(VkDevice device)
{
	VkFenceCreateInfo createInfo = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };

	VkFence fence = 0;
	VK_CHECK(vkCreateFence(device, &createInfo, 0, &fence));

	return fence;
}

VkQueryPool createQueryPool(VkDevice device, uint32_t queryCount, VkQueryType queryType)
{
	VkQueryPoolCreateInfo createInfo = { VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO };
	createInfo.queryType = queryType;
	createInfo.queryCount = queryCount;

	if (queryType == VK_QUERY_TYPE_PIPELINE_STATISTICS)
	{
		createInfo.pipelineStatistics = VK_QUERY_PIPELINE_STATISTIC_CLIPPING_INVOCATIONS_BIT;
	}

	VkQueryPool queryPool = 0;
	VK_CHECK(vkCreateQueryPool(device, &createInfo, 0, &queryPool));

	return queryPool;
}

// deprecated
float halfToFloat(uint16_t v)
{
	// This function is AI generated.
	int s = (v >> 15) & 0x1;
	int e = (v >> 10) & 0x1f;
	int f = v & 0x3ff;

	assert(e != 31);

	if (e == 0)
	{
		if (f == 0)
			return s ? -0.f : 0.f;
		while ((f & 0x400) == 0)
		{
			f <<= 1;
			e -= 1;
		}
		e += 1;
		f &= ~0x400;
	}
	else if (e == 31)
	{
		if (f == 0)
			return s ? -INFINITY : INFINITY;
		return f ? NAN : s ? -INFINITY
		                   : INFINITY;
	}
	e = e + (127 - 15);
	f = f << 13;
	union
	{
		uint32_t u;
		float f;
	} result;
	result.u = (s << 31) | (e << 23) | f;
	return result.f;
}

// Platform-independent pointer helpers used for camera orientation.
static bool g_editorViewportInputMode = false;
static bool g_editorViewportRectValid = false;
static ImVec2 g_editorViewportRectMin = ImVec2(0.f, 0.f);
static ImVec2 g_editorViewportRectMax = ImVec2(0.f, 0.f);

static bool IsPointInsideEditorViewport(float x, float y)
{
	if (!g_editorViewportInputMode || !g_editorViewportRectValid)
		return false;

	return x >= g_editorViewportRectMin.x && x <= g_editorViewportRectMax.x &&
	       y >= g_editorViewportRectMin.y && y <= g_editorViewportRectMax.y;
}

void OnPointerDown(float x, float y)
{
	if (g_editorViewportInputMode && !IsPointInsideEditorViewport(x, y))
		return;

	mousePressed = true;
	firstMouse = true;
	lastX = x;
	lastY = y;
}

void OnPointerMove(float x, float y)
{
	if (!mousePressed)
		return;

	if (ImGui::GetIO().WantCaptureMouse && !IsPointInsideEditorViewport(x, y))
		return;

	static const float sensitivity = 0.1f;

	if (firstMouse)
	{
		lastX = x;
		lastY = y;
		firstMouse = false;
		return;
	}

	float xoffset = lastX - x;
	float yoffset = lastY - y;
	lastX = x;
	lastY = y;

	xoffset *= sensitivity;
	yoffset *= sensitivity;

	yaw += xoffset;
	pitch += yoffset;

	cameraDirty = true;
}

void OnPointerUp()
{
	mousePressed = false;
}

// Virtual sticks (Android): left stick controls movement, right stick controls look.
static float g_moveX = 0.0f;
static float g_moveY = 0.0f;
static float g_lookX = 0.0f;
static float g_lookY = 0.0f;

void SetVirtualSticks(float moveX, float moveY, float lookX, float lookY)
{
	g_moveX = moveX;
	g_moveY = moveY;
	g_lookX = lookX;
	g_lookY = lookY;
}

#if defined(WIN32)
void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
	if (action == GLFW_PRESS)
	{
		if (key == GLFW_KEY_M)
		{
			meshShadingEnabled = !meshShadingEnabled;
			return;
		}
		if (key == GLFW_KEY_C)
		{
			cullingEnabled = !cullingEnabled;
			return;
		}
		if (key == GLFW_KEY_O)
		{
			occlusionEnabled = !occlusionEnabled;
			return;
		}
		if (key == GLFW_KEY_K)
		{
			clusterOcclusionEnabled = !clusterOcclusionEnabled;
			return;
		}
		if (key == GLFW_KEY_L)
		{
			lodEnabled = !lodEnabled;
			return;
		}
		if (key == GLFW_KEY_T)
		{
			taskShadingEnabled = !taskShadingEnabled;
		}
		if (key == GLFW_KEY_S && action == GLFW_PRESS && (mods & GLFW_MOD_CONTROL))
		{
			shadowEnabled = !shadowEnabled;
			return;
		}
		if (key == GLFW_KEY_B)
		{
			shadowblurEnabled = !shadowblurEnabled;
		}
		if (key == GLFW_KEY_X)
		{
			shadowCheckerboard = !shadowCheckerboard;
		}
		if (key == GLFW_KEY_Q)
		{
			shadowQuality = 1 - shadowQuality;
		}
		if (key >= GLFW_KEY_0 && key <= GLFW_KEY_9)
		{
			debugLodStep = key - GLFW_KEY_0;
			return;
		}
		if (key == GLFW_KEY_R)
		{
			reloadShaders = !reloadShaders;
			reloadShadersTimer = 0;
		}
		if (key == GLFW_KEY_G)
		{
			debugGuiMode++;
		}
		if (key == GLFW_KEY_SPACE)
		{
			animationEnabled = !animationEnabled;
		}
		if (key == GLFW_KEY_Z)
		{
			debugSleep = !debugSleep;
		}
	}
}

void mouse_callback(GLFWwindow* window, double xpos, double ypos)
{
	OnPointerMove(static_cast<float>(xpos), static_cast<float>(ypos));
}

void mouse_button_callback(GLFWwindow* window, int button, int action, int mods)
{
	if (button == GLFW_MOUSE_BUTTON_LEFT)
	{
		if (action == GLFW_PRESS)
		{
			OnPointerDown(static_cast<float>(lastX), static_cast<float>(lastY));
		}
		else if (action == GLFW_RELEASE)
		{
			OnPointerUp();
		}
	}
}
#endif

void updateCamera()
{
	glm::mat3 matPitch = {
		glm::vec3(1.0f, 0.0f, 0.0f),
		glm::vec3(0.0f, cos(glm::radians(pitch)), -sin(glm::radians(pitch))),
		glm::vec3(0.0f, sin(glm::radians(pitch)), cos(glm::radians(pitch)))
	};
	matPitch = glm::transpose(matPitch);

	glm::mat3 matYaw = {
		glm::vec3(cos(glm::radians(yaw)), 0.0f, sin(glm::radians(yaw))),
		glm::vec3(0.0f, 1.0f, 0.0f),
		glm::vec3(-sin(glm::radians(yaw)), 0.0f, cos(glm::radians(yaw)))
	};
	matYaw = glm::transpose(matYaw);

	glm::mat3 matRoll = {
		glm::vec3(cos(glm::radians(roll)), -sin(glm::radians(roll)), 0.0f),
		glm::vec3(sin(glm::radians(roll)), cos(glm::radians(roll)), 0.0f),
		glm::vec3(0.0f, 0.0f, 1.0f)
	};
	matRoll = glm::transpose(matRoll);

	glm::vec3 front = matRoll * matYaw * matPitch * glm::vec3(0.0f, 0.0f, -1.0f);

	glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
	auto sce = VulkanContext::GetInstance()->GetScene();
	sce->camera.orientation = glm::quatLookAt(front, up);
}

EditorRenderSettings CaptureEditorRenderSettings()
{
	EditorRenderSettings settings{};
	settings.meshShadingEnabled = meshShadingEnabled;
	settings.taskShadingEnabled = taskShadingEnabled;
	settings.cullingEnabled = cullingEnabled;
	settings.occlusionEnabled = occlusionEnabled;
	settings.clusterOcclusionEnabled = clusterOcclusionEnabled;
	settings.shadowEnabled = shadowEnabled;
	settings.shadowQuality = shadowQuality;
	settings.shadowBlurEnabled = shadowblurEnabled;
	settings.shadowCheckerboard = shadowCheckerboard;
	settings.taaEnabled = taaEnabled;
	settings.taaBlendAlpha = taaBlendAlpha;
	settings.screenSpaceRefractionEnabled = screenSpaceRefractionEnabled;
	settings.lodEnabled = lodEnabled;
	settings.debugLodStep = debugLodStep;
	settings.animationEnabled = animationEnabled;
	settings.reloadShaders = reloadShaders;
	settings.debugGuiMode = debugGuiMode;
	settings.debugSleep = debugSleep;
	settings.gbufferDebugViewMode = VulkanContext::GetInstance()->GetGBufferDebugViewMode();
	settings.clusterRTEnabled = clusterRTEnabled;
	return settings;
}

void ApplyEditorRenderSettings(const EditorRenderSettings& settings)
{
	meshShadingEnabled = settings.meshShadingEnabled;
	taskShadingEnabled = settings.taskShadingEnabled;
	cullingEnabled = settings.cullingEnabled;
	occlusionEnabled = settings.occlusionEnabled;
	clusterOcclusionEnabled = settings.clusterOcclusionEnabled;
	shadowEnabled = settings.shadowEnabled;
	shadowQuality = std::clamp(settings.shadowQuality, 0, 1);
	shadowblurEnabled = settings.shadowBlurEnabled;
	shadowCheckerboard = settings.shadowCheckerboard;
	taaEnabled = settings.taaEnabled;
	taaBlendAlpha = std::clamp(settings.taaBlendAlpha, 0.01f, 1.0f);
	screenSpaceRefractionEnabled = settings.screenSpaceRefractionEnabled;
	lodEnabled = settings.lodEnabled;
	debugLodStep = std::max(0, settings.debugLodStep);
	animationEnabled = settings.animationEnabled;
	reloadShaders = settings.reloadShaders;
	debugGuiMode = std::clamp(settings.debugGuiMode, 0, 2);
	debugSleep = settings.debugSleep;
	VulkanContext::GetInstance()->SetGBufferDebugViewMode(settings.gbufferDebugViewMode);
	clusterRTEnabled = settings.clusterRTEnabled;
}

EditorCameraState CaptureEditorCameraState(const Scene& scene)
{
	EditorCameraState state{};
	state.position = scene.camera.position;
	state.eulerDegrees = vec3(pitch, yaw, roll);
	state.fovY = scene.camera.fovY;
	state.znear = scene.camera.znear;
	state.moveSpeed = cameraSpeed;
	state.dollyZoomEnabled = enableDollyZoom;
	state.dollyZoomRefDistance = soRef;
	return state;
}

void ApplyEditorCameraState(Scene& scene, const EditorCameraState& state)
{
	scene.camera.position = state.position;
	scene.camera.fovY = state.fovY;
	scene.camera.znear = state.znear;

	pitch = state.eulerDegrees.x;
	yaw = state.eulerDegrees.y;
	roll = state.eulerDegrees.z;
	cameraSpeed = std::max(0.0f, state.moveSpeed);
	enableDollyZoom = state.dollyZoomEnabled;
	soRef = std::max(0.01f, state.dollyZoomRefDistance);
	if (enableDollyZoom)
		cameraOriginForDolly = scene.camera.position;

	cameraDirty = true;
	updateCamera();
	cameraDirty = false;
}

EditorSceneUiState CaptureEditorSceneUiState(const Scene& scene)
{
	EditorSceneUiState ui{};
	ui.selectedGltfNode = scene.uiSelectedGltfNode;
	ui.selectionOutlineEnabled = scene.uiEnableSelectionOutline;
	ui.showSelectedSubtreeAabb = scene.uiShowSelectedSubtreeAabb;
	return ui;
}

void ApplyEditorSceneUiState(Scene& scene, const EditorSceneUiState& ui)
{
	scene.uiEnableSelectionOutline = ui.selectionOutlineEnabled;
	scene.uiShowSelectedSubtreeAabb = ui.showSelectedSubtreeAabb;
	if (!ui.selectedGltfNode.has_value())
	{
		scene.uiSelectedGltfNode.reset();
		scene.uiSelectedMaterialIndex.reset();
		return;
	}
	const uint32_t nodeIdx = *ui.selectedGltfNode;
	if (scene.gltfDocument.nodes.empty() || nodeIdx >= scene.gltfDocument.nodes.size())
	{
		LOGW("Editor scene state: selectedGltfNode %u is invalid for this scene (gltf node count %zu); clearing selection.",
		    nodeIdx, scene.gltfDocument.nodes.size());
		scene.uiSelectedGltfNode.reset();
		scene.uiSelectedMaterialIndex.reset();
		return;
	}
	scene.uiSelectedGltfNode = nodeIdx;
}

std::vector<mat4> CaptureEditorTransformNodeLocals(const Scene& scene)
{
	std::vector<mat4> out;
	out.reserve(scene.transformNodes.size());
	for (const TransformNode& tn : scene.transformNodes)
		out.push_back(tn.local);
	return out;
}

void ApplyEditorTransformNodeLocals(Scene& scene, const std::vector<mat4>& locals)
{
	if (locals.empty())
		return;
	if (locals.size() != scene.transformNodes.size())
	{
		LOGW("Editor scene state: transform count mismatch (snapshot %zu vs scene %zu); ignoring saved transforms.",
		    locals.size(), scene.transformNodes.size());
		return;
	}
	for (size_t i = 0; i < locals.size(); ++i)
		scene.transformNodes[i].local = locals[i];
	for (size_t i = 0; i < scene.transformNodes.size(); ++i)
		scene.transformNodes[i].worldDirty = true;
	FlushSceneTransforms(scene);
	scene.transformsGpuDirty = true;
}

std::vector<EditorMaterialOverride> CaptureEditorMaterialOverrides(const Scene& scene)
{
	std::vector<EditorMaterialOverride> out;
	const uint32_t count = std::min(scene.gltfMaterialCount, uint32_t(scene.gltfMaterialDefaults.size()));
	out.reserve(count);

	for (uint32_t gltfIndex = 0; gltfIndex < count; ++gltfIndex)
	{
		const std::optional<uint32_t> materialIndex = scene.GltfMaterialIndexToMaterialIndex(gltfIndex);
		if (!materialIndex.has_value())
			continue;
		if (*materialIndex >= scene.materialDb.entries.size())
			continue;
		const PBRMaterial* current = dynamic_cast<const PBRMaterial*>(scene.materialDb.entries[*materialIndex].get());
		if (!current)
			continue;
		const PBRMaterial& initial = scene.gltfMaterialDefaults[gltfIndex];
		if (!MaterialFactorsDiffer(*current, initial))
			continue;

		EditorMaterialOverride ov{};
		ov.gltfMaterialIndex = gltfIndex;
		ov.baseColorFactor = current->data.baseColorFactor;
		ov.pbrFactor = current->data.pbrFactor;
		ov.emissiveFactor = vec3(current->data.emissiveFactor[0], current->data.emissiveFactor[1], current->data.emissiveFactor[2]);
		out.push_back(ov);
	}

	return out;
}

void ApplyEditorMaterialOverrides(Scene& scene, const std::vector<EditorMaterialOverride>& overrides)
{
	bool applied = false;
	for (const EditorMaterialOverride& ov : overrides)
	{
		const std::optional<uint32_t> materialIndex = scene.GltfMaterialIndexToMaterialIndex(ov.gltfMaterialIndex);
		if (!materialIndex.has_value())
		{
			LOGW("Editor scene state: gltfMaterialIndex %u is invalid for this scene; override ignored.", ov.gltfMaterialIndex);
			continue;
		}
		if (*materialIndex >= scene.materialDb.entries.size() || *materialIndex >= scene.materialDb.gpuMaterials.size())
		{
			LOGW("Editor scene state: material index %u out of range; override ignored.", *materialIndex);
			continue;
		}
		PBRMaterial* pbr = dynamic_cast<PBRMaterial*>(scene.materialDb.entries[*materialIndex].get());
		if (!pbr)
		{
			LOGW("Editor scene state: material index %u is not PBR; override ignored.", *materialIndex);
			continue;
		}

		pbr->data.baseColorFactor = ov.baseColorFactor;
		pbr->data.pbrFactor = ov.pbrFactor;
		pbr->data.emissiveFactor[0] = ov.emissiveFactor.x;
		pbr->data.emissiveFactor[1] = ov.emissiveFactor.y;
		pbr->data.emissiveFactor[2] = ov.emissiveFactor.z;
		scene.materialDb.gpuMaterials[*materialIndex] = pbr->ToGpuMaterial();
		applied = true;
	}

	if (applied)
		scene.materialDb.gpuDirty = true;
}

mat4 perspectiveProjection(float fovY, float aspectWbyH, float zNear)
{
	float f = 1.0f / tanf(fovY / 2.0f);
	return mat4(
	    f / aspectWbyH, 0.0f, 0.0f, 0.0f,
	    0.0f, f, 0.0f, 0.0f,
	    0.0f, 0.0f, 0.0f, -1.0f,
	    0.0f, 0.0f, zNear, 0.0f);
}

mat4 perspectiveProjectionDollyZoom(float fovY, float aspectWbyH, float zNear, float so, float soRef)
{
	float f = 1.0f / tanf(fovY / 2.0f);

	double halfWidth = zNear * tanf(fovY / 2.0);
	double focalLengthRef = 1.0 / (1.0 / soRef + 1.0 / zNear);
	double transVerseMag = zNear / soRef;
	double focalLength = transVerseMag * so / (transVerseMag + 1.0);

	zNear = 1.f / (1.f / focalLength - 1.f / so);

	f = f * focalLength / focalLengthRef;

	return mat4(
	    f / aspectWbyH, 0.0f, 0.0f, 0.0f,
	    0.0f, f, 0.0f, 0.0f,
	    0.0f, 0.0f, 0.0f, -1.0f,
	    0.0f, 0.0f, zNear, 0.0f);
}

vec4 normalizePlane(vec4 p)
{
	return p / length(vec3(p));
}

uint32_t previousPow2(uint32_t v)
{
	uint32_t r = 1;
	while (r * 2 < v)
		r *= 2;

	return r;
}

uint32_t pcg32_random_r(pcg32_random_t* rng)
{
	uint64_t oldstate = rng->state;
	// Advance internal state
	rng->state = oldstate * 6364136223846793005ULL + (rng->inc | 1);
	// Calculate output function (XSH RR), uses old state for max ILP
	uint32_t xorshifted = uint32_t(((oldstate >> 18u) ^ oldstate) >> 27u);
	uint32_t rot = oldstate >> 59u;
	return (xorshifted >> rot) | (xorshifted << ((32 - rot) & 31));
}

double rand01()
{
	return pcg32_random_r(&rngstate) / double(1ull << 32);
}

uint32_t rand32()
{
	return pcg32_random_r(&rngstate);
}

const std::shared_ptr<VulkanContext>& VulkanContext::GetInstance()
{
	if (!gInstance)
	{
		gInstance = std::make_shared<VulkanContext>();
	}
	return gInstance;
}

#if defined(WIN32)
void VulkanContext::InitVulkan()
#elif defined(__ANDROID__)
void VulkanContext::InitVulkan(ANativeWindow* _window)
#endif
{
#if defined(WIN32)
	int rc = glfwInit();
	assert(rc);
#endif

	VK_CHECK(volkInitialize());

	instance = createInstance();
	if (!instance)
		return;

	volkLoadInstanceOnly(instance);

	debugCallback = registerDebugCallback(instance);

	VkPhysicalDevice physicalDevices[16];
	uint32_t physicalDeviceCount = sizeof(physicalDevices) / sizeof(physicalDevices[0]);
	auto result = vkEnumeratePhysicalDevices(instance, &physicalDeviceCount, physicalDevices);
	VK_CHECK(result);

	physicalDevice = pickPhysicalDevice(physicalDevices, physicalDeviceCount);
	if (!physicalDevice)
	{
		if (debugCallback)
			vkDestroyDebugReportCallbackEXT(instance, debugCallback, 0);
		vkDestroyInstance(instance, 0);
		return;
	}

	uint32_t extensionCount;
	VK_CHECK(vkEnumerateDeviceExtensionProperties(physicalDevice, 0, &extensionCount, 0));

	std::vector<VkExtensionProperties> extensions(extensionCount);
	VK_CHECK(vkEnumerateDeviceExtensionProperties(physicalDevice, 0, &extensionCount, extensions.data()));

	for (const auto& ext : extensions)
	{
		meshShadingSupported = meshShadingSupported || strcmp(ext.extensionName, VK_EXT_MESH_SHADER_EXTENSION_NAME) == 0;
		raytracingSupported = raytracingSupported || strcmp(ext.extensionName, VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME) == 0;
#if defined(VK_NV_cluster_acceleration_structure)
		clusterrtSupported = clusterrtSupported || strcmp(ext.extensionName, VK_NV_CLUSTER_ACCELERATION_STRUCTURE_EXTENSION_NAME) == 0;
#endif
	}
	// RenderDoc replay often fails when captures contain GPU-volatile addresses (BDA, BLAS/TLAS
	// deviceAddress). Disable RT/cluster-AS so captures stay replayable; shadows fall back to off.
	if (readProcessEnvFlag("KALEIDO_RENDERDOC_COMPAT"))
	{
		if (raytracingSupported || clusterrtSupported)
			LOGI("KALEIDO_RENDERDOC_COMPAT=1: ray tracing and cluster AS disabled for capture/replay stability");
		raytracingSupported = false;
		clusterrtSupported = false;
	}
#if defined(WIN32)
	pushDescriptorSupported = true;
#endif

	meshShadingEnabled = meshShadingSupported;

	vkGetPhysicalDeviceProperties(physicalDevice, &props);
	assert(props.limits.timestampComputeAndGraphics);

	graphicsFamily = getGraphicsFamilyIndex(physicalDevice);
	assert(graphicsFamily != VK_QUEUE_FAMILY_IGNORED);

	{
		VkPhysicalDeviceFeatures pdf{};
		vkGetPhysicalDeviceFeatures(physicalDevice, &pdf);
		wireframeDebugSupported = (pdf.fillModeNonSolid == VK_TRUE);
	}

	device = createDevice(instance, physicalDevice, graphicsFamily, pushDescriptorSupported, meshShadingEnabled, raytracingSupported, clusterrtSupported);
	assert(device);

	volkLoadDevice(device);

#if defined(WIN32)
	const char* windowTitle = editorViewportMode ? "kaleido editor" : "kaleido_standalone";
	int clientW = 1024;
	int clientH = 768;
	if (editorViewportMode && editorInitialViewportRequestWidth > 0u && editorInitialViewportRequestHeight > 0u)
	{
		// Leave room for the editor panel (360) + gap (20), ImGui viewport window chrome, and OS borders so
		// mainViewport->WorkSize can fit (viewport outer width) without clamping below the requested render size.
		const int panelAndGap = 380;
		const int horizontalChrome = 64;
		const int verticalChrome = 112;
		clientW = std::max(clientW, int(editorInitialViewportRequestWidth) + panelAndGap + horizontalChrome);
		clientH = std::max(clientH, int(editorInitialViewportRequestHeight) + verticalChrome);
	}
	window = glfwCreateWindow(clientW, clientH, windowTitle, 0, 0);
	assert(window);

	glfwSetKeyCallback(window, keyCallback);
#elif defined(__ANDROID__)
	window = _window;
#endif

	surface = createSurface(instance, window);
	assert(surface);

	// Check if VkSurfaceKHR is supported in physical device.
	VkBool32 presentSupported = VK_FALSE;
	VK_CHECK(vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, graphicsFamily, surface, &presentSupported));
	assert(presentSupported);

	swapchainFormat = getSwapchainFormat(physicalDevice, surface);
	depthFormat = VK_FORMAT_D32_SFLOAT;

	vkGetDeviceQueue(device, graphicsFamily, 0, &queue);

	if (!pushDescriptorSupported)
	{
		// TODO: find a proper way for resource allocation, maybe using bindless resources for android
		VkDescriptorPoolSize poolSizes[] = {
			{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 160 * MAX_FRAMES },
			{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 160 * MAX_FRAMES },
			{ VK_DESCRIPTOR_TYPE_SAMPLER, 160 * MAX_FRAMES },
			{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 160 * MAX_FRAMES },
			{ VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 10 * MAX_FRAMES }
		};

		VkDescriptorPoolCreateInfo poolInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
		poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT | VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
		poolInfo.maxSets = 150;
		poolInfo.poolSizeCount = COUNTOF(poolSizes);
		poolInfo.pPoolSizes = poolSizes;

		VK_CHECK(vkCreateDescriptorPool(device, &poolInfo, 0, &descriptorPool));
	}

	textureSampler = createSampler(device, VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT);
	assert(textureSampler);

	readSampler = createSampler(device, VK_FILTER_NEAREST, VK_SAMPLER_MIPMAP_MODE_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
	assert(readSampler);

	depthSampler = createSampler(device, VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_SAMPLER_REDUCTION_MODE_MIN);
	assert(depthSampler);

	gbufferFormats[0] = VK_FORMAT_R8G8B8A8_UNORM;
	gbufferFormats[1] = VK_FORMAT_A2B10G10R10_UNORM_PACK32;
	gbufferFormats[2] = VK_FORMAT_R32_UINT;

	gbufferInfo = { VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
	gbufferInfo.colorAttachmentCount = gbufferCount;
	gbufferInfo.pColorAttachmentFormats = gbufferFormats;
	gbufferInfo.depthAttachmentFormat = depthFormat;

	createSwapchain(swapchain, physicalDevice, device, surface, graphicsFamily, window, swapchainFormat);

	textureSetLayout = createDescriptorArrayLayout(device);

#if defined(WIN32)
	bool rcs = loadShaders(shaderSet, scene->path.c_str(), "shaders/");
#elif defined(__ANDROID__) // Remove this when upgrading to Vulkan 1.4
	bool rcs = loadShaders(shaderSet, device, scene->path.c_str(), "shaders/");
#endif
	assert(rcs);

	debugtextProgram = createProgram(device, VK_PIPELINE_BIND_POINT_COMPUTE, { &shaderSet["debugtext.comp"] }, sizeof(TextData), pushDescriptorSupported, descriptorPool);

	drawcullProgram = createProgram(device, VK_PIPELINE_BIND_POINT_COMPUTE, { &shaderSet["drawcull.comp"] }, sizeof(CullData), pushDescriptorSupported, descriptorPool);

	tasksubmitProgram = createProgram(device, VK_PIPELINE_BIND_POINT_COMPUTE, { &shaderSet["tasksubmit.comp"] }, 0, pushDescriptorSupported, descriptorPool);

	clustersubmitProgram = createProgram(device, VK_PIPELINE_BIND_POINT_COMPUTE, { &shaderSet["clustersubmit.comp"] }, 0, pushDescriptorSupported, descriptorPool);

	clustercullProgram = createProgram(device, VK_PIPELINE_BIND_POINT_COMPUTE, { &shaderSet["clustercull.comp"] }, sizeof(CullData), pushDescriptorSupported, descriptorPool);

	depthreduceProgram = createProgram(device, VK_PIPELINE_BIND_POINT_COMPUTE, { &shaderSet["depthreduce.comp"] }, sizeof(vec4), pushDescriptorSupported, descriptorPool);

	meshProgram = createProgram(device, VK_PIPELINE_BIND_POINT_GRAPHICS, { &shaderSet["mesh.vert"], &shaderSet["mesh.frag"] }, sizeof(Globals), pushDescriptorSupported, descriptorPool, textureSetLayout);

	meshSelectionOutlineProgram = createProgram(device, VK_PIPELINE_BIND_POINT_GRAPHICS, { &shaderSet["mesh.vert"], &shaderSet["mesh_selection_outline.frag"] }, sizeof(Globals), pushDescriptorSupported, descriptorPool, textureSetLayout);

#if defined(WIN32)
	editorAabbLineProgram = createProgram(device, VK_PIPELINE_BIND_POINT_GRAPHICS, { &shaderSet["editor_aabb_line.vert"], &shaderSet["editor_aabb_line.frag"] }, sizeof(EditorAabbLinePush), pushDescriptorSupported, descriptorPool);
#endif

	transparencyBlendMeshProgram = createProgram(device, VK_PIPELINE_BIND_POINT_GRAPHICS, { &shaderSet["mesh.vert"], &shaderSet["transparency_blend.frag"] }, sizeof(Globals), pushDescriptorSupported, descriptorPool, textureSetLayout);

	meshtaskProgram = {};
	clusterProgram = {};
	transparencyBlendMeshtaskProgram = {};
	transparencyBlendClusterProgram = {};
	if (meshShadingEnabled)
	{
		meshtaskProgram = createProgram(device, VK_PIPELINE_BIND_POINT_GRAPHICS, { &shaderSet["meshlet.task"], &shaderSet["meshlet.mesh"], &shaderSet["mesh.frag"] }, sizeof(Globals), pushDescriptorSupported, descriptorPool, textureSetLayout);

		clusterProgram = createProgram(device, VK_PIPELINE_BIND_POINT_GRAPHICS, { &shaderSet["meshlet.mesh"], &shaderSet["mesh.frag"] }, sizeof(Globals), pushDescriptorSupported, descriptorPool, textureSetLayout);
	}
	if (meshShadingSupported)
	{
		transparencyBlendMeshtaskProgram = createProgram(device, VK_PIPELINE_BIND_POINT_GRAPHICS, { &shaderSet["meshlet.task"], &shaderSet["meshlet.mesh"], &shaderSet["transparency_blend.frag"] }, sizeof(Globals), pushDescriptorSupported, descriptorPool, textureSetLayout);

		transparencyBlendClusterProgram = createProgram(device, VK_PIPELINE_BIND_POINT_GRAPHICS, { &shaderSet["meshlet.mesh"], &shaderSet["transparency_blend.frag"] }, sizeof(Globals), pushDescriptorSupported, descriptorPool, textureSetLayout);
	}

	finalProgram = createProgram(device, VK_PIPELINE_BIND_POINT_COMPUTE, { &shaderSet["final.comp"] }, sizeof(ShadeData), pushDescriptorSupported, descriptorPool);
	transmissionResolveProgram = createProgram(device, VK_PIPELINE_BIND_POINT_COMPUTE, { &shaderSet["transmission_resolve.comp"] }, sizeof(TransmissionResolveData), pushDescriptorSupported, descriptorPool, textureSetLayout);
	taaProgram = createProgram(device, VK_PIPELINE_BIND_POINT_COMPUTE, { &shaderSet["taa.comp"] }, sizeof(TaaData), pushDescriptorSupported, descriptorPool);
	shadowProgram = {};
	shadowblurProgram = {};
	if (raytracingSupported)
	{
		shadowProgram = createProgram(device, VK_PIPELINE_BIND_POINT_COMPUTE, { &shaderSet["shadow.comp"] }, sizeof(ShadowData), pushDescriptorSupported, descriptorPool, textureSetLayout);
		shadowfillProgram = createProgram(device, VK_PIPELINE_BIND_POINT_COMPUTE, { &shaderSet["shadowfill.comp"] }, sizeof(vec4), pushDescriptorSupported, descriptorPool);
		shadowblurProgram = createProgram(device, VK_PIPELINE_BIND_POINT_COMPUTE, { &shaderSet["shadowblur.comp"] }, sizeof(vec4), pushDescriptorSupported, descriptorPool);
	}

	pipelinesReloadedCallback = [&]()
	{
		auto replace = [&](VkPipeline& pipeline, VkPipeline newPipeline)
		{
			if (pipeline)
				vkDestroyPipeline(device, pipeline, 0);
			assert(newPipeline);
			pipeline = newPipeline;
			pipelines.emplace_back(newPipeline);
		};

		pipelines.clear();

		replace(debugtextPipeline, createComputePipeline(device, pipelineCache, debugtextProgram));
		replace(drawcullPipeline, createComputePipeline(device, pipelineCache, drawcullProgram, { { /* LATE= */ VK_FALSE }, { /* TASK= */ VK_FALSE } }));
		replace(drawculllatePipeline, createComputePipeline(device, pipelineCache, drawcullProgram, { { /* LATE= */ VK_TRUE }, { /* TASK= */ VK_FALSE } }));
		replace(taskcullPipeline, createComputePipeline(device, pipelineCache, drawcullProgram, { { /* LATE= */ VK_FALSE }, { /* TASK= */ VK_TRUE } }));
		replace(taskculllatePipeline, createComputePipeline(device, pipelineCache, drawcullProgram, { { /* LATE= */ VK_TRUE }, { /* TASK= */ VK_TRUE } }));

		replace(tasksubmitPipeline, createComputePipeline(device, pipelineCache, tasksubmitProgram));
		replace(clustersubmitPipeline, createComputePipeline(device, pipelineCache, clustersubmitProgram));
		replace(clustercullPipeline, createComputePipeline(device, pipelineCache, clustercullProgram, { { /* LATE= */ VK_FALSE } }));
		replace(clusterculllatePipeline, createComputePipeline(device, pipelineCache, clustercullProgram, { { /* LATE= */ VK_TRUE } }));
		replace(depthreducePipeline, createComputePipeline(device, pipelineCache, depthreduceProgram));
		replace(meshPipeline, createGraphicsPipeline(device, pipelineCache, gbufferInfo, meshProgram));
		replace(meshpostPipeline, createGraphicsPipeline(device, pipelineCache, gbufferInfo, meshProgram, { { /* LATE= */ VK_FALSE }, { /* TASK= */ VK_FALSE }, { /* POST= */ VK_TRUE } }));

		VkFormat sceneColorFormat = VK_FORMAT_R8G8B8A8_UNORM;
		VkPipelineRenderingCreateInfo sceneColorBlendRenderingInfo{ VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
		sceneColorBlendRenderingInfo.colorAttachmentCount = 1;
		sceneColorBlendRenderingInfo.pColorAttachmentFormats = &sceneColorFormat;
		sceneColorBlendRenderingInfo.depthAttachmentFormat = depthFormat;

		GraphicsPipelineExtraState transparencyBlendRaster;
		transparencyBlendRaster.alphaBlendFirstAttachment = true;
		transparencyBlendRaster.depthWrite = false;

		replace(meshTransparencyBlendPipeline, createGraphicsPipeline(device, pipelineCache, sceneColorBlendRenderingInfo, transparencyBlendMeshProgram,
		                                           { { /* LATE= */ VK_FALSE }, { /* TASK= */ VK_FALSE }, { /* POST= */ VK_TRUE } }, transparencyBlendRaster));

		{
			VkPipelineRenderingCreateInfo editorOutlineRenderingInfo{ VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
			editorOutlineRenderingInfo.colorAttachmentCount = 1;
			editorOutlineRenderingInfo.pColorAttachmentFormats = &sceneColorFormat;
			editorOutlineRenderingInfo.depthAttachmentFormat = VK_FORMAT_UNDEFINED;
			GraphicsPipelineExtraState outlineRaster{};
			outlineRaster.alphaBlendFirstAttachment = true;
			outlineRaster.depthWrite = false;
			outlineRaster.depthTestEnable = false;
			replace(meshSelectionOutlineEditorPipeline, createGraphicsPipeline(device, pipelineCache, editorOutlineRenderingInfo, meshSelectionOutlineProgram, {}, outlineRaster));
		}

#if defined(WIN32)
		{
			VkPipelineRenderingCreateInfo editorAabbRenderingInfo{ VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
			editorAabbRenderingInfo.colorAttachmentCount = 1;
			editorAabbRenderingInfo.pColorAttachmentFormats = &sceneColorFormat;
			editorAabbRenderingInfo.depthAttachmentFormat = depthFormat;
			GraphicsPipelineExtraState aabbLineRaster{};
			aabbLineRaster.primitiveTopology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
			aabbLineRaster.vertexInputWorldPositionVec3 = true;
			aabbLineRaster.disableBackfaceCull = true;
			aabbLineRaster.depthWrite = false;
			aabbLineRaster.depthTestEnable = true;
			replace(editorAabbLinePipeline, createGraphicsPipeline(device, pipelineCache, editorAabbRenderingInfo, editorAabbLineProgram, {}, aabbLineRaster));
		}
#endif

		if (wireframeDebugSupported)
		{
			{
				GraphicsPipelineExtraState wireExtra;
				wireExtra.polygonMode = VK_POLYGON_MODE_LINE;
				replace(meshWirePipeline, createGraphicsPipeline(device, pipelineCache, gbufferInfo, meshProgram,
				                              { { VK_FALSE }, { VK_FALSE }, { VK_FALSE } }, wireExtra));
				replace(meshpostWirePipeline, createGraphicsPipeline(device, pipelineCache, gbufferInfo, meshProgram,
				                                  { { VK_FALSE }, { VK_FALSE }, { VK_TRUE } }, wireExtra));
			}
		}

		if (meshShadingSupported)
		{
			replace(meshtaskPipeline, createGraphicsPipeline(device, pipelineCache, gbufferInfo, meshtaskProgram, { { /* LATE= */ VK_FALSE }, { /* TASK= */ VK_TRUE } }));
			replace(meshtasklatePipeline, createGraphicsPipeline(device, pipelineCache, gbufferInfo, meshtaskProgram, { { /* LATE= */ VK_TRUE }, { /* TASK= */ VK_TRUE }, { /* POST= */ VK_FALSE } }));
			replace(meshtaskpostPipeline, createGraphicsPipeline(device, pipelineCache, gbufferInfo, meshtaskProgram, { { /* LATE= */ VK_TRUE }, { /* TASK= */ VK_TRUE }, { /* POST= */ VK_TRUE } }));
			replace(clusterPipeline, createGraphicsPipeline(device, pipelineCache, gbufferInfo, clusterProgram));
			replace(clusterpostPipeline, createGraphicsPipeline(device, pipelineCache, gbufferInfo, clusterProgram, { { /* LATE= */ VK_FALSE }, { /* TASK= */ VK_FALSE }, { /* POST= */ VK_TRUE } }));

			replace(clusterTransparencyBlendPipeline, createGraphicsPipeline(device, pipelineCache, sceneColorBlendRenderingInfo, transparencyBlendClusterProgram,
			                                              { { /* LATE= */ VK_FALSE }, { /* TASK= */ VK_FALSE }, { /* POST= */ VK_TRUE } }, transparencyBlendRaster));
			replace(meshtaskTransparencyBlendPipeline, createGraphicsPipeline(device, pipelineCache, sceneColorBlendRenderingInfo, transparencyBlendMeshtaskProgram,
			                                               { { /* LATE= */ VK_TRUE }, { /* TASK= */ VK_TRUE }, { /* POST= */ VK_TRUE } }, transparencyBlendRaster));

			if (wireframeDebugSupported)
			{
				GraphicsPipelineExtraState meshWireExtra;
				meshWireExtra.polygonMode = VK_POLYGON_MODE_LINE;
				replace(meshtaskWirePipeline, createGraphicsPipeline(device, pipelineCache, gbufferInfo, meshtaskProgram,
				                                  { { VK_FALSE }, { VK_TRUE }, { VK_FALSE } }, meshWireExtra));
				replace(meshtasklateWirePipeline, createGraphicsPipeline(device, pipelineCache, gbufferInfo, meshtaskProgram,
				                                      { { VK_TRUE }, { VK_TRUE }, { VK_FALSE } }, meshWireExtra));
				replace(meshtaskpostWirePipeline, createGraphicsPipeline(device, pipelineCache, gbufferInfo, meshtaskProgram,
				                                      { { VK_TRUE }, { VK_TRUE }, { VK_TRUE } }, meshWireExtra));
				replace(clusterWirePipeline, createGraphicsPipeline(device, pipelineCache, gbufferInfo, clusterProgram,
				                                 { { VK_FALSE }, { VK_FALSE }, { VK_FALSE } }, meshWireExtra));
				replace(clusterpostWirePipeline, createGraphicsPipeline(device, pipelineCache, gbufferInfo, clusterProgram,
				                                     { { VK_FALSE }, { VK_FALSE }, { VK_TRUE } }, meshWireExtra));
			}
		}

		replace(finalPipeline, createComputePipeline(device, pipelineCache, finalProgram));
		replace(transmissionResolvePipeline, createComputePipeline(device, pipelineCache, transmissionResolveProgram));
		replace(taaPipeline, createComputePipeline(device, pipelineCache, taaProgram));
		if (raytracingSupported)
		{
			replace(shadowlqPipeline, createComputePipeline(device, pipelineCache, shadowProgram, { { /* QUALITY= */ int32_t(0) } }));
			replace(shadowhqPipeline, createComputePipeline(device, pipelineCache, shadowProgram, { { /* QUALITY= */ int32_t(1) } }));
			replace(shadowfillPipeline, createComputePipeline(device, pipelineCache, shadowfillProgram));
			replace(shadowblurPipeline, createComputePipeline(device, pipelineCache, shadowblurProgram));
		}
	};

	pipelinesReloadedCallback();

	for (size_t ii = 0; ii < MAX_FRAMES; ++ii)
	{
		queryPoolsTimestamp[ii] = createQueryPool(device, 128, VK_QUERY_TYPE_TIMESTAMP);
		assert(queryPoolsTimestamp[ii]);
	}
#if defined(WIN32)
	for (size_t ii = 0; ii < MAX_FRAMES; ++ii)
	{
		queryPoolsPipeline[ii] = createQueryPool(device, 4, VK_QUERY_TYPE_PIPELINE_STATISTICS);
		assert(queryPoolsPipeline[ii]);
	}
#endif

	for (size_t ii = 0; ii < MAX_FRAMES; ++ii)
	{
		commandPools[ii] = createCommandPool(device, graphicsFamily);
		assert(commandPools[ii]);
		VkCommandBufferAllocateInfo allocateInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
		allocateInfo.commandPool = commandPools[ii];
		allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocateInfo.commandBufferCount = 1;

		VK_CHECK(vkAllocateCommandBuffers(device, &allocateInfo, &commandBuffers[ii]));
	}

	vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);

	// Initialize central resource manager for buffers/textures used by the renderer.
	resourceManager.Initialize(device, memoryProperties);

	resourceManager.CreateBuffer(scratch, 128 * 1024 * 1024, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
}

void VulkanContext::SetScene(const std::shared_ptr<Scene>& _scene)
{
	scene = _scene;
}

const std::shared_ptr<Scene>& VulkanContext::GetScene() const noexcept
{
	return scene;
}

void VulkanContext::SetRuntimeUiEnabled(bool enabled)
{
	runtimeUiEnabled = enabled;
}

bool VulkanContext::IsRuntimeUiEnabled() const noexcept
{
	return runtimeUiEnabled;
}

void VulkanContext::SetEditorViewportMode(bool enabled)
{
	editorViewportMode = enabled;
}

#if defined(WIN32)
void VulkanContext::SetEditorInitialViewportRequest(uint32_t width, uint32_t height)
{
	editorInitialViewportRequestWidth = width;
	editorInitialViewportRequestHeight = height;
}

void VulkanContext::ApplyEditorViewportSizeFromSnapshot(uint32_t width, uint32_t height)
{
	if (!editorViewportMode || width == 0u || height == 0u || !window)
		return;
	editorInitialViewportRequestWidth = width;
	editorInitialViewportRequestHeight = height;
	const int panelAndGap = 380;
	const int horizontalChrome = 64;
	const int verticalChrome = 112;
	const int needW = int(width) + panelAndGap + horizontalChrome;
	const int needH = int(height) + verticalChrome;
	int curW = 0;
	int curH = 0;
	glfwGetWindowSize(window, &curW, &curH);
	glfwSetWindowSize(window, std::max(curW, needW), std::max(curH, needH));
}
#endif

bool VulkanContext::IsEditorViewportMode() const noexcept
{
	return editorViewportMode;
}

int VulkanContext::GetGBufferDebugViewMode() const noexcept
{
	return gbufferDebugViewMode;
}

void VulkanContext::SetGBufferDebugViewMode(int mode)
{
	gbufferDebugViewMode = std::clamp(mode, 0, 2);
}

void VulkanContext::SetAutoExitAfterExrDump(const std::string& exrPath, uint32_t frameDelay)
{
	if (exrPath.empty() || !editorViewportMode)
		return;
	autoExitAfterViewportDump = true;
	autoDumpExrPathPending = exrPath;
	autoDumpExrFireAtFrame = uint64_t(frameDelay);
	autoExrFired = false;
}

void VulkanContext::ProcessCompletedViewportDump(uint32_t frameSlot)
{
	if (frameSlot >= MAX_FRAMES)
		return;
	ViewportDumpReadback& pending = viewportDumpReadbacks[frameSlot];
	if (!pending.inUse)
		return;
	const std::string outputPath = pending.outputPath;
	const bool saveExr = pending.saveAsExr;

	const uint8_t* pixels = static_cast<const uint8_t*>(pending.staging.data);
	bool ok = false;
	if (saveExr)
		ok = SaveViewportDumpExr(outputPath, pending.width, pending.height, pixels);
	else
		ok = SaveViewportDumpPng(outputPath, pending.width, pending.height, pixels);

	if (ok)
	{
		editorViewportDumpStatus = std::string("Viewport dump saved: ") + outputPath;
		editorViewportDumpStatusIsError = false;
		LOGI("Viewport dump saved: %s", outputPath.c_str());
	}
	else
	{
		editorViewportDumpStatus = std::string("Failed to save viewport dump: ") + outputPath;
		editorViewportDumpStatusIsError = true;
		LOGE("Failed to save viewport dump: %s", outputPath.c_str());
	}

	if (autoExitAfterViewportDump && !autoDumpExrPathPending.empty() && outputPath == autoDumpExrPathPending)
	{
#if defined(WIN32)
		if (window)
			glfwSetWindowShouldClose(window, GLFW_TRUE);
#endif
	}

	resourceManager.DestroyBuffer(pending.staging);
	pending = {};
}

void VulkanContext::ReleaseViewportDumpReadbacks()
{
	for (uint32_t i = 0; i < MAX_FRAMES; ++i)
	{
		if (viewportDumpReadbacks[i].inUse)
		{
			resourceManager.DestroyBuffer(viewportDumpReadbacks[i].staging);
			viewportDumpReadbacks[i] = {};
		}
	}
	editorViewportDumpRequested = false;
	editorViewportDumpRequestPath.clear();
}

void VulkanContext::RequestEditorSceneLoad(const std::string& scenePath)
{
	if (scenePath.empty())
		return;

	editorSceneLoadRequestPath = scenePath;
	editorSceneLoadRequested = true;
}

bool VulkanContext::ConsumeEditorSceneLoadRequest(std::string& outScenePath)
{
	if (!editorSceneLoadRequested)
		return false;

	editorSceneLoadRequested = false;
	outScenePath = editorSceneLoadRequestPath;
	editorSceneLoadRequestPath.clear();
	return !outScenePath.empty();
}

void VulkanContext::ResetSceneResourcesForReload()
{
	VK_CHECK(vkDeviceWaitIdle(device));
	ReleaseViewportDumpReadbacks();

	if (raytracingSupported)
	{
		if (tlas != VK_NULL_HANDLE)
			vkDestroyAccelerationStructureKHR(device, tlas, 0);
		tlas = VK_NULL_HANDLE;
		for (VkAccelerationStructureKHR as : blas)
			if (as != VK_NULL_HANDLE)
				vkDestroyAccelerationStructureKHR(device, as, 0);
		blas.clear();
		blasAddresses.clear();
		resourceManager.DestroyBuffer(tlasBuffer);
		resourceManager.DestroyBuffer(blasBuffer);
		resourceManager.DestroyBuffer(tlasScratchBuffer);
		resourceManager.DestroyBuffer(tlasInstanceBuffer);
		tlasBuffer = {};
		blasBuffer = {};
		tlasScratchBuffer = {};
		tlasInstanceBuffer = {};
	}

	resourceManager.DestroyBuffer(outlineDccb);
	resourceManager.DestroyBuffer(outlineDcb);
	resourceManager.DestroyBuffer(dccb);
	resourceManager.DestroyBuffer(dcb);
	resourceManager.DestroyBuffer(dvb);
	resourceManager.DestroyBuffer(db);
	resourceManager.DestroyBuffer(mb);
	resourceManager.DestroyBuffer(mtb);
	resourceManager.DestroyBuffer(mlb);
	resourceManager.DestroyBuffer(mdb);
	resourceManager.DestroyBuffer(mvb);
	resourceManager.DestroyBuffer(cib);
	resourceManager.DestroyBuffer(ccb);
	resourceManager.DestroyBuffer(ib);
	resourceManager.DestroyBuffer(vb);
	outlineDccb = {};
	outlineDcb = {};
	dccb = {};
	dcb = {};
	dvb = {};
	db = {};
	mb = {};
	mtb = {};
	mlb = {};
	mdb = {};
	mvb = {};
	cib = {};
	ccb = {};
	ib = {};
	vb = {};

	if (scene)
	{
		for (Image& image : scene->images)
			resourceManager.DestroyImage(image);
		scene->images.clear();
		if (scene->textureSet.first != VK_NULL_HANDLE)
			vkDestroyDescriptorPool(device, scene->textureSet.first, 0);
		scene->textureSet = {};
	}

	tlasNeedsRebuild = true;
	dvbCleared = false;
	mvbCleared = false;
	meshletVisibilityBytes = 0;
}

void VulkanContext::ClearRenderGraphExternalImages()
{
	rgExternalImageRegistry.clear();
}

void VulkanContext::RegisterRenderGraphExternalImage(const std::string& name, VkImage image, TextureFormat format, TextureUsage usage)
{
	rgExternalImageRegistry.insert_or_assign(name, RGExternalImageRegistryEntry{ image, format, usage });
}

void VulkanContext::PrepareRenderGraphPassContext(RGPassContext& out, VkCommandBuffer commandBuffer, uint64_t frameIndex, uint32_t swapchainImageIndex)
{
	out = {};
	out.resourceManager = &resourceManager;
	out.commandBuffer = commandBuffer;
	out.frameIndex = frameIndex;
	out.enableBarrierDebugLog = readProcessEnvFlag("RG_BARRIER_DEBUG");

	ClearRenderGraphExternalImages();
	if (editorViewportMode && editorViewportTargetHandle.IsValid())
	{
		Image* editorTarget = resourceManager.GetTexture(editorViewportTargetHandle);
		if (editorTarget)
		{
			RegisterRenderGraphExternalImage("FinalColor", editorTarget->image, TextureFormat::RGBA8_UNorm,
			    TextureUsage::Storage | TextureUsage::Sampled | TextureUsage::ColorAttachment);
		}
	}
	else
	{
		RegisterRenderGraphExternalImage("FinalColor", swapchain.images[swapchainImageIndex], TextureFormat::RGBA8_UNorm,
		    TextureUsage::Storage | TextureUsage::Sampled | TextureUsage::ColorAttachment);
	}

	out.insertImageBarriers = [this](VkCommandBuffer cb, const std::vector<RGImageBarrier>& barriers)
	{
		if (barriers.empty())
			return;

		std::vector<VkImageMemoryBarrier2> vkBarriers;
		vkBarriers.reserve(barriers.size());

		for (const RGImageBarrier& b : barriers)
		{
			Image* image = resourceManager.GetTexture(b.handle);
			const RGTextureDesc* desc = resourceManager.GetTextureDesc(b.handle);
			if (!image || !desc)
				continue;

			const bool isDepth = desc->format == TextureFormat::D24S8 || desc->format == TextureFormat::D32_Float;
			const VkImageAspectFlags aspect = isDepth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
			const bool supportsStorage = (static_cast<uint32_t>(desc->usage) & static_cast<uint32_t>(TextureUsage::Storage)) != 0;

			VkPipelineStageFlags2 srcStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
			VkAccessFlags2 srcAccess = 0;
			VkImageLayout oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			mapResourceStateToVulkanLayout(b.oldState, isDepth, supportsStorage, srcStage, srcAccess, oldLayout);

			VkPipelineStageFlags2 dstStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
			VkAccessFlags2 dstAccess = 0;
			VkImageLayout newLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			mapResourceStateToVulkanLayout(b.newState, isDepth, supportsStorage, dstStage, dstAccess, newLayout);

			vkBarriers.push_back(imageBarrier(image->image, srcStage, srcAccess, oldLayout, dstStage, dstAccess, newLayout, aspect));
		}

		if (!vkBarriers.empty())
			pipelineBarrier(cb, VK_DEPENDENCY_BY_REGION_BIT, 0, nullptr, vkBarriers.size(), vkBarriers.data());
	};

	out.insertExternalImageBarriers = [this](VkCommandBuffer cb, const std::vector<RGExternalImageBarrier>& barriers)
	{
		if (barriers.empty())
			return;

		std::vector<VkImageMemoryBarrier2> vkBarriers;
		vkBarriers.reserve(barriers.size());

		for (const RGExternalImageBarrier& b : barriers)
		{
			auto regIt = rgExternalImageRegistry.find(b.name);
			if (regIt == rgExternalImageRegistry.end() || regIt->second.image == VK_NULL_HANDLE)
				continue;

			const RGExternalImageRegistryEntry& entry = regIt->second;

			const bool isDepth = entry.format == TextureFormat::D24S8 || entry.format == TextureFormat::D32_Float;
			const bool supportsStorage = (static_cast<uint32_t>(entry.usage) & static_cast<uint32_t>(TextureUsage::Storage)) != 0;
			const VkImageAspectFlags aspect = isDepth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;

			VkPipelineStageFlags2 srcStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
			VkAccessFlags2 srcAccess = 0;
			VkImageLayout oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			mapResourceStateToVulkanLayout(b.oldState, isDepth, supportsStorage, srcStage, srcAccess, oldLayout);

			VkPipelineStageFlags2 dstStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
			VkAccessFlags2 dstAccess = 0;
			VkImageLayout newLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			mapResourceStateToVulkanLayout(b.newState, isDepth, supportsStorage, dstStage, dstAccess, newLayout);

			vkBarriers.push_back(imageBarrier(entry.image,
			    srcStage, srcAccess, oldLayout,
			    dstStage, dstAccess, newLayout,
			    aspect));
		}

		if (!vkBarriers.empty())
			pipelineBarrier(cb, VK_DEPENDENCY_BY_REGION_BIT, 0, nullptr, vkBarriers.size(), vkBarriers.data());
	};
}

void VulkanContext::InitResources()
{
	meshletVisibilityBytes = (scene->meshletVisibilityCount + 31) / 32 * sizeof(uint32_t);

	uint32_t raytracingBufferFlags =
	    raytracingSupported
	        ? VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
	        : 0;

	const size_t meshCount = scene->geometry.meshes.size();
	std::vector<GpuMeshStd430> gpuMeshes(meshCount);
	for (size_t i = 0; i < meshCount; ++i)
		gpuMeshes[i] = PackGpuMeshStd430(scene->geometry.meshes[i]);

	resourceManager.CreateBuffer(mb, meshCount * sizeof(GpuMeshStd430), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	resourceManager.CreateBuffer(mtb, scene->materialDb.gpuMaterials.size() * sizeof(Material), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
	    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

	resourceManager.CreateBuffer(vb, scene->geometry.vertices.size() * sizeof(Vertex), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | raytracingBufferFlags, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	resourceManager.CreateBuffer(ib, scene->geometry.indices.size() * sizeof(uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | raytracingBufferFlags, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	if (meshShadingEnabled)
	{
		resourceManager.CreateBuffer(mlb, scene->geometry.meshlets.size() * sizeof(Meshlet), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		resourceManager.CreateBuffer(mdb, scene->geometry.meshletdata.size() * sizeof(uint32_t), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | raytracingBufferFlags, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	}

	VkCommandPool initCommandPool = commandPools[0];
	VkCommandBuffer initCommandBuffer = commandBuffers[0];

	uploadBuffer(device, initCommandPool, initCommandBuffer, queue, mb, scratch, gpuMeshes.data(), gpuMeshes.size() * sizeof(GpuMeshStd430));
	if (mtb.data)
	{
		memcpy(mtb.data, scene->materialDb.gpuMaterials.data(), scene->materialDb.gpuMaterials.size() * sizeof(Material));
	}
	else
	{
		uploadBuffer(device, initCommandPool, initCommandBuffer, queue, mtb, scratch, scene->materialDb.gpuMaterials.data(), scene->materialDb.gpuMaterials.size() * sizeof(Material));
	}
	uploadBuffer(device, initCommandPool, initCommandBuffer, queue, vb, scratch, scene->geometry.vertices.data(), scene->geometry.vertices.size() * sizeof(Vertex));
	uploadBuffer(device, initCommandPool, initCommandBuffer, queue, ib, scratch, scene->geometry.indices.data(), scene->geometry.indices.size() * sizeof(uint32_t));

	if (meshShadingEnabled)
	{
		uploadBuffer(device, initCommandPool, initCommandBuffer, queue, mlb, scratch, scene->geometry.meshlets.data(), scene->geometry.meshlets.size() * sizeof(Meshlet));
		uploadBuffer(device, initCommandPool, initCommandBuffer, queue, mdb, scratch, scene->geometry.meshletdata.data(), scene->geometry.meshletdata.size() * sizeof(uint32_t));
	}

	resourceManager.CreateBuffer(db, scene->draws.size() * sizeof(MeshDraw), VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

	resourceManager.CreateBuffer(dvb, scene->draws.size() * sizeof(uint32_t), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	resourceManager.CreateBuffer(dcb, TASK_WGLIMIT * sizeof(MeshTaskCommand), VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	resourceManager.CreateBuffer(dccb, 16, VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	const uint32_t outlineIndirectMax = std::max(1u, uint32_t(scene->draws.size()));
	resourceManager.CreateBuffer(outlineDcb, outlineIndirectMax * sizeof(MeshDrawCommand),
	    VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
	    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	resourceManager.CreateBuffer(outlineDccb, 16,
	    VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
	    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

#if defined(WIN32)
	if (!editorAabbLineVb.buffer)
	{
		resourceManager.CreateBuffer(editorAabbLineVb, 24u * 3u * sizeof(float), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
		    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	}
#endif

	// TODO: there's a way to implement cluster visibility persistence *without* using bitwise storage at all, which may be beneficial on the balance, so we should try that.
	// *if* we do that, we can drop meshletVisibilityOffset et al from everywhere
	if (meshShadingSupported)
	{
		createBuffer(mvb, device, memoryProperties, meshletVisibilityBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	}

	if (meshShadingSupported)
	{
		createBuffer(cib, device, memoryProperties, CLUSTER_LIMIT * sizeof(uint32_t), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		createBuffer(ccb, device, memoryProperties, 16, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	}

	uploadBuffer(device, initCommandPool, initCommandBuffer, queue, db, scratch, scene->draws.data(), scene->draws.size() * sizeof(MeshDraw));

	if (raytracingSupported)
	{
		if (clusterrtSupported && clusterRTEnabled)
		{
			Buffer vxb = {};
			createBuffer(vxb, device, memoryProperties, scene->geometry.meshletvtx0.size() * sizeof(uint16_t), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | raytracingBufferFlags, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
			memcpy(vxb.data, scene->geometry.meshletvtx0.data(), scene->geometry.meshletvtx0.size() * sizeof(uint16_t));

			buildCBLAS(device, scene->geometry.meshes, scene->geometry.meshlets, vxb, mdb, blas, blasBuffer, initCommandPool, initCommandBuffer, queue, memoryProperties);

			destroyBuffer(vxb, device);
		}
		else
		{
			std::vector<VkDeviceSize> compactedSizes;
			buildBLAS(device, scene->geometry.meshes, vb, ib, blas, compactedSizes, blasBuffer, initCommandPool, initCommandBuffer, queue, memoryProperties);
			compactBLAS(device, blas, compactedSizes, blasBuffer, initCommandPool, initCommandBuffer, queue, memoryProperties);
		}

		blasAddresses.resize(blas.size());

		for (size_t i = 0; i < blas.size(); ++i)
		{
			VkAccelerationStructureDeviceAddressInfoKHR info = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR };
			info.accelerationStructure = blas[i];

			blasAddresses[i] = vkGetAccelerationStructureDeviceAddressKHR(device, &info);
		}
		createBuffer(tlasInstanceBuffer, device, memoryProperties, sizeof(VkAccelerationStructureInstanceKHR) * scene->draws.size(), VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

		for (size_t i = 0; i < scene->draws.size(); ++i)
		{
			const MeshDraw& draw = scene->draws[i];
			assert(draw.meshIndex < blas.size());

			VkAccelerationStructureInstanceKHR instance = {};
			fillInstanceRT(instance, draw, uint32_t(i), blasAddresses[draw.meshIndex]);

			memcpy(static_cast<VkAccelerationStructureInstanceKHR*>(tlasInstanceBuffer.data) + i, &instance, sizeof(VkAccelerationStructureInstanceKHR));
		}
		tlas = createTLAS(device, tlasBuffer, tlasScratchBuffer, tlasInstanceBuffer, scene->draws.size(), memoryProperties);
		LOGI("Ray Tracing is supported!");
	}
	else
		LOGW("Ray Tracing is not supported, this may cause artifacts!");

	// Make sure we don't accidentally reuse the init command pool because that would require extra synchronization
	initCommandPool = VK_NULL_HANDLE;
	initCommandBuffer = VK_NULL_HANDLE;

	if (!frameResourcesInitialized)
	{
		swapchainImageViews.resize(swapchain.imageCount);

		for (size_t ii = 0; ii < MAX_FRAMES; ++ii)
		{
			acquireSemaphores[ii] = createSemaphore(device);
			frameFences[ii] = createFence(device);
			assert(acquireSemaphores[ii] && frameFences[ii]);
			releaseSemaphores[ii].resize(swapchain.imageCount);
			for (uint32_t jj = 0; jj < swapchain.imageCount; ++jj)
			{
				releaseSemaphores[ii][jj] = createSemaphore(device);
				assert(releaseSemaphores[ii][jj]);
			}
		}

#if defined(WIN32)
		glfwSetMouseButtonCallback(window, mouse_button_callback);
		glfwSetCursorPosCallback(window, mouse_callback);
#endif

		// Initialize GUI renderer
		const auto& guiRenderer = GuiRenderer::GetInstance();
		VkSurfaceCapabilitiesKHR surfaceCaps;
		vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &surfaceCaps);
		uint32_t imageCount = std::max(uint32_t(MAX_FRAMES), surfaceCaps.minImageCount); // using triple buffering

		if (!pushDescriptorSupported)
		{
			uint32_t setSize = MAX_FRAMES * DESCRIPTOR_SET_PER_FRAME;
			// cull descriptor sets
			{
				std::vector<VkDescriptorSetLayout> layouts(setSize, drawcullProgram.descriptorSetLayout);
				VkDescriptorSetAllocateInfo allocInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
				allocInfo.descriptorPool = descriptorPool;
				allocInfo.descriptorSetCount = setSize;
				allocInfo.pSetLayouts = layouts.data();

				auto ret = vkAllocateDescriptorSets(device, &allocInfo, (VkDescriptorSet*)drawcullSets);
				VK_CHECK(ret);
			}
			{
				std::vector<VkDescriptorSetLayout> layouts(setSize, tasksubmitProgram.descriptorSetLayout);
				VkDescriptorSetAllocateInfo allocInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
				allocInfo.descriptorPool = descriptorPool;
				allocInfo.descriptorSetCount = setSize;
				allocInfo.pSetLayouts = layouts.data();

				VK_CHECK(vkAllocateDescriptorSets(device, &allocInfo, (VkDescriptorSet*)tasksubmitSets));
			}

			// render descriptor sets
			{
				std::vector<VkDescriptorSetLayout> layouts(setSize, clustercullProgram.descriptorSetLayout);
				VkDescriptorSetAllocateInfo allocInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
				allocInfo.descriptorPool = descriptorPool;
				allocInfo.descriptorSetCount = setSize;
				allocInfo.pSetLayouts = layouts.data();

				auto ret = vkAllocateDescriptorSets(device, &allocInfo, (VkDescriptorSet*)clustercullSets);
				VK_CHECK(ret);
			}
			{
				std::vector<VkDescriptorSetLayout> layouts(setSize, clustersubmitProgram.descriptorSetLayout);
				VkDescriptorSetAllocateInfo allocInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
				allocInfo.descriptorPool = descriptorPool;
				allocInfo.descriptorSetCount = setSize;
				allocInfo.pSetLayouts = layouts.data();

				VK_CHECK(vkAllocateDescriptorSets(device, &allocInfo, (VkDescriptorSet*)clustersubmitSets));
			}
			if (meshShadingEnabled)
			{
				{
					std::vector<VkDescriptorSetLayout> layouts(setSize, meshtaskProgram.descriptorSetLayout);
					VkDescriptorSetAllocateInfo allocInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
					allocInfo.descriptorPool = descriptorPool;
					allocInfo.descriptorSetCount = setSize;
					allocInfo.pSetLayouts = layouts.data();

					auto ret = vkAllocateDescriptorSets(device, &allocInfo, (VkDescriptorSet*)meshtaskSets);
					VK_CHECK(ret);
				}
				{
					std::vector<VkDescriptorSetLayout> layouts(setSize, clusterProgram.descriptorSetLayout);
					VkDescriptorSetAllocateInfo allocInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
					allocInfo.descriptorPool = descriptorPool;
					allocInfo.descriptorSetCount = setSize;
					allocInfo.pSetLayouts = layouts.data();

					auto ret = vkAllocateDescriptorSets(device, &allocInfo, (VkDescriptorSet*)clusterSets);
					VK_CHECK(ret);
				}
			}

			{
				std::vector<VkDescriptorSetLayout> layouts(setSize, meshProgram.descriptorSetLayout);
				VkDescriptorSetAllocateInfo allocInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
				allocInfo.descriptorPool = descriptorPool;
				allocInfo.descriptorSetCount = setSize;
				allocInfo.pSetLayouts = layouts.data();

				VK_CHECK(vkAllocateDescriptorSets(device, &allocInfo, (VkDescriptorSet*)meshSets));
			}

			// shadow blur sets
			{
				std::vector<VkDescriptorSetLayout> layouts(setSize, shadowblurProgram.descriptorSetLayout);
				VkDescriptorSetAllocateInfo allocInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
				allocInfo.descriptorPool = descriptorPool;
				allocInfo.descriptorSetCount = setSize;
				allocInfo.pSetLayouts = layouts.data();

				VK_CHECK(vkAllocateDescriptorSets(device, &allocInfo, (VkDescriptorSet*)shadowblurSets));
			}
		}

		VkPipelineRenderingCreateInfo renderingInfo = { VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
		renderingInfo.colorAttachmentCount = 1;
		renderingInfo.pColorAttachmentFormats = &swapchainFormat;
		renderingInfo.depthAttachmentFormat = depthFormat;

		guiRenderer->Initialize(window, API_VERSION, instance, physicalDevice, device, graphicsFamily, queue, renderingInfo, swapchainFormat, imageCount);

		lastFrame = GetTimeInSeconds();
		frameResourcesInitialized = true;
	}
}

// Main frame path: scene + UI setup, then record rendering (RenderGraph + legacy barriers in-record).
bool VulkanContext::DrawFrame()
{
	static uint64_t frameIndex = 0;
	static bool g_taaHistoryReady = false;
	static bool previousTaaEnabled = taaEnabled;
	double frameCPUBegin = GetTimeInSeconds();

	if (previousTaaEnabled != taaEnabled)
	{
		g_taaHistoryReady = false;
		previousTaaEnabled = taaEnabled;
	}

	resourceManager.BeginFrame();

	const auto& guiRenderer = GuiRenderer::GetInstance();
	const bool shouldRenderRuntimeUi = runtimeUiEnabled;
	g_editorViewportInputMode = editorViewportMode;
	if (!editorViewportMode)
		g_editorViewportRectValid = false;
#if defined(WIN32)
	glfwPollEvents();
#endif
	if (shouldRenderRuntimeUi)
	{
		guiRenderer->BeginFrame();
		ImVec2 windowSize = ImVec2(800, 600);
		ImGui::SetNextWindowSize(windowSize, ImGuiCond_FirstUseEver);
	}

	// update camera position
	float currentFrame = GetTimeInSeconds();
	float deltaTime = currentFrame - lastFrame;
	lastFrame = currentFrame;

	glm::vec3 front = scene->camera.orientation * glm::vec3(0.0f, 0.0f, -1.0f);
	glm::vec3 right = scene->camera.orientation * glm::vec3(1.0f, 0.0f, 0.0f);
	glm::vec3 up = glm::cross(right, front);
	const bool blockCameraByUi = shouldRenderRuntimeUi && ImGui::GetIO().WantCaptureMouse;

	// Apply virtual stick look (Android): right stick -> yaw/pitch.
	if (!blockCameraByUi && (g_lookX != 0.0f || g_lookY != 0.0f))
	{
		const float lookSpeedDegPerSec = 120.0f;
		yaw += g_lookX * lookSpeedDegPerSec * deltaTime;
		pitch += g_lookY * lookSpeedDegPerSec * deltaTime;
		cameraDirty = true;
	}

	// Apply virtual stick movement (Android): left stick -> move/strafe.
	if (!blockCameraByUi && (g_moveX != 0.0f || g_moveY != 0.0f))
	{
		const float moveSpeedScale = 1.2f; // slightly faster than keyboard
		float velocity = cameraSpeed * moveSpeedScale * deltaTime;
		scene->camera.position += front * (g_moveY * velocity);
		scene->camera.position += right * (g_moveX * velocity);
	}

#if defined(WIN32)
	float velocity = cameraSpeed * deltaTime;

	if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) != GLFW_PRESS)
	{
		if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
			scene->camera.position -= front * velocity;
		if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
			scene->camera.position += front * velocity;
		if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
			scene->camera.position -= right * velocity;
		if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
			scene->camera.position += right * velocity;
	}

	glfwPollEvents();
#endif

#if defined(WIN32)
	if (reloadShaders && glfwGetTime() >= reloadShadersTimer)
	{
		bool changed = false;

		for (Shader& shader : shaderSet.shaders)
		{
#if defined(__ANDROID__) // Remove this when upgrading to Vulkan 1.4
			if (shader.module)
				vkDestroyShaderModule(device, shader.module, 0);
#endif

			std::vector<char> oldSpirv = std::move(shader.spirv);

			std::string spirvPath = "/shaders/" + shader.name + ".spv";
#if defined(WIN32)
			bool rcs = loadShader(shader, scene->path.c_str(), spirvPath.c_str());
#elif defined(__ANDROID__) // Remove this when upgrading to Vulkan 1.4
			bool rcs = loadShader(shader, device, scene->path.c_str(), spirvPath.c_str());
#endif
			assert(rcs);

			changed |= oldSpirv != shader.spirv;
		}

		if (changed)
		{
			VK_CHECK(vkDeviceWaitIdle(device));
			pipelinesReloadedCallback();
			reloadShadersColor = 0x00ff00;
		}
		else
		{
			reloadShadersColor = 0xffffff;
		}

		reloadShadersTimer = glfwGetTime() + 1;
	}
#endif

	if (cameraDirty)
	{
		updateCamera();
		cameraDirty = false;
	}

	SwapchainStatus swapchainStatus = updateSwapchain(swapchain, physicalDevice, device, surface, graphicsFamily, window, swapchainFormat);

	if (swapchainStatus == Swapchain_NotReady)
		return true;

	if (!editorViewportMode && editorViewportDescriptorSet != VK_NULL_HANDLE)
	{
		pendingViewportDescriptorReleases.push_back({ editorViewportDescriptorSet, frameIndex + uint64_t(MAX_FRAMES) + 1ull });
		editorViewportDescriptorSet = VK_NULL_HANDLE;
	}
	if (!editorViewportMode && editorViewportTargetHandle.IsValid())
	{
		resourceManager.ReleaseTexture(editorViewportTargetHandle);
		editorViewportTargetHandle = {};
	}

	const uint32_t desiredRenderWidth = (editorViewportMode && editorViewportWidth > 0) ? editorViewportWidth : uint32_t(swapchain.width);
	const uint32_t desiredRenderHeight = (editorViewportMode && editorViewportHeight > 0) ? editorViewportHeight : uint32_t(swapchain.height);

	Image* depthTarget = resourceManager.GetTexture(depthTargetHandle);

	if (swapchainStatus == Swapchain_Resized || !depthTarget ||
	    (currentRenderWidth != desiredRenderWidth || currentRenderHeight != desiredRenderHeight))
	{
		for (uint32_t i = 0; i < gbufferCount; ++i)
			if (gbufferTargetHandles[i].IsValid())
				resourceManager.ReleaseTexture(gbufferTargetHandles[i]);
		if (depthTargetHandle.IsValid())
			resourceManager.ReleaseTexture(depthTargetHandle);

		if (depthPyramidHandle.IsValid())
		{
			for (uint32_t i = 0; i < depthPyramidLevels; ++i)
			{
				resourceManager.ReleaseImageView(depthPyramidMips[i]);
				depthPyramidMips[i] = VK_NULL_HANDLE;
			}
			resourceManager.ReleaseTexture(depthPyramidHandle);
		}

		if (shadowTargetHandle.IsValid())
			resourceManager.ReleaseTexture(shadowTargetHandle);
		if (shadowblurTargetHandle.IsValid())
			resourceManager.ReleaseTexture(shadowblurTargetHandle);
		if (sceneColorHDRHandle.IsValid())
			resourceManager.ReleaseTexture(sceneColorHDRHandle);
		if (sceneColorResolvedHandle.IsValid())
			resourceManager.ReleaseTexture(sceneColorResolvedHandle);

		for (int ti = 0; ti < 2; ++ti)
			if (taaHistoryHandles[ti].IsValid())
				resourceManager.ReleaseTexture(taaHistoryHandles[ti]);

		if (editorViewportTargetHandle.IsValid())
		{
			if (editorViewportDescriptorSet != VK_NULL_HANDLE)
			{
				pendingViewportDescriptorReleases.push_back({ editorViewportDescriptorSet, frameIndex + uint64_t(MAX_FRAMES) + 1ull });
				editorViewportDescriptorSet = VK_NULL_HANDLE;
			}
			resourceManager.ReleaseTexture(editorViewportTargetHandle);
			editorViewportTargetHandle = {};
		}

		for (uint32_t i = 0; i < gbufferCount; ++i)
		{
			RGTextureDesc gbufDesc{};
			gbufDesc.width = desiredRenderWidth;
			gbufDesc.height = desiredRenderHeight;
			gbufDesc.mipLevels = 1;
			gbufDesc.usage = TextureUsage::ColorAttachment | TextureUsage::Sampled;

			switch (gbufferFormats[i])
			{
			case VK_FORMAT_R8G8B8A8_UNORM:
				gbufDesc.format = TextureFormat::RGBA8_UNorm;
				break;
			case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
				gbufDesc.format = TextureFormat::A2B10G10R10_UNorm;
				break;
			case VK_FORMAT_R32_UINT:
				gbufDesc.format = TextureFormat::R32_UINT;
				break;
			default:
				// Fallback to something reasonable.
				gbufDesc.format = TextureFormat::RGBA8_UNorm;
				break;
			}

			gbufferTargetHandles[i] = resourceManager.CreateTexture(gbufDesc);
		}

		RGTextureDesc depthDesc{};
		depthDesc.width = desiredRenderWidth;
		depthDesc.height = desiredRenderHeight;
		depthDesc.mipLevels = 1;
		depthDesc.format = TextureFormat::D32_Float;
		depthDesc.usage = TextureUsage::DepthStencil | TextureUsage::Sampled;
		depthTargetHandle = resourceManager.CreateTexture(depthDesc);

		depthTarget = resourceManager.GetTexture(depthTargetHandle);
		assert(depthTarget);

		RGTextureDesc shadowDesc{};
		shadowDesc.width = desiredRenderWidth;
		shadowDesc.height = desiredRenderHeight;
		shadowDesc.mipLevels = 1;
		shadowDesc.format = TextureFormat::R8_UNorm;
		shadowDesc.usage = TextureUsage::Storage | TextureUsage::Sampled;

		shadowTargetHandle = resourceManager.CreateTexture(shadowDesc, /* transient= */ false);
		shadowblurTargetHandle = resourceManager.CreateTexture(shadowDesc, /* transient= */ false);

		RGTextureDesc sceneColorDesc{};
		sceneColorDesc.width = desiredRenderWidth;
		sceneColorDesc.height = desiredRenderHeight;
		sceneColorDesc.mipLevels = 1;
		sceneColorDesc.format = TextureFormat::RGBA8_UNorm;
		// ColorAttachment: optional alpha-blend pass into this target when screen-space refraction is disabled.
		sceneColorDesc.usage = TextureUsage::Storage | TextureUsage::Sampled | TextureUsage::ColorAttachment;
		sceneColorHDRHandle = resourceManager.CreateTexture(sceneColorDesc, /* transient= */ false);
		sceneColorResolvedHandle = resourceManager.CreateTexture(sceneColorDesc, /* transient= */ false);

		RGTextureDesc taaDesc{};
		taaDesc.width = desiredRenderWidth;
		taaDesc.height = desiredRenderHeight;
		taaDesc.mipLevels = 1;
		taaDesc.format = TextureFormat::RGBA8_UNorm;
		taaDesc.usage = TextureUsage::Storage | TextureUsage::Sampled;
		taaHistoryHandles[0] = resourceManager.CreateTexture(taaDesc, false);
		taaHistoryHandles[1] = resourceManager.CreateTexture(taaDesc, false);
		g_taaHistoryReady = false;

		// Note: previousPow2 makes sure all reductions are at most by 2x2 which makes sure they are consertive
		depthPyramidWidth = previousPow2(desiredRenderWidth);
		depthPyramidHeight = previousPow2(desiredRenderHeight);
		depthPyramidLevels = getImageMipLevels(depthPyramidWidth, depthPyramidHeight);

		RGTextureDesc depthPyramidDesc{};
		depthPyramidDesc.width = depthPyramidWidth;
		depthPyramidDesc.height = depthPyramidHeight;
		depthPyramidDesc.mipLevels = depthPyramidLevels;
		depthPyramidDesc.format = TextureFormat::R32_Float;
		depthPyramidDesc.usage = TextureUsage::Sampled | TextureUsage::Storage | TextureUsage::TransferSrc;

		depthPyramidHandle = resourceManager.CreateTexture(depthPyramidDesc);
		Image* depthPyramid = resourceManager.GetTexture(depthPyramidHandle);
		assert(depthPyramid);

		for (uint32_t i = 0; i < depthPyramidLevels; ++i)
		{
			depthPyramidMips[i] = resourceManager.AcquireImageView(depthPyramid->image, VK_FORMAT_R32_SFLOAT, i, 1, /* transient= */ false);
			assert(depthPyramidMips[i]);
		}

		for (uint32_t i = 0; i < swapchain.imageCount; ++i)
		{
			if (swapchainImageViews[i])
			{
				resourceManager.DestroyImageView(swapchainImageViews[i]);
				swapchainImageViews[i] = VK_NULL_HANDLE;
			}

			swapchainImageViews[i] = resourceManager.CreateImageView(swapchain.images[i], swapchainFormat, 0, 1);
		}

		if (!pushDescriptorSupported)
		{
			for (size_t ii = 0; ii < MAX_FRAMES; ++ii)
			{
				if (!depthreduceSets[ii].empty())
					vkFreeDescriptorSets(device, descriptorPool, uint32_t(depthreduceSets[ii].size()), depthreduceSets[ii].data());

				depthreduceSets[ii].resize(depthPyramidLevels);
				std::vector<VkDescriptorSetLayout> layouts(depthPyramidLevels, depthreduceProgram.descriptorSetLayout);
				VkDescriptorSetAllocateInfo allocInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
				allocInfo.descriptorPool = descriptorPool;
				allocInfo.descriptorSetCount = depthPyramidLevels;
				allocInfo.pSetLayouts = layouts.data();

				VK_CHECK(vkAllocateDescriptorSets(device, &allocInfo, depthreduceSets[ii].data()));
			}
		}

		if (editorViewportMode)
		{
			RGTextureDesc viewportDesc{};
			viewportDesc.width = desiredRenderWidth;
			viewportDesc.height = desiredRenderHeight;
			viewportDesc.mipLevels = 1;
			viewportDesc.format = TextureFormat::RGBA8_UNorm;
			// TransferSrc is required for vkCmdCopyImageToBuffer (viewport EXR/PNG dump readback).
			viewportDesc.usage = TextureUsage::Storage | TextureUsage::Sampled | TextureUsage::TransferSrc | TextureUsage::ColorAttachment;
			editorViewportTargetHandle = resourceManager.CreateTexture(viewportDesc, /* transient= */ false);
		}

		const uint64_t safePurgeFrame = frameIndex + uint64_t(MAX_FRAMES) + 1ull;
		if (pendingTexturePoolPurgeAfterFrame < safePurgeFrame)
			pendingTexturePoolPurgeAfterFrame = safePurgeFrame;

		currentRenderWidth = desiredRenderWidth;
		currentRenderHeight = desiredRenderHeight;
	}

	Image* gbufferTargets[gbufferCount] = {};
	for (uint32_t i = 0; i < gbufferCount; ++i)
	{
		gbufferTargets[i] = resourceManager.GetTexture(gbufferTargetHandles[i]);
		assert(gbufferTargets[i]);
	}
	depthTarget = resourceManager.GetTexture(depthTargetHandle);
	assert(depthTarget);
	if (depthTarget->imageView == VK_NULL_HANDLE)
	{
		LOGW("Depth target view is null; recreate lazily (handle=%u image=%p)", depthTargetHandle.id, depthTarget->image);
		depthTarget->imageView = resourceManager.CreateImageView(depthTarget->image, depthFormat, 0, 1);
	}

	Image* sceneColorTarget = resourceManager.GetTexture(sceneColorHDRHandle);
	assert(sceneColorTarget);

	const uint32_t renderWidth = currentRenderWidth;
	const uint32_t renderHeight = currentRenderHeight;

	// TODO: this code races the GPU reading the transforms from both TLAS and draw buffers, which can cause rendering issues
	if (animationEnabled && !scene->transformNodes.empty())
	{
		static double animationTime = 0.0; // TODO: handle overflow when the program last for long time
		animationTime += deltaTime;

		for (Animation& animation : scene->animations)
		{
			if (animation.gltfNodeIndex >= scene->transformNodes.size())
				continue;

			double index = (animationTime - animation.startTime) / animation.period;

			if (index < 0)
				continue;

			index = fmod(index, double(animation.keyframes.size()));

			int index0 = int(index) % int(animation.keyframes.size());
			int index1 = (index0 + 1) % int(animation.keyframes.size());

			double t = index - floor(index);

			const Keyframe& keyframe0 = animation.keyframes[size_t(index0)];
			const Keyframe& keyframe1 = animation.keyframes[size_t(index1)];

			const glm::vec3 tr = glm::mix(keyframe0.translation, keyframe1.translation, float(t));
			const glm::vec3 scl = glm::mix(keyframe0.scale, keyframe1.scale, float(t));
			const glm::quat rot = glm::slerp(keyframe0.rotation, keyframe1.rotation, float(t));

			TransformNode& node = scene->transformNodes[animation.gltfNodeIndex];
			node.local = glm::translate(glm::mat4(1.f), tr) * glm::mat4_cast(rot) * glm::scale(glm::mat4(1.f), scl);
			MarkTransformSubtreeDirty(*scene, animation.gltfNodeIndex);
		}

		FlushSceneTransforms(*scene);
	}

	if (scene->transformsGpuDirty && db.data)
	{
		memcpy(db.data, scene->draws.data(), scene->draws.size() * sizeof(MeshDraw));
		if (raytracingSupported && tlasInstanceBuffer.data)
		{
			for (size_t i = 0; i < scene->draws.size(); ++i)
			{
				const MeshDraw& draw = scene->draws[i];
				VkAccelerationStructureInstanceKHR instance = {};
				fillInstanceRT(instance, draw, uint32_t(i), blasAddresses[draw.meshIndex]);
				memcpy(static_cast<VkAccelerationStructureInstanceKHR*>(tlasInstanceBuffer.data) + i, &instance, sizeof(VkAccelerationStructureInstanceKHR));
			}
		}
		scene->transformsGpuDirty = false;
	}

	if (editorViewportMode && scene->materialDb.gpuDirty && mtb.buffer && !scene->materialDb.gpuMaterials.empty())
	{
		if (mtb.data)
		{
			memcpy(mtb.data, scene->materialDb.gpuMaterials.data(), scene->materialDb.gpuMaterials.size() * sizeof(Material));
		}
		else
		{
			uploadBuffer(device, commandPools[0], commandBuffers[0], queue, mtb, scratch,
			    scene->materialDb.gpuMaterials.data(), scene->materialDb.gpuMaterials.size() * sizeof(Material));
		}
		scene->materialDb.gpuDirty = false;
	}

	uint8_t frameOffset = frameIndex % MAX_FRAMES;

	if (autoExitAfterViewportDump && !autoExrFired && !autoDumpExrPathPending.empty() && frameIndex >= autoDumpExrFireAtFrame
	    && editorViewportMode && !editorViewportDumpRequested)
	{
		editorViewportDumpRequested = true;
		editorViewportDumpSaveAsExr = !PathEndsWithExtensionIgnoreCase(autoDumpExrPathPending, ".png");
		editorViewportDumpRequestPath = autoDumpExrPathPending;
		autoExrFired = true;
	}

	if (!pushDescriptorSupported && depthPyramidLevels > 0 && depthreduceSets[frameOffset].empty())
	{
		LOGW("Depth-reduce descriptor sets missing on frame %u; allocating lazily (%u levels)",
		    uint32_t(frameOffset), depthPyramidLevels);

		for (size_t ii = 0; ii < MAX_FRAMES; ++ii)
		{
			if (!depthreduceSets[ii].empty())
				continue;

			depthreduceSets[ii].resize(depthPyramidLevels);
			std::vector<VkDescriptorSetLayout> layouts(depthPyramidLevels, depthreduceProgram.descriptorSetLayout);
			VkDescriptorSetAllocateInfo allocInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
			allocInfo.descriptorPool = descriptorPool;
			allocInfo.descriptorSetCount = depthPyramidLevels;
			allocInfo.pSetLayouts = layouts.data();
			VK_CHECK(vkAllocateDescriptorSets(device, &allocInfo, depthreduceSets[ii].data()));
		}
	}

	VkCommandPool commandPool = commandPools[frameOffset];
	VkCommandBuffer commandBuffer = commandBuffers[frameOffset];
	VkSemaphore acquireSemaphore = acquireSemaphores[frameOffset];
	VkFence frameFence = frameFences[frameOffset];
	VkQueryPool queryPoolTimestamp = queryPoolsTimestamp[frameOffset];
#if defined(WIN32)
	VkQueryPool queryPoolPipeline = queryPoolsPipeline[frameOffset];
#endif

	uint32_t imageIndex = 0;
	VkResult acquireResult = vkAcquireNextImageKHR(device, swapchain.swapchain, ~0ull, acquireSemaphore, VK_NULL_HANDLE, &imageIndex);

	VkSemaphore releaseSemaphore = releaseSemaphores[frameOffset][imageIndex];
	Image* finalOutputImage = (editorViewportMode && editorViewportTargetHandle.IsValid()) ? resourceManager.GetTexture(editorViewportTargetHandle) : nullptr;
	VkImage finalOutputVkImage = finalOutputImage ? finalOutputImage->image : swapchain.images[imageIndex];
	VkImageView finalOutputImageView = finalOutputImage ? finalOutputImage->imageView : swapchainImageViews[imageIndex];

	if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR)
		return true; // attempting to render to an out-of-date swapchain would break semaphore synchronization
	VK_CHECK_SWAPCHAIN(acquireResult);

	VK_CHECK(vkResetCommandPool(device, commandPool, 0));

	VkCommandBufferBeginInfo beginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

	VK_CHECK(vkBeginCommandBuffer(commandBuffer, &beginInfo));

	vkCmdResetQueryPool(commandBuffer, queryPoolTimestamp, 0, 128);
	vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, queryPoolTimestamp, TS_FrameBegin);

	if (!dvbCleared)
	{
		// TODO: this is stupidly redundant
		vkCmdFillBuffer(commandBuffer, dvb.buffer, 0, sizeof(uint32_t) * scene->draws.size(), 0);
		VkBufferMemoryBarrier2 fillBarrier = bufferBarrier(dvb.buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
#if defined(WIN32)
		                                                                                                                                 | VK_PIPELINE_STAGE_TASK_SHADER_BIT_EXT
#endif
		    ,
		    VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
		pipelineBarrier(commandBuffer, 0, 1, &fillBarrier, 0, nullptr);

		dvbCleared = true;
	}

	if (!mvbCleared && meshShadingSupported)
	{
		// TODO: this is stupidly redundant
		vkCmdFillBuffer(commandBuffer, mvb.buffer, 0, meshletVisibilityBytes, 0);

		VkBufferMemoryBarrier2 fillBarrier = bufferBarrier(mvb.buffer,
		    VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
		    VK_PIPELINE_STAGE_TASK_SHADER_BIT_EXT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
		pipelineBarrier(commandBuffer, 0, 1, &fillBarrier, 0, nullptr);

		mvbCleared = true;
	}

	if (raytracingSupported)
	{
		uint32_t timestamp = 21;

		vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, queryPoolTimestamp, timestamp + 0);

		if (tlasNeedsRebuild)
		{
			buildTLAS(device, commandBuffer, tlas, tlasBuffer, tlasScratchBuffer, tlasInstanceBuffer, scene->draws.size(), VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR);
			tlasNeedsRebuild = false;
		}
		else if (animationEnabled)
			buildTLAS(device, commandBuffer, tlas, tlasBuffer, tlasScratchBuffer, tlasInstanceBuffer, scene->draws.size(), VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR);

		vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, queryPoolTimestamp, timestamp + 1);
	}

	mat4 view = glm::mat4_cast(scene->camera.orientation);
	// view[3] = vec4(-camera.position, 1.0f);
	// view = inverse(view);
	//  view = glm::scale(glm::identity<glm::mat4>(), vec3(1, 1, 1)) * view;
	view = glm::lookAt(scene->camera.position, scene->camera.position + front, up);

	mat4 projection;
	if (enableDollyZoom)
	{
		float so = soRef - glm::abs(glm::dot(glm::normalize(front), scene->camera.position - cameraOriginForDolly));
		projection = perspectiveProjectionDollyZoom(scene->camera.fovY, float(renderWidth) / float(renderHeight), scene->camera.znear, so, soRef);
	}
	else
	{
		projection = perspectiveProjection(scene->camera.fovY, float(renderWidth) / float(renderHeight), scene->camera.znear);
	}

	mat4 projectionJittered = projection;
	if (taaEnabled)
	{
		uint32_t jitterSample = uint32_t(frameIndex % 4) + 1u;
		float jx = halton(jitterSample, 2u);
		float jy = halton(jitterSample, 3u);
		projectionJittered[2][0] += (jx - 0.5f) * (2.0f / float(renderWidth));
		projectionJittered[2][1] += (jy - 0.5f) * (2.0f / float(renderHeight));
	}

	mat4 projectionT = transpose(projection);

	vec4 frustumX = normalizePlane(projectionT[3] + projectionT[0]); // x + w < 0
	vec4 frustumY = normalizePlane(projectionT[3] + projectionT[1]); // y + w < 0

	CullData cullData = {};
	cullData.view = view;
	cullData.P00 = projection[0][0];
	cullData.P11 = projection[1][1];
	cullData.znear = scene->camera.znear;
	cullData.zfar = scene->drawDistance;
	cullData.frustum[0] = frustumX.x;
	cullData.frustum[1] = frustumX.z;
	cullData.frustum[2] = frustumY.y;
	cullData.frustum[3] = frustumY.z;
	cullData.drawCount = uint32_t(scene->draws.size());
	cullData.cullingEnabled = int(cullingEnabled);
	cullData.lodEnabled = int(lodEnabled);
	cullData.occlusionEnabled = int(occlusionEnabled);
	cullData.lodTarget = (2 / cullData.P11) * (1.f / float(renderHeight)) * (1 << debugLodStep); // 1px
	cullData.pyramidWidth = float(depthPyramidWidth);
	cullData.clusterOcclusionEnabled = occlusionEnabled && clusterOcclusionEnabled && meshShadingSupported && meshShadingEnabled;

	Globals globals = {};
	globals.projection = projectionJittered;
	globals.cullData = cullData;

	globals.screenWidth = float(renderWidth);
	globals.screenHeight = float(renderHeight);

	uint32_t debugView = uint32_t(gbufferDebugViewMode);
	if (debugView > 2u)
		debugView = 0u;
	if (debugView == 1u && !wireframeDebugSupported)
		debugView = 0u;
	globals.gbufferDebugMode = debugView;
	globals.sunDirection = scene->sunDirection;
	globals.selectionOutlinePass = 0;
	globals.selectionOutlineWidth = 0.f;

	const mat4 inverseViewProjection = inverse(projectionJittered * view);

	bool taskSubmit = meshShadingSupported && meshShadingEnabled; // TODO; refactor this to be false when taskShadingEnabled is false
	bool clusterSubmit = meshShadingSupported && meshShadingEnabled && !taskShadingEnabled;

	auto fullbarrier = [&]()
	{
		VkMemoryBarrier2 barrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER_2 };
		barrier.srcStageMask = barrier.dstStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
		barrier.srcAccessMask = barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
		VkDependencyInfo dependencyInfo = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
		dependencyInfo.memoryBarrierCount = 1;
		dependencyInfo.pMemoryBarriers = &barrier;
		vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);
	};

	auto cull = [&](VkPipeline pipeline, uint32_t timestamp, const char* phase, bool late, unsigned int postPass = 0)
	{
		Image* depthPyramid = resourceManager.GetTexture(depthPyramidHandle);
		assert(depthPyramid);

		size_t descriptorSetIndex = late ? (postPass > 0 ? 2 : 1) : 0;
		uint32_t rasterizationStage =
		    taskSubmit
		        ? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
#if defined(WIN32)
		              | VK_PIPELINE_STAGE_TASK_SHADER_BIT_EXT | VK_PIPELINE_STAGE_MESH_SHADER_BIT_EXT
#endif
		        : VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;

		vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, queryPoolTimestamp, timestamp + 0);

		VkBufferMemoryBarrier2 prefillBarrier = bufferBarrier(dccb.buffer,
		    VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT, VK_ACCESS_INDIRECT_COMMAND_READ_BIT,
		    VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
		pipelineBarrier(commandBuffer, 0, 1, &prefillBarrier, 0, nullptr);

		vkCmdFillBuffer(commandBuffer, dccb.buffer, 0, 4, 0);

		// pyramid barrier is tricky: our frame sequence is cull -> render -> pyramid -> cull -> render
		// the first cull (late=0) doesn't read pyramid data BUT the read in the shader is guarded by a push constant value (which could be specialization constant but isn't due to AMD bug)
		// the second cull (late=1) does read pyramid data that was written in the pyramid stage
		// as such, second cull needs to transition GENERAL->GENERAL with a COMPUTE->COMPUTE barrier, but the first cull needs to have a dummy transition because pyramid starts in UNDEFINED state on first frame
		VkImageMemoryBarrier2 pyramidBarrier = imageBarrier(depthPyramid->image,
		    late ? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT : 0, late ? VK_ACCESS_SHADER_WRITE_BIT : 0, late ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED,
		    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL);

		VkBufferMemoryBarrier2 fillBarriers[] = {
			bufferBarrier(dcb.buffer,
			    VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | rasterizationStage, VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_SHADER_READ_BIT,
			    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT),
			bufferBarrier(dccb.buffer,
			    VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
			    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT),
		};

		pipelineBarrier(commandBuffer, 0, COUNTOF(fillBarriers), fillBarriers, 1, &pyramidBarrier);

		{
			CullData passData = cullData;
			passData.clusterBackfaceEnabled = postPass == 0;
			passData.postPass = postPass;
			vkCmdBindPipeline(commandBuffer, drawcullProgram.bindPoint, pipeline);

			DescriptorInfo pyramidDesc{ depthSampler, depthPyramid->imageView, VK_IMAGE_LAYOUT_GENERAL };
			DescriptorInfo descriptors[] = { db.buffer, mb.buffer, dcb.buffer, dccb.buffer, dvb.buffer, pyramidDesc };

			if (pushDescriptorSupported)
			{
				dispatch(commandBuffer, drawcullProgram, uint32_t(scene->draws.size()), 1, passData, descriptors);
			}
			else
			{
				vkUpdateDescriptorSetWithTemplateKHR(device, drawcullSets[frameOffset][descriptorSetIndex], drawcullProgram.updateTemplate, descriptors);
				vkCmdBindDescriptorSets(commandBuffer, drawcullProgram.bindPoint, drawcullProgram.layout, 0, 1, &drawcullSets[frameOffset][descriptorSetIndex], 0, nullptr);
				vkCmdPushConstants(commandBuffer, drawcullProgram.layout, drawcullProgram.pushConstantStages, 0, sizeof(cullData), &passData);
				vkCmdDispatch(commandBuffer, getGroupCount(uint32_t(scene->draws.size()), drawcullProgram.localSizeX), 1, 1);
			}
		}

		if (taskSubmit)
		{
			VkBufferMemoryBarrier2 syncBarrier = bufferBarrier(dccb.buffer,
			    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
			    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);

			pipelineBarrier(commandBuffer, 0, 1, &syncBarrier, 0, nullptr);

			vkCmdBindPipeline(commandBuffer, tasksubmitProgram.bindPoint, tasksubmitPipeline);

			DescriptorInfo descriptors[] = { dccb.buffer, dcb.buffer };
#if defined(WIN32)
			if (pushDescriptorSupported)
			{
				vkCmdPushDescriptorSetWithTemplate(commandBuffer, tasksubmitProgram.updateTemplate, tasksubmitProgram.layout, 0, descriptors);
			}
			else
#endif
			{
				vkUpdateDescriptorSetWithTemplateKHR(device, tasksubmitSets[frameOffset][descriptorSetIndex], tasksubmitProgram.updateTemplate, descriptors);
				vkCmdBindDescriptorSets(commandBuffer, tasksubmitProgram.bindPoint, tasksubmitProgram.layout, 0, 1, &tasksubmitSets[frameOffset][descriptorSetIndex], 0, nullptr);
			}
			vkCmdDispatch(commandBuffer, 1, 1, 1);
		}

		VkBufferMemoryBarrier2 cullBarriers[] = {
			bufferBarrier(dcb.buffer,
			    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
			    VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | rasterizationStage, VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_SHADER_READ_BIT),
			bufferBarrier(dccb.buffer,
			    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
			    VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT, VK_ACCESS_INDIRECT_COMMAND_READ_BIT),
		};

		pipelineBarrier(commandBuffer, 0, COUNTOF(cullBarriers), cullBarriers, 0, nullptr);

		vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, queryPoolTimestamp, timestamp + 1);
	};

	auto render = [&](bool late, const std::vector<VkClearColorValue>& clearColors, const VkClearDepthStencilValue& depthClear, uint32_t query, uint32_t timestamp, const char* phase, unsigned int postPass = 0, bool alphaBlendToSceneColor = false)
	{
		Image* depthPyramid = resourceManager.GetTexture(depthPyramidHandle);
		assert(depthPyramid);

		size_t descriptorSetIndex = late ? (postPass > 0 ? 2 : 1) : 0;
		assert(clearColors.size() == gbufferCount);
		vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, queryPoolTimestamp, timestamp + 0);

#if defined(WIN32)
		vkCmdBeginQuery(commandBuffer, queryPoolPipeline, query, 0);
#endif

		if (clusterSubmit)
		{
			VkBufferMemoryBarrier2 prefillBarrier = bufferBarrier(ccb.buffer,
			    VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT, VK_ACCESS_INDIRECT_COMMAND_READ_BIT,
			    VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
			pipelineBarrier(commandBuffer, 0, 1, &prefillBarrier, 0, nullptr);

			vkCmdFillBuffer(commandBuffer, ccb.buffer, 0, 4, 0);

			VkBufferMemoryBarrier2 fillBarriers[] = {
				bufferBarrier(cib.buffer,
				    VK_PIPELINE_STAGE_MESH_SHADER_BIT_EXT, VK_ACCESS_SHADER_READ_BIT,
				    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT),
				bufferBarrier(ccb.buffer,
				    VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
				    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT),
			};
			pipelineBarrier(commandBuffer, 0, COUNTOF(fillBarriers), fillBarriers, 0, nullptr);

			vkCmdBindPipeline(commandBuffer, clustercullProgram.bindPoint, late ? clusterculllatePipeline : clustercullPipeline);

			DescriptorInfo pyramidDesc(depthSampler, depthPyramid->imageView, VK_IMAGE_LAYOUT_GENERAL);
			DescriptorInfo descriptors[] = { dcb.buffer, db.buffer, mlb.buffer, mvb.buffer, pyramidDesc, cib.buffer, ccb.buffer };

#if defined(WIN32)
			if (pushDescriptorSupported)
			{
				vkCmdPushDescriptorSetWithTemplate(commandBuffer, clustercullProgram.updateTemplate, clustercullProgram.layout, 0, descriptors);
			}
			else
#endif
			{
				vkUpdateDescriptorSetWithTemplateKHR(device, clustercullSets[frameOffset][descriptorSetIndex], clustercullProgram.updateTemplate, descriptors);
				vkCmdBindDescriptorSets(commandBuffer, clustercullProgram.bindPoint, clustercullProgram.layout, 0, 1, &clustercullSets[frameOffset][descriptorSetIndex], 0, nullptr);
			}

			CullData passData = cullData;
			passData.postPass = postPass;

			vkCmdPushConstants(commandBuffer, clustercullProgram.layout, clustercullProgram.pushConstantStages, 0, sizeof(cullData), &passData);
			vkCmdDispatchIndirect(commandBuffer, dccb.buffer, 4);

			VkBufferMemoryBarrier2 syncBarrier = bufferBarrier(ccb.buffer,
			    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
			    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);

			pipelineBarrier(commandBuffer, 0, 1, &syncBarrier, 0, nullptr);

			vkCmdBindPipeline(commandBuffer, clustersubmitProgram.bindPoint, clustersubmitPipeline);

			DescriptorInfo descriptors2[] = { ccb.buffer, cib.buffer };

#if defined(WIN32)
			if (pushDescriptorSupported)
			{
				vkCmdPushDescriptorSetWithTemplate(commandBuffer, clustersubmitProgram.updateTemplate, clustersubmitProgram.layout, 0, descriptors2);
			}
			else
#endif
			{
				vkUpdateDescriptorSetWithTemplateKHR(device, clustersubmitSets[frameOffset][descriptorSetIndex], clustersubmitProgram.updateTemplate, descriptors2);
				vkCmdBindDescriptorSets(commandBuffer, clustersubmitProgram.bindPoint, clustersubmitProgram.layout, 0, 1, &clustersubmitSets[frameOffset][descriptorSetIndex], 0, nullptr);
			}

			vkCmdDispatch(commandBuffer, 1, 1, 1);

			VkBufferMemoryBarrier2 cullBarriers[] = {
				bufferBarrier(cib.buffer,
				    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
				    VK_PIPELINE_STAGE_MESH_SHADER_BIT_EXT, VK_ACCESS_SHADER_READ_BIT),
				bufferBarrier(ccb.buffer,
				    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
				    VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT, VK_ACCESS_INDIRECT_COMMAND_READ_BIT),
			};

			pipelineBarrier(commandBuffer, 0, COUNTOF(cullBarriers), cullBarriers, 0, nullptr);
		}

		VkRenderingAttachmentInfo gbufferAttachments[gbufferCount] = {};
		if (!alphaBlendToSceneColor)
		{
			for (uint32_t i = 0; i < gbufferCount; ++i)
			{
				gbufferAttachments[i].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
				gbufferAttachments[i].imageView = gbufferTargets[i]->imageView;
				gbufferAttachments[i].imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
				gbufferAttachments[i].loadOp = late ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR;
				gbufferAttachments[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
				gbufferAttachments[i].clearValue.color = clearColors[i];
			}
		}

		VkRenderingAttachmentInfo depthAttachment = { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
		depthAttachment.imageView = depthTarget->imageView;
		depthAttachment.imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
		depthAttachment.loadOp = late ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR;
		depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		depthAttachment.clearValue.depthStencil = depthClear;

		VkRenderingAttachmentInfo sceneColorBlendAttachment = { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
		VkRenderingInfo passInfo = { VK_STRUCTURE_TYPE_RENDERING_INFO };
		passInfo.renderArea.extent.width = renderWidth;
		passInfo.renderArea.extent.height = renderHeight;
		passInfo.layerCount = 1;
		passInfo.pDepthAttachment = &depthAttachment;

		if (alphaBlendToSceneColor)
		{
			assert(postPass == 1);
			sceneColorBlendAttachment.imageView = sceneColorTarget->imageView;
			sceneColorBlendAttachment.imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
			sceneColorBlendAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
			sceneColorBlendAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			passInfo.colorAttachmentCount = 1;
			passInfo.pColorAttachments = &sceneColorBlendAttachment;
		}
		else
		{
			passInfo.colorAttachmentCount = gbufferCount;
			passInfo.pColorAttachments = gbufferAttachments;
		}

		vkCmdBeginRendering(commandBuffer, &passInfo);

		VkViewport viewport = { 0, float(renderHeight), float(renderWidth), -float(renderHeight), 0, 1 };
		VkRect2D scissor = { { 0, 0 }, { renderWidth, renderHeight } };

		vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
		vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

		const bool useDebugWireframe = (globals.gbufferDebugMode == 1u);
		const VkCullModeFlags passCull = (postPass == 0 && !useDebugWireframe) ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_NONE;
		vkCmdSetCullMode(commandBuffer, passCull);
		vkCmdSetDepthBias(commandBuffer, postPass == 0 ? 0 : 16, 0, postPass == 0 ? 0 : 1);

		Globals passGlobals = globals;
		passGlobals.cullData.postPass = postPass;

		if (clusterSubmit)
		{
			const VkPipeline clusterPl = alphaBlendToSceneColor ? clusterTransparencyBlendPipeline
			                             : postPass >= 1        ? (useDebugWireframe ? clusterpostWirePipeline : clusterpostPipeline)
			                                                    : (useDebugWireframe ? clusterWirePipeline : clusterPipeline);
			const Program& clusterProg = alphaBlendToSceneColor ? transparencyBlendClusterProgram : clusterProgram;
			vkCmdBindPipeline(commandBuffer, clusterProg.bindPoint, clusterPl);

			DescriptorInfo pyramidDesc(depthSampler, depthPyramid->imageView, VK_IMAGE_LAYOUT_GENERAL);
			DescriptorInfo descriptors[] = { dcb.buffer, db.buffer, mb.buffer, mlb.buffer, mdb.buffer, vb.buffer, mvb.buffer, pyramidDesc, cib.buffer, textureSampler, mtb.buffer };

#if defined(WIN32)
			if (pushDescriptorSupported)
			{
				vkCmdPushDescriptorSetWithTemplate(commandBuffer, clusterProg.updateTemplate, clusterProg.layout, 0, descriptors);
				vkCmdBindDescriptorSets(commandBuffer, clusterProg.bindPoint, clusterProg.layout, 1, 1, &scene->textureSet.second, 0, nullptr);
			}
			else
#endif
			{
				vkUpdateDescriptorSetWithTemplateKHR(device, clusterSets[frameOffset][descriptorSetIndex], clusterProg.updateTemplate, descriptors);
				vkCmdBindDescriptorSets(commandBuffer, clusterProg.bindPoint, clusterProg.layout, 0, 1, &clusterSets[frameOffset][descriptorSetIndex], 0, nullptr);
				vkCmdBindDescriptorSets(commandBuffer, clusterProg.bindPoint, clusterProg.layout, 1, 1, &scene->textureSet.second, 0, nullptr);
			}

			vkCmdPushConstants(commandBuffer, clusterProg.layout, clusterProg.pushConstantStages, 0, sizeof(globals), &passGlobals);
			vkCmdDrawMeshTasksIndirectEXT(commandBuffer, ccb.buffer, 4, 1, 0);
		}
		else if (taskSubmit)
		{
			const VkPipeline taskPl = alphaBlendToSceneColor ? meshtaskTransparencyBlendPipeline
			                          : postPass >= 1        ? (useDebugWireframe ? meshtaskpostWirePipeline : meshtaskpostPipeline)
			                          : late                 ? (useDebugWireframe ? meshtasklateWirePipeline : meshtasklatePipeline)
			                                                 : (useDebugWireframe ? meshtaskWirePipeline : meshtaskPipeline);
			const Program& taskProg = alphaBlendToSceneColor ? transparencyBlendMeshtaskProgram : meshtaskProgram;
			vkCmdBindPipeline(commandBuffer, taskProg.bindPoint, taskPl);

			DescriptorInfo pyramidDesc(depthSampler, depthPyramid->imageView, VK_IMAGE_LAYOUT_GENERAL);
			DescriptorInfo descriptors[] = { dcb.buffer, db.buffer, mb.buffer, mlb.buffer, mdb.buffer, vb.buffer, mvb.buffer, pyramidDesc, cib.buffer, textureSampler, mtb.buffer };

#if defined(WIN32)
			if (pushDescriptorSupported)
			{
				vkCmdPushDescriptorSetWithTemplate(commandBuffer, taskProg.updateTemplate, taskProg.layout, 0, descriptors);
			}
			else
#endif
			{
				vkUpdateDescriptorSetWithTemplateKHR(device, meshtaskSets[frameOffset][descriptorSetIndex], taskProg.updateTemplate, descriptors);
				vkCmdBindDescriptorSets(commandBuffer, taskProg.bindPoint, taskProg.layout, 0, 1, &meshtaskSets[frameOffset][descriptorSetIndex], 0, nullptr);
			}

			vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, taskProg.layout, 1, 1, &scene->textureSet.second, 0, nullptr);

			vkCmdPushConstants(commandBuffer, taskProg.layout, taskProg.pushConstantStages, 0, sizeof(globals), &passGlobals);

			vkCmdDrawMeshTasksIndirectEXT(commandBuffer, dccb.buffer, 4, 1, 0);
		}
		else
		{
			const VkPipeline meshPl = alphaBlendToSceneColor ? meshTransparencyBlendPipeline
			                          : postPass >= 1        ? (useDebugWireframe ? meshpostWirePipeline : meshpostPipeline)
			                                                 : (useDebugWireframe ? meshWirePipeline : meshPipeline);
			const Program& meshProg = alphaBlendToSceneColor ? transparencyBlendMeshProgram : meshProgram;
			vkCmdBindPipeline(commandBuffer, meshProg.bindPoint, meshPl);

			DescriptorInfo descriptors[] = { dcb.buffer, db.buffer, vb.buffer, DescriptorInfo(), DescriptorInfo(), DescriptorInfo(), DescriptorInfo(), DescriptorInfo(), DescriptorInfo(), textureSampler, mtb.buffer };

#if defined(WIN32)
			if (pushDescriptorSupported)
			{
				vkCmdPushDescriptorSetWithTemplate(commandBuffer, meshProg.updateTemplate, meshProg.layout, 0, descriptors);
				vkCmdBindDescriptorSets(commandBuffer, meshProg.bindPoint, meshProg.layout, 1, 1, &scene->textureSet.second, 0, nullptr);
			}
			else
#endif
			{
				vkCmdBindDescriptorSets(commandBuffer, meshProg.bindPoint, meshProg.layout, 0, 1, &meshSets[frameOffset][descriptorSetIndex], 0, nullptr);
				vkCmdBindDescriptorSets(commandBuffer, meshProg.bindPoint, meshProg.layout, 1, 1, &scene->textureSet.second, 0, nullptr);
				vkUpdateDescriptorSetWithTemplateKHR(device, meshSets[frameOffset][descriptorSetIndex], meshProg.updateTemplate, descriptors);
			}

			vkCmdBindIndexBuffer(commandBuffer, ib.buffer, 0, VK_INDEX_TYPE_UINT32);

			vkCmdPushConstants(commandBuffer, meshProg.layout, meshProg.pushConstantStages, 0, sizeof(globals), &passGlobals);
			vkCmdDrawIndexedIndirectCount(commandBuffer, dcb.buffer, offsetof(MeshDrawCommand, indirect), dccb.buffer, 0, uint32_t(scene->draws.size()), sizeof(MeshDrawCommand));
		}

		vkCmdEndRendering(commandBuffer);
#if defined(WIN32)
		vkCmdEndQuery(commandBuffer, queryPoolPipeline, query);
#endif

		vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, queryPoolTimestamp, timestamp + 1);
	};

	auto pyramid = [&](uint32_t timestamp)
	{
		Image* depthPyramid = resourceManager.GetTexture(depthPyramidHandle);
		assert(depthPyramid);

		vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, queryPoolTimestamp, timestamp + 0);

		VkImageMemoryBarrier2 pyramidWriteBarrier = imageBarrier(depthPyramid->image,
		    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL,
		    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL);
		pipelineBarrier(commandBuffer, 0, 0, nullptr, 1, &pyramidWriteBarrier);
		vkCmdBindPipeline(commandBuffer, depthreduceProgram.bindPoint, depthreducePipeline);

		for (uint32_t i = 0; i < depthPyramidLevels; ++i)
		{
			VkImageView sourceView = (i == 0) ? depthTarget->imageView : depthPyramidMips[i - 1];
			VkImageLayout sourceLayout = (i == 0) ? VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_GENERAL;
			DescriptorInfo descriptors[] = {
				{ depthPyramidMips[i], VK_IMAGE_LAYOUT_GENERAL },
				{ depthSampler, sourceView, sourceLayout }
			};

			uint32_t levelWidth = std::max(1u, depthPyramidWidth >> i);
			uint32_t levelHeight = std::max(1u, depthPyramidHeight >> i);

			vec4 reduceData = vec4(levelWidth, levelHeight, 0.f, 0.f);
			if (pushDescriptorSupported)
			{
				dispatch(commandBuffer, depthreduceProgram, levelWidth, levelHeight, reduceData, descriptors);
			}
			else
			{
				VkDescriptorSet depthReduceSet = depthreduceProgram.descriptorSets[frameOffset];
				if (depthReduceSet == VK_NULL_HANDLE)
				{
					LOGE("Depth-reduce descriptor set is null: frame=%u level=%u programSet=%p",
					    uint32_t(frameOffset), i, depthreduceProgram.descriptorSets[frameOffset]);
					continue;
				}

				VkDescriptorImageInfo outImageInfo = {};
				outImageInfo.sampler = VK_NULL_HANDLE;
				outImageInfo.imageView = depthPyramidMips[i];
				outImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

				VkDescriptorImageInfo inImageInfo = {};
				inImageInfo.sampler = depthSampler;
				inImageInfo.imageView = sourceView;
				inImageInfo.imageLayout = sourceLayout;

				VkWriteDescriptorSet writes[2] = {};
				writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				writes[0].dstSet = depthReduceSet;
				writes[0].dstBinding = 0;
				writes[0].descriptorCount = 1;
				writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
				writes[0].pImageInfo = &outImageInfo;

				writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				writes[1].dstSet = depthReduceSet;
				writes[1].dstBinding = 1;
				writes[1].descriptorCount = 1;
				writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				writes[1].pImageInfo = &inImageInfo;

				vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);
				vkCmdBindDescriptorSets(commandBuffer, depthreduceProgram.bindPoint, depthreduceProgram.layout, 0, 1, &depthReduceSet, 0, nullptr);
				vkCmdPushConstants(commandBuffer, depthreduceProgram.layout, depthreduceProgram.pushConstantStages, 0, sizeof(reduceData), &reduceData);
				vkCmdDispatch(commandBuffer, getGroupCount(levelWidth, depthreduceProgram.localSizeX), getGroupCount(levelHeight, depthreduceProgram.localSizeY), 1);
			}
			VkImageMemoryBarrier2 reduceBarrier = imageBarrier(depthPyramid->image,
			    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL,
			    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL,
			    VK_IMAGE_ASPECT_COLOR_BIT, i, 1);
			pipelineBarrier(commandBuffer, 0, 0, nullptr, 1, &reduceBarrier);
		}

		vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, queryPoolTimestamp, timestamp + 1);
	};

	VkImageMemoryBarrier2 renderBeginBarriers[gbufferCount + 1] = {
		imageBarrier(depthTarget->image,
		    VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, VK_IMAGE_LAYOUT_UNDEFINED,
		    VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
		    VK_IMAGE_ASPECT_DEPTH_BIT),
	};

	for (uint32_t i = 0; i < gbufferCount; ++i)
		renderBeginBarriers[i + 1] = imageBarrier(gbufferTargets[i]->image,
		    VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, VK_IMAGE_LAYOUT_UNDEFINED,
		    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);

	pipelineBarrier(commandBuffer, VK_DEPENDENCY_BY_REGION_BIT, 0, nullptr, COUNTOF(renderBeginBarriers), renderBeginBarriers);

#if defined(WIN32)
	vkCmdResetQueryPool(commandBuffer, queryPoolPipeline, 0, 4);
#endif

	std::vector<VkClearColorValue> clearColors(gbufferCount);
	clearColors[0] = { 135.f / 255.f, 206.f / 255.f, 250.f / 255.f, 15.f / 255.f };
	clearColors[1] = { 0.f, 0.f, 0.f, 0.f };
	clearColors[2].uint32[0] = 0xffffffffu;
	clearColors[2].uint32[1] = 0;
	clearColors[2].uint32[2] = 0;
	clearColors[2].uint32[3] = 0;
	VkClearDepthStencilValue depthClear = { 0.f, 0 };

	RenderGraph rg;

	rg.addPass("GBuffer Early", [&](RGPassBuilder& builder)
	    {
			builder.readTextureFromPreviousFrame(depthPyramidHandle);  // Use previous frame's pyramid for culling
			for (uint32_t i = 0; i < gbufferCount; ++i)
				builder.writeTexture(gbufferTargetHandles[i], { RGLoadOp::Clear, RGStoreOp::Store });
			builder.writeTexture(depthTargetHandle, { RGLoadOp::Clear, RGStoreOp::Store }); }, [&](RGPassContext&)
	    {
			cull(taskSubmit ? taskcullPipeline : drawcullPipeline, 2, "early cull", /* late= */ false);
			render(/* late= */ false, clearColors, depthClear, 0, 4, "early render"); });

	rg.addPass("Depth Pyramid", [&](RGPassBuilder& builder)
	    {
			builder.readTexture(depthTargetHandle, ResourceState::DepthStencilRead);
			builder.writeTexture(depthPyramidHandle, ResourceState::ShaderWrite, { RGLoadOp::DontCare, RGStoreOp::Store }); }, [&](RGPassContext&)
	    { pyramid(6); });

	rg.addPass("GBuffer Opaque", [&](RGPassBuilder& builder)
	    {
			builder.readTexture(depthPyramidHandle, ResourceState::ShaderRead);
			for (uint32_t i = 0; i < gbufferCount; ++i)
				builder.writeTexture(gbufferTargetHandles[i], ResourceState::ColorAttachment, { RGLoadOp::Load, RGStoreOp::Store });
			builder.writeTexture(depthTargetHandle, ResourceState::DepthStencilWrite, { RGLoadOp::Load, RGStoreOp::Store }); }, [&](RGPassContext&)
	    {
			cull(taskSubmit ? taskculllatePipeline : drawculllatePipeline, 8, "opaque cull", /* late= */ true);
			render(/* late= */ true, clearColors, depthClear, 1, 10, "opaque render"); });

	rg.addPass("Shadow Pass", [&](RGPassBuilder& builder)
	    {
			builder.readTexture(gbufferTargetHandles[0], ResourceState::ShaderRead);
			builder.readTexture(gbufferTargetHandles[1], ResourceState::ShaderRead);
			builder.readTexture(gbufferTargetHandles[2], ResourceState::ShaderRead);
			builder.readTexture(depthTargetHandle, ResourceState::ShaderRead);
			builder.writeTexture(shadowTargetHandle, ResourceState::ShaderWrite, { RGLoadOp::DontCare, RGStoreOp::Store });
			builder.writeExternalTexture("FinalColor", ResourceState::ShaderWrite, { RGLoadOp::DontCare, RGStoreOp::Store });

			if (shadowblurEnabled)
			{
				builder.writeTexture(shadowblurTargetHandle, ResourceState::ShaderWrite, { RGLoadOp::DontCare, RGStoreOp::Store });
			} }, [&](RGPassContext& ctx)
	    {
			Image* shadowTarget = resourceManager.GetTexture(shadowTargetHandle);
			assert(shadowTarget);

			if (raytracingSupported && shadowEnabled)
			{
				Image* shadowblurTarget = nullptr;
				if (shadowblurTargetHandle.IsValid())
				{
					shadowblurTarget = resourceManager.GetTexture(shadowblurTargetHandle);
					assert(shadowblurTarget);
				}

				uint32_t timestamp = 16;

				// checkerboard rendering: we dispatch half as many columns and xform them to fill the screen
				int shadowWidthCB = shadowCheckerboard ? (renderWidth + 1) / 2 : renderWidth;
				int shadowCheckerboardF = shadowCheckerboard ? 1 : 0;

				vkCmdWriteTimestamp(ctx.commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, queryPoolTimestamp, timestamp + 0);
				{
					vkCmdBindPipeline(ctx.commandBuffer, shadowProgram.bindPoint, shadowQuality == 0 ? shadowlqPipeline : shadowhqPipeline);
					DescriptorInfo descriptors[] = { { shadowTarget->imageView, VK_IMAGE_LAYOUT_GENERAL }, { readSampler, depthTarget->imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }, tlas, db.buffer, mb.buffer, mtb.buffer, vb.buffer, ib.buffer, textureSampler };

					ShadowData shadowData = {};
					shadowData.sunDirection = scene->sunDirection;
					shadowData.sunJitter = shadowblurEnabled ? 1e-2f : 0;
					shadowData.inverseViewProjection = inverseViewProjection;
					shadowData.imageSize = vec2(float(renderWidth), float(renderHeight));
					shadowData.checkerboard = shadowCheckerboardF;

					if (pushDescriptorSupported)
					{
						vkCmdBindDescriptorSets(ctx.commandBuffer, shadowProgram.bindPoint, shadowProgram.layout, 1, 1, &scene->textureSet.second, 0, nullptr);
						dispatch(ctx.commandBuffer, shadowProgram, shadowWidthCB, renderHeight, shadowData, descriptors);
					}
					else
					{
						vkUpdateDescriptorSetWithTemplateKHR(device, shadowProgram.descriptorSets[frameOffset], shadowProgram.updateTemplate, descriptors);
						vkCmdBindDescriptorSets(ctx.commandBuffer, shadowProgram.bindPoint, shadowProgram.layout, 0, 1, &shadowProgram.descriptorSets[frameOffset], 0, nullptr);
						vkCmdBindDescriptorSets(ctx.commandBuffer, shadowProgram.bindPoint, shadowProgram.layout, 1, 1, &scene->textureSet.second, 0, nullptr);
						vkCmdPushConstants(ctx.commandBuffer, shadowProgram.layout, shadowProgram.pushConstantStages, 0, sizeof(shadowData), &shadowData);
						vkCmdDispatch(ctx.commandBuffer, getGroupCount(shadowWidthCB, shadowProgram.localSizeX), getGroupCount(renderHeight, shadowProgram.localSizeY), 1);
					}
				}

				vkCmdWriteTimestamp(ctx.commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, queryPoolTimestamp, timestamp + 1);

				if (shadowCheckerboard)
				{
					VkImageMemoryBarrier2 fillBarrier = imageBarrier(shadowTarget->image,
						VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL,
						VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL);

					pipelineBarrier(ctx.commandBuffer, VK_DEPENDENCY_BY_REGION_BIT, 0, nullptr, 1, &fillBarrier);

					vkCmdBindPipeline(ctx.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, shadowfillPipeline);

					DescriptorInfo descriptors[] = { { shadowTarget->imageView, VK_IMAGE_LAYOUT_GENERAL }, { readSampler, depthTarget->imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } };
					vec4 fillData = vec4(float(renderWidth), float(renderHeight), 0, 0);

					dispatch(ctx.commandBuffer, shadowfillProgram, shadowWidthCB, renderHeight, fillData, descriptors);
				}

				for (int pass = 0; pass < (shadowblurEnabled ? 2 : 0); ++pass)
				{
					assert(shadowblurTarget);
					const Image& blurFrom = pass == 0 ? *shadowTarget : *shadowblurTarget;
					const Image& blurTo = pass == 0 ? *shadowblurTarget : *shadowTarget;

					VkImageMemoryBarrier2 blurBarriers[] = {
						imageBarrier(blurFrom.image,
						    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL,
						    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL),
						imageBarrier(blurTo.image,
						    pass == 0 ? 0 : VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, pass == 0 ? 0 : VK_ACCESS_SHADER_READ_BIT, pass == 0 ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_GENERAL,
						    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL),
					};

					pipelineBarrier(ctx.commandBuffer, VK_DEPENDENCY_BY_REGION_BIT, 0, nullptr, COUNTOF(blurBarriers), blurBarriers);
					vkCmdBindPipeline(ctx.commandBuffer, shadowblurProgram.bindPoint, shadowblurPipeline);
					DescriptorInfo descriptors[] = { { blurTo.imageView, VK_IMAGE_LAYOUT_GENERAL }, { readSampler, blurFrom.imageView, VK_IMAGE_LAYOUT_GENERAL }, { readSampler, depthTarget->imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } };
					vec4 blurData = vec4(float(renderWidth), float(renderHeight), pass == 0 ? 1 : 0, scene->camera.znear);

					if (pushDescriptorSupported)
					{
						dispatch(ctx.commandBuffer, shadowblurProgram, renderWidth, renderHeight, blurData, descriptors);
					}
					else
					{
						vkUpdateDescriptorSetWithTemplateKHR(device, shadowblurSets[frameOffset][pass], shadowblurProgram.updateTemplate, descriptors);
						vkCmdBindDescriptorSets(ctx.commandBuffer, shadowblurProgram.bindPoint, shadowblurProgram.layout, 0, 1, &shadowblurSets[frameOffset][pass], 0, nullptr);
						vkCmdPushConstants(ctx.commandBuffer, shadowblurProgram.layout, shadowblurProgram.pushConstantStages, 0, sizeof(blurData), &blurData);
						vkCmdDispatch(ctx.commandBuffer, getGroupCount(renderWidth, shadowblurProgram.localSizeX), getGroupCount(renderHeight, shadowblurProgram.localSizeY), 1);
					}
				}

				VkImageMemoryBarrier2 postblurBarrier =
				    imageBarrier(shadowTarget->image,
				        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL,
				        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL);

				pipelineBarrier(ctx.commandBuffer, VK_DEPENDENCY_BY_REGION_BIT, 0, nullptr, 1, &postblurBarrier);

				vkCmdWriteTimestamp(ctx.commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, queryPoolTimestamp, timestamp + 2);
			}
			else
			{
				uint32_t timestamp = 16; // Placeholder until shadow pass is wired to the real profiler.
				vkCmdWriteTimestamp(ctx.commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, queryPoolTimestamp, timestamp + 0);
				vkCmdWriteTimestamp(ctx.commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, queryPoolTimestamp, timestamp + 1);
				vkCmdWriteTimestamp(ctx.commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, queryPoolTimestamp, timestamp + 2);
			} });

	const auto lightingPassSetup = [&](RGPassBuilder& builder)
	{
		builder.readTexture(gbufferTargetHandles[0], ResourceState::ShaderRead);
		builder.readTexture(gbufferTargetHandles[1], ResourceState::ShaderRead);
		builder.readTexture(gbufferTargetHandles[2], ResourceState::ShaderRead);
		builder.readTexture(depthTargetHandle, ResourceState::ShaderRead);
		builder.readTexture(shadowTargetHandle, ResourceState::ShaderRead);
		builder.writeTexture(sceneColorHDRHandle, ResourceState::ShaderWrite, { RGLoadOp::DontCare, RGStoreOp::Store });
	};

	const auto lightingPassExecute = [&](RGPassContext& ctx)
	{
		Image* shadowTarget = ctx.resourceManager->GetTexture(shadowTargetHandle);
		assert(shadowTarget);
		Image* sceneColorHDR = ctx.resourceManager->GetTexture(sceneColorHDRHandle);
		assert(sceneColorHDR);

		uint32_t timestamp = TS_ShadeBegin;

		vkCmdWriteTimestamp(ctx.commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, queryPoolTimestamp, timestamp + 0);

		vkCmdBindPipeline(ctx.commandBuffer, finalProgram.bindPoint, finalPipeline);

		DescriptorInfo descriptors[] = {
			{ sceneColorHDR->imageView, VK_IMAGE_LAYOUT_GENERAL },
			{ readSampler, gbufferTargets[0]->imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
			{ readSampler, gbufferTargets[1]->imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
			{ readSampler, depthTarget->imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
			{ readSampler, shadowTarget->imageView, VK_IMAGE_LAYOUT_GENERAL }
		};

		ShadeData shadeData = {};
		shadeData.cameraPosition = scene->camera.position;
		shadeData.sunDirection = scene->sunDirection;
		shadeData.shadowEnabled = shadowEnabled;
		shadeData.inverseViewProjection = inverseViewProjection;
		shadeData.imageSize = vec2(float(renderWidth), float(renderHeight));

		if (pushDescriptorSupported)
		{
			dispatch(ctx.commandBuffer, finalProgram, renderWidth, renderHeight, shadeData, descriptors);
		}
		else
		{
			vkUpdateDescriptorSetWithTemplateKHR(device, finalProgram.descriptorSets[frameOffset], finalProgram.updateTemplate, descriptors);
			vkCmdBindDescriptorSets(ctx.commandBuffer, finalProgram.bindPoint, finalProgram.layout, 0, 1, &finalProgram.descriptorSets[frameOffset], 0, nullptr);
			vkCmdPushConstants(ctx.commandBuffer, finalProgram.layout, finalProgram.pushConstantStages, 0, sizeof(shadeData), &shadeData);
			vkCmdDispatch(ctx.commandBuffer, getGroupCount(renderWidth, finalProgram.localSizeX), getGroupCount(renderHeight, finalProgram.localSizeY), 1);
		}

		vkCmdWriteTimestamp(ctx.commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, queryPoolTimestamp, timestamp + 1);
	};

	// Opaque-lit HDR must be ready before transparency so refraction can sample the background (see transmission resolve / TAA path).
	rg.addPass("Lighting Pass", lightingPassSetup, lightingPassExecute);

	if (screenSpaceRefractionEnabled)
	{
		rg.addPass("GBuffer Transparency", [&](RGPassBuilder& builder)
		    {
				// Render-graph ordering: without a resource edge, topo sort may run this pass before Lighting while both only
				// depend on opaque GBuffer; lighting must consume opaque-only GBuffer before we layer transparency into it.
				builder.readTexture(sceneColorHDRHandle, ResourceState::ShaderRead);
				builder.readTexture(depthPyramidHandle, ResourceState::ShaderRead);
				for (uint32_t i = 0; i < gbufferCount; ++i)
					builder.writeTexture(gbufferTargetHandles[i], ResourceState::ColorAttachment, { RGLoadOp::Load, RGStoreOp::Store });
				builder.writeTexture(depthTargetHandle, ResourceState::DepthStencilWrite, { RGLoadOp::Load, RGStoreOp::Store }); }, [&](RGPassContext&)
		    {
				if (scene->meshPostPasses >> 1)
				{
					cull(taskSubmit ? taskculllatePipeline : drawculllatePipeline, 12, "transparency cull", /* late= */ true, /* postPass= */ 1);
					render(/* late= */ true, clearColors, depthClear, 2, 14, "transparency render", /* postPass= */ 1);
				} });
	}
	else if (scene->meshPostPasses >> 1)
	{
		rg.addPass("Transparency Alpha Blend", [&](RGPassBuilder& builder)
		    {
				builder.readTexture(sceneColorHDRHandle, ResourceState::ColorAttachment);
				builder.writeTexture(sceneColorHDRHandle, ResourceState::ColorAttachment, { RGLoadOp::Load, RGStoreOp::Store });
				// Depth is a dynamic-rendering attachment (depth test); DepthStencilRead barriers to DEPTH_READ_ONLY_OPTIMAL,
				// which mismatches vkCmdBeginRendering depthAttachment (ATTACHMENT_OPTIMAL). Match GBuffer Transparency pass.
				builder.writeTexture(depthTargetHandle, ResourceState::DepthStencilWrite, { RGLoadOp::Load, RGStoreOp::Store }); }, [&](RGPassContext&)
		    {
				cull(taskSubmit ? taskculllatePipeline : drawculllatePipeline, 12, "transparency cull", /* late= */ true, /* postPass= */ 1);
				render(/* late= */ true, clearColors, depthClear, 2, 14, "transparency alpha blend", /* postPass= */ 1, /* alphaBlendToSceneColor= */ true); });
	}

	rg.addPass("Transmission Resolve", [&](RGPassBuilder& builder)
	    {
		    builder.readTexture(sceneColorHDRHandle, ResourceState::ShaderRead);
		    builder.readTexture(gbufferTargetHandles[0], ResourceState::ShaderRead);
		    builder.readTexture(gbufferTargetHandles[1], ResourceState::ShaderRead);
		    builder.readTexture(gbufferTargetHandles[2], ResourceState::ShaderRead);
		    builder.readTexture(depthTargetHandle, ResourceState::ShaderRead);
		    if (taaEnabled)
			    builder.writeTexture(sceneColorResolvedHandle, ResourceState::ShaderWrite, { RGLoadOp::DontCare, RGStoreOp::Store });
		    else
			    builder.writeExternalTexture("FinalColor", ResourceState::ShaderWrite, { RGLoadOp::DontCare, RGStoreOp::Store }); }, [&](RGPassContext& ctx)
	    {
		    Image* sceneColorTex = ctx.resourceManager->GetTexture(sceneColorHDRHandle);
		    assert(sceneColorTex);
		    Image* resolvedTarget = taaEnabled ? ctx.resourceManager->GetTexture(sceneColorResolvedHandle) : nullptr;
		    if (taaEnabled)
			    assert(resolvedTarget);
		    const VkImageView outView = taaEnabled ? resolvedTarget->imageView : finalOutputImageView;

		    vkCmdBindPipeline(ctx.commandBuffer, transmissionResolveProgram.bindPoint, transmissionResolvePipeline);

		    TransmissionResolveData resolveData = {};
		    resolveData.cameraPosition = scene->camera.position;
		    resolveData.inverseViewProjection = inverseViewProjection;
		    resolveData.imageSize = vec2(float(renderWidth), float(renderHeight));
		    resolveData.refractionWorldDistance = 0.08f;
		    resolveData.refractionEnabled = screenSpaceRefractionEnabled ? 1u : 0u;

		    DescriptorInfo descriptors[] = {
			    { outView, VK_IMAGE_LAYOUT_GENERAL },
			    { readSampler, sceneColorTex->imageView, VK_IMAGE_LAYOUT_GENERAL },
			    { readSampler, gbufferTargets[0]->imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
			    { readSampler, gbufferTargets[1]->imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
			    { readSampler, depthTarget->imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
			    { readSampler, gbufferTargets[2]->imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
			    mtb.buffer,
			    textureSampler,
		    };

		    if (pushDescriptorSupported)
		    {
			    vkCmdBindDescriptorSets(ctx.commandBuffer, transmissionResolveProgram.bindPoint, transmissionResolveProgram.layout, 1, 1, &scene->textureSet.second, 0, nullptr);
			    dispatch(ctx.commandBuffer, transmissionResolveProgram, renderWidth, renderHeight, resolveData, descriptors);
		    }
		    else
		    {
			    vkUpdateDescriptorSetWithTemplateKHR(device, transmissionResolveProgram.descriptorSets[frameOffset], transmissionResolveProgram.updateTemplate, descriptors);
			    vkCmdBindDescriptorSets(ctx.commandBuffer, transmissionResolveProgram.bindPoint, transmissionResolveProgram.layout, 0, 1, &transmissionResolveProgram.descriptorSets[frameOffset], 0, nullptr);
			    vkCmdBindDescriptorSets(ctx.commandBuffer, transmissionResolveProgram.bindPoint, transmissionResolveProgram.layout, 1, 1, &scene->textureSet.second, 0, nullptr);
			    vkCmdPushConstants(ctx.commandBuffer, transmissionResolveProgram.layout, transmissionResolveProgram.pushConstantStages, 0, sizeof(resolveData), &resolveData);
			    vkCmdDispatch(ctx.commandBuffer, getGroupCount(renderWidth, transmissionResolveProgram.localSizeX), getGroupCount(renderHeight, transmissionResolveProgram.localSizeY), 1);
		    } });

	if (taaEnabled)
	{
		rg.addPass("TAA", [&](RGPassBuilder& builder)
		    {
				const bool readA = (frameIndex % 2) == 0;
				RGTextureHandle historyRead = readA ? taaHistoryHandles[0] : taaHistoryHandles[1];
				RGTextureHandle historyWrite = readA ? taaHistoryHandles[1] : taaHistoryHandles[0];
				builder.readTexture(sceneColorResolvedHandle, ResourceState::ShaderRead);
				builder.readTextureFromPreviousFrame(historyRead);
				builder.writeTexture(historyWrite, ResourceState::ShaderWrite, { RGLoadOp::DontCare, RGStoreOp::Store });
				builder.writeExternalTexture("FinalColor", ResourceState::ShaderWrite, { RGLoadOp::DontCare, RGStoreOp::Store }); }, [&](RGPassContext& ctx)
		    {
				const uint32_t timestamp = TS_TaaBegin;
				vkCmdWriteTimestamp(ctx.commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, queryPoolTimestamp, timestamp + 0);

				const bool readA = (frameIndex % 2) == 0;
				Image* sceneColorResolved = ctx.resourceManager->GetTexture(sceneColorResolvedHandle);
				Image* taaRead = ctx.resourceManager->GetTexture(readA ? taaHistoryHandles[0] : taaHistoryHandles[1]);
				Image* taaWrite = ctx.resourceManager->GetTexture(readA ? taaHistoryHandles[1] : taaHistoryHandles[0]);
				assert(sceneColorResolved && taaRead && taaWrite);

				vkCmdBindPipeline(ctx.commandBuffer, taaProgram.bindPoint, taaPipeline);

				DescriptorInfo descriptors[] = {
					{ readSampler, sceneColorResolved->imageView, VK_IMAGE_LAYOUT_GENERAL },
					{ readSampler, taaRead->imageView, VK_IMAGE_LAYOUT_GENERAL },
					{ finalOutputImageView, VK_IMAGE_LAYOUT_GENERAL },
					{ taaWrite->imageView, VK_IMAGE_LAYOUT_GENERAL }
				};

				TaaData taaData = {};
				taaData.imageSize = vec2(float(renderWidth), float(renderHeight));
				taaData.historyValid = g_taaHistoryReady ? 1 : 0;
				taaData.blendAlpha = taaBlendAlpha;

				if (pushDescriptorSupported)
				{
					dispatch(ctx.commandBuffer, taaProgram, renderWidth, renderHeight, taaData, descriptors);
				}
				else
				{
					vkUpdateDescriptorSetWithTemplateKHR(device, taaProgram.descriptorSets[frameOffset], taaProgram.updateTemplate, descriptors);
					vkCmdBindDescriptorSets(ctx.commandBuffer, taaProgram.bindPoint, taaProgram.layout, 0, 1, &taaProgram.descriptorSets[frameOffset], 0, nullptr);
					vkCmdPushConstants(ctx.commandBuffer, taaProgram.layout, taaProgram.pushConstantStages, 0, sizeof(taaData), &taaData);
					vkCmdDispatch(ctx.commandBuffer, getGroupCount(renderWidth, taaProgram.localSizeX), getGroupCount(renderHeight, taaProgram.localSizeY), 1);
				}

				g_taaHistoryReady = true;
				vkCmdWriteTimestamp(ctx.commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, queryPoolTimestamp, timestamp + 1); });
	}

	RGPassContext rgContext{};
	PrepareRenderGraphPassContext(rgContext, commandBuffer, frameIndex, imageIndex);
	rg.execute(rgContext);

#if defined(WIN32)
	if (editorViewportMode && finalOutputImage && meshSelectionOutlineEditorPipeline && scene->uiEnableSelectionOutline
	    && scene->uiSelectedGltfNode.has_value() && outlineDcb.buffer && outlineDccb.buffer)
	{
		thread_local std::vector<uint32_t> s_subtreeDraws;
		CollectDrawIndicesInNodeSubtree(*scene, *scene->uiSelectedGltfNode, s_subtreeDraws);
		const uint32_t maxCmd = std::max(1u, uint32_t(scene->draws.size()));
		if (!s_subtreeDraws.empty())
		{
			auto* const cmds = reinterpret_cast<MeshDrawCommand*>(outlineDcb.data);
			const uint32_t outlineCount = FillEditorOutlineMeshDrawCommands(*scene, s_subtreeDraws, cmds, maxCmd);
			if (outlineCount > 0u)
			{
				*reinterpret_cast<uint32_t*>(outlineDccb.data) = outlineCount;

				VkImageMemoryBarrier2 toColorAttach = imageBarrier(finalOutputImage->image,
				    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL,
				    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
				    VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);

				pipelineBarrier(commandBuffer, VK_DEPENDENCY_BY_REGION_BIT, 0, nullptr, 1, &toColorAttach);

				VkRenderingAttachmentInfo colorAttach{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
				colorAttach.imageView = finalOutputImageView;
				colorAttach.imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
				colorAttach.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
				colorAttach.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

				VkRenderingInfo ri{ VK_STRUCTURE_TYPE_RENDERING_INFO };
				ri.renderArea.offset = { 0, 0 };
				ri.renderArea.extent = { renderWidth, renderHeight };
				ri.layerCount = 1;
				ri.colorAttachmentCount = 1;
				ri.pColorAttachments = &colorAttach;
				ri.pDepthAttachment = nullptr;

				vkCmdBeginRendering(commandBuffer, &ri);

				VkViewport viewport{ 0.f, float(renderHeight), float(renderWidth), -float(renderHeight), 0.f, 1.f };
				VkRect2D scissor{ { 0, 0 }, { renderWidth, renderHeight } };
				vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
				vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
				vkCmdSetCullMode(commandBuffer, VK_CULL_MODE_FRONT_BIT);
				vkCmdSetDepthBias(commandBuffer, 0.f, 0.f, 0.f);

				Globals outlineGlobals = globals;
				outlineGlobals.selectionOutlinePass = 1u;
				outlineGlobals.selectionOutlineWidth = 0.02f;

				vkCmdBindPipeline(commandBuffer, meshSelectionOutlineProgram.bindPoint, meshSelectionOutlineEditorPipeline);
				DescriptorInfo outlineDescriptors[] = { outlineDcb.buffer, db.buffer, vb.buffer, DescriptorInfo(), DescriptorInfo(), DescriptorInfo(), DescriptorInfo(), DescriptorInfo(), DescriptorInfo(), textureSampler, mtb.buffer };
				const size_t dsi = 2;
				if (pushDescriptorSupported)
				{
					vkCmdPushDescriptorSetWithTemplate(commandBuffer, meshSelectionOutlineProgram.updateTemplate, meshSelectionOutlineProgram.layout, 0, outlineDescriptors);
					vkCmdBindDescriptorSets(commandBuffer, meshSelectionOutlineProgram.bindPoint, meshSelectionOutlineProgram.layout, 1, 1, &scene->textureSet.second, 0, nullptr);
				}
				else
				{
					vkUpdateDescriptorSetWithTemplateKHR(device, meshSets[frameOffset][dsi], meshSelectionOutlineProgram.updateTemplate, outlineDescriptors);
					vkCmdBindDescriptorSets(commandBuffer, meshSelectionOutlineProgram.bindPoint, meshSelectionOutlineProgram.layout, 0, 1, &meshSets[frameOffset][dsi], 0, nullptr);
					vkCmdBindDescriptorSets(commandBuffer, meshSelectionOutlineProgram.bindPoint, meshSelectionOutlineProgram.layout, 1, 1, &scene->textureSet.second, 0, nullptr);
				}

				vkCmdBindIndexBuffer(commandBuffer, ib.buffer, 0, VK_INDEX_TYPE_UINT32);
				vkCmdPushConstants(commandBuffer, meshSelectionOutlineProgram.layout, meshSelectionOutlineProgram.pushConstantStages, 0, sizeof(outlineGlobals), &outlineGlobals);
				vkCmdDrawIndexedIndirectCount(commandBuffer, outlineDcb.buffer, offsetof(MeshDrawCommand, indirect), outlineDccb.buffer, 0, maxCmd, sizeof(MeshDrawCommand));

				vkCmdEndRendering(commandBuffer);

				VkImageMemoryBarrier2 toGeneral = imageBarrier(finalOutputImage->image,
				    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
				    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL);

				pipelineBarrier(commandBuffer, VK_DEPENDENCY_BY_REGION_BIT, 0, nullptr, 1, &toGeneral);
			}
		}
	}

	if (editorViewportMode && finalOutputImage && editorAabbLinePipeline && editorAabbLineProgram.layout && editorAabbLineVb.buffer && depthTarget
	    && scene->uiShowSelectedSubtreeAabb && scene->uiSelectedGltfNode.has_value() && !scene->transformNodes.empty()
	    && *scene->uiSelectedGltfNode < scene->transformNodes.size() && editorAabbLineVb.data)
	{
		thread_local std::vector<uint32_t> s_aabbDraws;
		CollectDrawIndicesInNodeSubtree(*scene, *scene->uiSelectedGltfNode, s_aabbDraws);
		glm::vec3 mn{}, mx{};
		if (!s_aabbDraws.empty() && UnionWorldAabbForDraws(*scene, s_aabbDraws, mn, mx))
		{
			const float diag = glm::length(mx - mn);
			if (diag > 1e-8f)
			{
				const vec3 c[8] = {
					vec3(mn.x, mn.y, mn.z),
					vec3(mx.x, mn.y, mn.z),
					vec3(mn.x, mx.y, mn.z),
					vec3(mx.x, mx.y, mn.z),
					vec3(mn.x, mn.y, mx.z),
					vec3(mx.x, mn.y, mx.z),
					vec3(mn.x, mx.y, mx.z),
					vec3(mx.x, mx.y, mx.z),
				};
				vec3 verts[24];
				uint32_t k = 0;
				auto edge = [&](int a, int b) {
					verts[k++] = c[a];
					verts[k++] = c[b];
				};
				edge(0, 1);
				edge(1, 3);
				edge(3, 2);
				edge(2, 0);
				edge(4, 5);
				edge(5, 7);
				edge(7, 6);
				edge(6, 4);
				edge(0, 4);
				edge(1, 5);
				edge(2, 6);
				edge(3, 7);

				memcpy(editorAabbLineVb.data, verts, sizeof(verts));

				VkBufferMemoryBarrier2 vbBarrier = bufferBarrier(editorAabbLineVb.buffer,
				    VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_WRITE_BIT,
				    VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT);

				VkImageMemoryBarrier2 toAttach[2];
				toAttach[0] = imageBarrier(finalOutputImage->image,
				    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				    VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL,
				    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
				    VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);
				toAttach[1] = imageBarrier(depthTarget->image,
				    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
				    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				    VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
				    VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_DEPTH_BIT);

				pipelineBarrier(commandBuffer, VK_DEPENDENCY_BY_REGION_BIT, 1, &vbBarrier, 2, toAttach);

				VkRenderingAttachmentInfo colorAttach{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
				colorAttach.imageView = finalOutputImageView;
				colorAttach.imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
				colorAttach.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
				colorAttach.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

				VkRenderingAttachmentInfo depthAttach{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
				depthAttach.imageView = depthTarget->imageView;
				depthAttach.imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
				depthAttach.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
				depthAttach.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

				VkRenderingInfo aabbRi{ VK_STRUCTURE_TYPE_RENDERING_INFO };
				aabbRi.renderArea.offset = { 0, 0 };
				aabbRi.renderArea.extent = { renderWidth, renderHeight };
				aabbRi.layerCount = 1;
				aabbRi.colorAttachmentCount = 1;
				aabbRi.pColorAttachments = &colorAttach;
				aabbRi.pDepthAttachment = &depthAttach;

				vkCmdBeginRendering(commandBuffer, &aabbRi);

				VkViewport viewport{ 0.f, float(renderHeight), float(renderWidth), -float(renderHeight), 0.f, 1.f };
				VkRect2D scissor{ { 0, 0 }, { renderWidth, renderHeight } };
				vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
				vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
				vkCmdSetCullMode(commandBuffer, VK_CULL_MODE_NONE);
				vkCmdSetDepthBias(commandBuffer, 0.f, 0.f, 0.f);

				EditorAabbLinePush aabbPush{};
				aabbPush.view = view;
				aabbPush.projection = projectionJittered;
				aabbPush.lineColor = vec4(1.f, 235.f / 255.f, 60.f / 255.f, 1.f);

				vkCmdBindPipeline(commandBuffer, editorAabbLineProgram.bindPoint, editorAabbLinePipeline);
				VkBuffer vb = editorAabbLineVb.buffer;
				VkDeviceSize vbOffset = 0;
				vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vb, &vbOffset);
				vkCmdPushConstants(commandBuffer, editorAabbLineProgram.layout, editorAabbLineProgram.pushConstantStages, 0, sizeof(aabbPush), &aabbPush);
				vkCmdDraw(commandBuffer, 24, 1, 0, 0);

				vkCmdEndRendering(commandBuffer);

				VkImageMemoryBarrier2 afterAabb[2];
				afterAabb[0] = imageBarrier(finalOutputImage->image,
				    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
				    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL);
				afterAabb[1] = imageBarrier(depthTarget->image,
				    VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
				    VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
				    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
				    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_DEPTH_BIT);

				pipelineBarrier(commandBuffer, VK_DEPENDENCY_BY_REGION_BIT, 0, nullptr, 2, afterAabb);
			}
		}
	}
#endif

	static double cullGPUTime = 0.0;
	static double pyramidGPUTime = 0.0;
	static double culllateGPUTime = 0.0;
	static double cullpostGPUTime = 0.0;
	static double renderGPUTime = 0.0;
	static double renderlateGPUTime = 0.0;
	static double renderpostGPUTime = 0.0;
	static double shadowsGPUTime = 0.0;
	static double shadowblurGPUTime = 0.0;
	static double shadeGPUTime = 0.0;
	static double taaGPUTime = 0.0;
	static double tlasGPUTime = 0.0;

	static double frameCPUAvg = 0.0;
	static double frameGPUAvg = 0.0;

#if defined(WIN32)
	uint64_t triangleCount = pipelineResults[0] + pipelineResults[1] + pipelineResults[2];
#elif defined(__ANDROID__)
	uint64_t triangleCount = 0;
#endif

	cullGPUTime = getTimestampDurationMs(timestampResults, TS_CullBegin, TS_CullEnd, props.limits.timestampPeriod);
	renderGPUTime = getTimestampDurationMs(timestampResults, TS_RenderBegin, TS_RenderEnd, props.limits.timestampPeriod);
	pyramidGPUTime = getTimestampDurationMs(timestampResults, TS_PyramidBegin, TS_PyramidEnd, props.limits.timestampPeriod);
	culllateGPUTime = getTimestampDurationMs(timestampResults, TS_CullLateBegin, TS_CullLateEnd, props.limits.timestampPeriod);
	renderlateGPUTime = getTimestampDurationMs(timestampResults, TS_RenderLateBegin, TS_RenderLateEnd, props.limits.timestampPeriod);
	cullpostGPUTime = getTimestampDurationMs(timestampResults, TS_CullPostBegin, TS_CullPostEnd, props.limits.timestampPeriod);
	renderpostGPUTime = getTimestampDurationMs(timestampResults, TS_RenderPostBegin, TS_RenderPostEnd, props.limits.timestampPeriod);
	shadowsGPUTime = getTimestampDurationMs(timestampResults, TS_ShadowBegin, TS_ShadowEnd, props.limits.timestampPeriod);
	shadowblurGPUTime = getTimestampDurationMs(timestampResults, TS_ShadowEnd, TS_ShadowBlurEnd, props.limits.timestampPeriod);
	shadeGPUTime = getTimestampDurationMs(timestampResults, TS_ShadeBegin, TS_ShadeEnd, props.limits.timestampPeriod);
	taaGPUTime = taaEnabled ? getTimestampDurationMs(timestampResults, TS_TaaBegin, TS_TaaEnd, props.limits.timestampPeriod) : 0.0;
	tlasGPUTime = getTimestampDurationMs(timestampResults, TS_TlasBegin, TS_TlasEnd, props.limits.timestampPeriod);

	// Skip burning debug text into the render target shown in the editor viewport (ImGui image).
	if (debugGuiMode % 3 && !editorViewportMode)
	{
		auto debugtext = [&](int line, uint32_t color, const char* format, ...)
		{
			TextData textData = {};
			textData.offsetX = 1;
			textData.offsetY = line + 2;
			textData.scale = 2;
			textData.color = color;

			va_list args;
			va_start(args, format);
			vsnprintf(textData.data, sizeof(textData.data), format, args);
			va_end(args);

			vkCmdPushConstants(commandBuffer, debugtextProgram.layout, debugtextProgram.pushConstantStages, 0, sizeof(textData), &textData);
			vkCmdDispatch(commandBuffer, strlen(textData.data), 1, 1);
		};

		VkImageMemoryBarrier2 textBarrier =
		    imageBarrier(finalOutputVkImage,
		        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL,
		        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL);

		pipelineBarrier(commandBuffer, VK_DEPENDENCY_BY_REGION_BIT, 0, nullptr, 1, &textBarrier);

		vkCmdBindPipeline(commandBuffer, debugtextProgram.bindPoint, debugtextPipeline);

		DescriptorInfo descriptors[] = { { finalOutputImageView, VK_IMAGE_LAYOUT_GENERAL } };

#if defined(WIN32)
		if (pushDescriptorSupported)
		{
			vkCmdPushDescriptorSetWithTemplate(commandBuffer, debugtextProgram.updateTemplate, debugtextProgram.layout, 0, descriptors);
		}
		else
#endif
		{
			vkUpdateDescriptorSetWithTemplateKHR(device, debugtextProgram.descriptorSets[frameOffset], debugtextProgram.updateTemplate, descriptors);
			vkCmdBindDescriptorSets(commandBuffer, debugtextProgram.bindPoint, debugtextProgram.layout, 0, 1, &debugtextProgram.descriptorSets[frameOffset], 0, nullptr);
		}

		double trianglesPerSec = double(triangleCount) / double(frameGPUAvg * 1e-3);
		double drawsPerSec = double(scene->draws.size()) / double(frameGPUAvg * 1e-3);

		debugtext(0, ~0u, "%scpu: %.2f ms  (%+.2f); gpu: %.2f ms", reloadShaders ? "   " : "", frameCPUAvg, deltaTime * frameCPUAvg, frameGPUAvg);
		if (reloadShaders)
			debugtext(0, reloadShadersColor, "R*");

		if (debugGuiMode % 3 == 2)
		{
			debugtext(2, ~0u, "cull: %.2f ms, pyramid: %.2f ms, render: %.2f ms, shade: %.2f ms",
			    cullGPUTime + culllateGPUTime + cullpostGPUTime,
			    pyramidGPUTime,
			    renderGPUTime + renderlateGPUTime + renderpostGPUTime,
			    shadeGPUTime);
			debugtext(3, ~0u, "render breakdown: early %.2f ms, opaque %.2f ms, transparency %.2f ms",
			    renderGPUTime, renderlateGPUTime, renderpostGPUTime);

			debugtext(4, ~0u, "tlas: %.2f ms, shadows: %.2f ms, shadow blur: %.2f ms, taa: %.2f ms",
			    tlasGPUTime, shadowsGPUTime, shadowblurGPUTime, taaGPUTime);
			debugtext(5, ~0u, "triangles %.2fM; %.1fB tri / sec, %.1fM draws / sec",
			    double(triangleCount) * 1e-6, trianglesPerSec * 1e-9, drawsPerSec * 1e-6);
			debugtext(7, ~0u, "frustum culling %s, occlusion culling %s, level-of-detail %s",
			    cullingEnabled ? "ON" : "OFF", occlusionEnabled ? "ON" : "OFF", lodEnabled ? "ON" : "OFF");
			debugtext(8, ~0u, "mesh shading %s, task shading %s, cluster occlusion culling %s",
			    taskSubmit ? "ON" : "OFF", taskSubmit && taskShadingEnabled ? "ON" : "OFF",
			    clusterOcclusionEnabled ? "ON" : "OFF");

			debugtext(10, ~0u, "RT shadow %s, blur %s, shadow quality %d, shadow checkerboard %s",
			    raytracingSupported && shadowEnabled ? "ON" : "OFF",
			    raytracingSupported && shadowEnabled && shadowblurEnabled ? "ON" : "OFF",
			    shadowQuality, shadowCheckerboard ? "ON" : "OFF");
		}
	}

	vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, queryPoolTimestamp, TS_FrameEnd);

	const bool sceneRenderedToSwapchain = !editorViewportMode;
	if (shouldRenderRuntimeUi)
	{
		if (editorViewportMode && finalOutputImage)
		{
			if (editorViewportDescriptorSet == VK_NULL_HANDLE)
			{
				editorViewportDescriptorSet = ImGui_ImplVulkan_AddTexture(readSampler, finalOutputImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
			}

			VkImageMemoryBarrier2 viewportSampleBarrier = imageBarrier(finalOutputImage->image,
			    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL,
			    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
			pipelineBarrier(commandBuffer, VK_DEPENDENCY_BY_REGION_BIT, 0, nullptr, 1, &viewportSampleBarrier);
		}

		BuildRuntimeUi(deltaTime, frameCPUAvg, frameGPUAvg, cullGPUTime, pyramidGPUTime, culllateGPUTime, renderGPUTime, renderlateGPUTime, taaGPUTime);
		if (editorViewportMode && finalOutputImage && editorViewportDumpRequested)
		{
			ViewportDumpReadback& pending = viewportDumpReadbacks[frameOffset];
			if (pending.inUse)
			{
				editorViewportDumpStatus = "Previous viewport dump is still pending. Please retry next frame.";
				editorViewportDumpStatusIsError = true;
				LOGW("%s", editorViewportDumpStatus.c_str());
			}
			else
			{
				const uint32_t dumpWidth = currentRenderWidth;
				const uint32_t dumpHeight = currentRenderHeight;
				const size_t dumpSize = size_t(dumpWidth) * size_t(dumpHeight) * 4;
				resourceManager.CreateBuffer(pending.staging,
				    dumpSize,
				    VK_BUFFER_USAGE_TRANSFER_DST_BIT,
				    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
				pending.inUse = true;
				pending.width = dumpWidth;
				pending.height = dumpHeight;
				pending.saveAsExr = editorViewportDumpSaveAsExr;
				pending.outputPath = editorViewportDumpRequestPath;

				VkImageMemoryBarrier2 toTransferBarrier = imageBarrier(finalOutputImage->image,
				    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				    VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
				pipelineBarrier(commandBuffer, VK_DEPENDENCY_BY_REGION_BIT, 0, nullptr, 1, &toTransferBarrier);

				VkBufferImageCopy copyRegion{};
				copyRegion.bufferOffset = 0;
				copyRegion.bufferRowLength = 0;
				copyRegion.bufferImageHeight = 0;
				copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				copyRegion.imageSubresource.mipLevel = 0;
				copyRegion.imageSubresource.baseArrayLayer = 0;
				copyRegion.imageSubresource.layerCount = 1;
				copyRegion.imageExtent.width = dumpWidth;
				copyRegion.imageExtent.height = dumpHeight;
				copyRegion.imageExtent.depth = 1;

				vkCmdCopyImageToBuffer(commandBuffer, finalOutputImage->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, pending.staging.buffer, 1, &copyRegion);

				VkBufferMemoryBarrier2 hostReadBarrier = bufferBarrier(
				    pending.staging.buffer,
				    VK_PIPELINE_STAGE_TRANSFER_BIT,
				    VK_ACCESS_TRANSFER_WRITE_BIT,
				    VK_PIPELINE_STAGE_HOST_BIT,
				    VK_ACCESS_HOST_READ_BIT);
				VkImageMemoryBarrier2 toSampleBarrier = imageBarrier(finalOutputImage->image,
				    VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
				pipelineBarrier(commandBuffer, VK_DEPENDENCY_BY_REGION_BIT, 1, &hostReadBarrier, 1, &toSampleBarrier);
			}
			editorViewportDumpRequested = false;
			editorViewportDumpRequestPath.clear();
		}
		guiRenderer->EndFrame();
		const VkPipelineStageFlags2 uiSrcStage = sceneRenderedToSwapchain ? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT : VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
		const VkAccessFlags2 uiSrcAccess = sceneRenderedToSwapchain ? VK_ACCESS_SHADER_WRITE_BIT : VK_ACCESS_2_NONE;
		const VkImageLayout uiSrcLayout = sceneRenderedToSwapchain ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED;
		VkImageMemoryBarrier2 uiBarrier = imageBarrier(swapchain.images[imageIndex],
		    uiSrcStage, uiSrcAccess, uiSrcLayout,
		    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);
		pipelineBarrier(commandBuffer, VK_DEPENDENCY_BY_REGION_BIT, 0, nullptr, 1, &uiBarrier);
		guiRenderer->RenderDrawData(commandBuffer, swapchainImageViews[imageIndex], { uint32_t(swapchain.width), uint32_t(swapchain.height) }, editorViewportMode);
	}

	const VkPipelineStageFlags2 presentSrcStage = shouldRenderRuntimeUi
	                                                  ? VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
	                                                  : (sceneRenderedToSwapchain ? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT : VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT);
	const VkAccessFlags2 presentSrcAccess = shouldRenderRuntimeUi
	                                            ? VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
	                                            : (sceneRenderedToSwapchain ? VK_ACCESS_SHADER_WRITE_BIT : VK_ACCESS_2_NONE);
	const VkImageLayout presentSrcLayout = shouldRenderRuntimeUi
	                                           ? VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL
	                                           : (sceneRenderedToSwapchain ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED);
	VkImageMemoryBarrier2 presentBarrier = imageBarrier(swapchain.images[imageIndex],
	    presentSrcStage, presentSrcAccess, presentSrcLayout,
	    VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

	pipelineBarrier(commandBuffer, 0, 0, nullptr, 1, &presentBarrier);

	VK_CHECK(vkEndCommandBuffer(commandBuffer));

	VkSemaphoreSubmitInfo waitSemaphoreInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO };
	waitSemaphoreInfo.semaphore = acquireSemaphore;
	waitSemaphoreInfo.value = 0;
	waitSemaphoreInfo.stageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
	waitSemaphoreInfo.deviceIndex = 0;

	VkCommandBufferSubmitInfo commandBufferSubmitInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO };
	commandBufferSubmitInfo.commandBuffer = commandBuffer;
	commandBufferSubmitInfo.deviceMask = 0;

	VkSemaphoreSubmitInfo releaseSemaphoreInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO };
	releaseSemaphoreInfo.semaphore = releaseSemaphore;
	releaseSemaphoreInfo.value = 0;
	releaseSemaphoreInfo.stageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
	releaseSemaphoreInfo.deviceIndex = 0;

	VkSubmitInfo2 submitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO_2 };
	submitInfo.waitSemaphoreInfoCount = 1;
	submitInfo.pWaitSemaphoreInfos = &waitSemaphoreInfo;
	submitInfo.commandBufferInfoCount = 1;
	submitInfo.pCommandBufferInfos = &commandBufferSubmitInfo;
	submitInfo.signalSemaphoreInfoCount = 1;
	submitInfo.pSignalSemaphoreInfos = &releaseSemaphoreInfo;

	VK_CHECK_FORCE(vkQueueSubmit2(queue, 1, &submitInfo, frameFence));

	VkPresentInfoKHR presentInfo = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pWaitSemaphores = &releaseSemaphore;
	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains = &swapchain.swapchain;
	presentInfo.pImageIndices = &imageIndex;

	VK_CHECK_SWAPCHAIN(vkQueuePresentKHR(queue, &presentInfo));

	if (frameIndex >= MAX_FRAMES - 1)
	{
		int waitIndex = (frameIndex + 1) % MAX_FRAMES;
		VkFence waitFence = frameFences[waitIndex];
		VK_CHECK(vkWaitForFences(device, 1, &waitFence, VK_TRUE, ~0ull));
		ProcessCompletedViewportDump(uint32_t(waitIndex));
		VK_CHECK(vkResetFences(device, 1, &waitFence));

		VK_CHECK_QUERY(vkGetQueryPoolResults(device, queryPoolsTimestamp[waitIndex], 0, COUNTOF(timestampResults), sizeof(timestampResults), timestampResults, sizeof(timestampResults[0]), VK_QUERY_RESULT_64_BIT));
#if defined(WIN32)
		VK_CHECK_QUERY(vkGetQueryPoolResults(device, queryPoolsPipeline[waitIndex], 0, COUNTOF(pipelineResults), sizeof(pipelineResults), pipelineResults, sizeof(pipelineResults[0]), VK_QUERY_RESULT_64_BIT));
#endif
		double frameGPUBegin = double(timestampResults[TS_FrameBegin]) * props.limits.timestampPeriod * 1e-6;
		double frameGPUEnd = double(timestampResults[TS_FrameEnd]) * props.limits.timestampPeriod * 1e-6;
		frameGPUAvg = frameGPUAvg * 0.9 + (frameGPUEnd - frameGPUBegin) * 0.1;
	}

	for (size_t i = 0; i < pendingViewportDescriptorReleases.size();)
	{
		if (frameIndex >= pendingViewportDescriptorReleases[i].safeAfterFrame && pendingViewportDescriptorReleases[i].descriptorSet != VK_NULL_HANDLE)
		{
			ImGui_ImplVulkan_RemoveTexture(pendingViewportDescriptorReleases[i].descriptorSet);
			pendingViewportDescriptorReleases.erase(pendingViewportDescriptorReleases.begin() + i);
		}
		else
		{
			++i;
		}
	}
	if (pendingTexturePoolPurgeAfterFrame != 0 && frameIndex >= pendingTexturePoolPurgeAfterFrame)
	{
		resourceManager.PurgeUnusedTextures();
		pendingTexturePoolPurgeAfterFrame = 0;
	}
	double frameCPUEnd = GetTimeInSeconds();

	frameCPUAvg = frameCPUAvg * 0.9 + (frameCPUEnd - frameCPUBegin) * 0.1;

	if (debugSleep)
	{
#if defined(WIN32)
		Sleep(200);
#endif
	}

	frameIndex++;

	resourceManager.EndFrame();

	return true;
}

void VulkanContext::BuildRuntimeUi(float deltaTime,
    double frameCPUAvg,
    double frameGPUAvg,
    double cullGPUTime,
    double pyramidGPUTime,
    double culllateGPUTime,
    double renderGPUTime,
    double renderlateGPUTime,
    double taaGPUTime)
{
	if (editorViewportMode)
	{
		const ImGuiViewport* mainViewport = ImGui::GetMainViewport();
		const float panelWidth = 360.0f;
		const float panelGap = 20.0f;
		const float workHeight = std::max(1.0f, mainViewport->WorkSize.y);
		float viewportWindowW = std::max(1.0f, mainViewport->WorkSize.x - panelWidth - panelGap);
		float viewportWindowH = workHeight;
#if defined(WIN32)
		if (editorInitialViewportRequestWidth > 0u && editorInitialViewportRequestHeight > 0u)
		{
			const ImGuiStyle& st = ImGui::GetStyle();
			const float titleBarH = ImGui::GetFrameHeight();
			viewportWindowW = float(editorInitialViewportRequestWidth) + st.WindowPadding.x * 2.0f;
			viewportWindowH = float(editorInitialViewportRequestHeight) + titleBarH + st.WindowPadding.y * 2.0f;
			const float maxW = std::max(1.0f, mainViewport->WorkSize.x - panelWidth - panelGap);
			const float maxH = std::max(1.0f, mainViewport->WorkSize.y);
			viewportWindowW = std::min(viewportWindowW, maxW);
			viewportWindowH = std::min(viewportWindowH, maxH);
		}
#endif

		// Keep editor UI windows synced with the host window size.
		ImGui::SetNextWindowPos(ImVec2(mainViewport->WorkPos.x, mainViewport->WorkPos.y), ImGuiCond_Always);
		ImGui::SetNextWindowSize(ImVec2(panelWidth, workHeight), ImGuiCond_Always);
		ImGui::Begin("kaleido editor");

		if (ImGui::CollapsingHeader("Rendering", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::Checkbox("Enable Mesh Shading", &meshShadingEnabled);
			ImGui::Checkbox("Enable Task Shading", &taskShadingEnabled);
			ImGui::Checkbox("Enable Culing", &cullingEnabled);
			ImGui::Checkbox("Enable Occlusion Culling", &occlusionEnabled);
			ImGui::Checkbox("Enable Cluster Occlusion Culling", &clusterOcclusionEnabled);
			ImGui::Checkbox("Enable Shadow", &shadowEnabled);
			ImGui::SetNextItemWidth(220.f);
			ImGui::SliderInt("Shadow Quality (0=low, 1=high)", &shadowQuality, 0, 1);
			ImGui::Checkbox("Enable Shadow Blurring", &shadowblurEnabled);
			ImGui::Checkbox("Enable Shadow Checkerboard", &shadowCheckerboard);
			ImGui::Checkbox("Enable TAA", &taaEnabled);
			if (taaEnabled)
			{
				ImGui::SetNextItemWidth(220.f);
				ImGui::SliderFloat("TAA Blend Alpha", &taaBlendAlpha, 0.01f, 1.0f, "%.2f");
			}
			ImGui::Checkbox("Screen-Space Refraction", &screenSpaceRefractionEnabled);
			ImGui::Checkbox("Enable LoD", &lodEnabled);
			if (lodEnabled)
			{
				ImGui::SetNextItemWidth(120.f);
				ImGui::DragInt("Level Index(LoD)", &debugLodStep, 1, 0, 9);
			}
			ImGui::Checkbox("Enable Animation", &animationEnabled);
			ImGui::Checkbox("Enable Reload Shaders", &reloadShaders);
			ImGui::SetNextItemWidth(220.f);
			ImGui::SliderInt("Debug Info Mode (0=off, 1=on, 2=verbose)", &debugGuiMode, 0, 2);
			ImGui::Checkbox("Enable Debug Sleep", &debugSleep);
			if (!wireframeDebugSupported && gbufferDebugViewMode == 1)
				gbufferDebugViewMode = 0;
			{
				static const char* gbufferViewItems[] = { "Lit (shaded)", "Wireframe", "Meshlet (random color)" };
				ImGui::SetNextItemWidth(260.f);
				ImGui::Combo("G-buffer debug view", &gbufferDebugViewMode, gbufferViewItems, IM_ARRAYSIZE(gbufferViewItems));
			}
			if (!wireframeDebugSupported)
				ImGui::TextDisabled("Wireframe needs GPU fillModeNonSolid.");
			ImGui::Text("Cluster Ray Tracing Enabled: %s", clusterRTEnabled ? "ON" : "OFF");
		}

		if (ImGui::CollapsingHeader("Scene", ImGuiTreeNodeFlags_DefaultOpen))
		{
			if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen))
			{
				ImGui::DragFloat3("Camera Position", (float*)(&scene->camera.position), 0.01f);
				float rotations[3] = { pitch, yaw, roll };
				if (ImGui::DragFloat3("Camera Rotation (Pitch, Yaw, Roll)", rotations, 0.01f))
				{
					pitch = rotations[0];
					yaw = rotations[1];
					roll = rotations[2];
					cameraDirty = true;
				}
				ImGui::SetNextItemWidth(220.f);
				ImGui::DragFloat("Camera Moving Speed", &cameraSpeed, 0.01f, 0.0f, 10.f);
				if (ImGui::Checkbox("Enable Dolly Zoom", &enableDollyZoom))
					cameraOriginForDolly = scene->camera.position;
				if (enableDollyZoom)
				{
					ImGui::SetNextItemWidth(220.f);
					ImGui::DragFloat("Dolly Zoom Ref Distance", &soRef, 0.01f, 1.0f, 100.f);
				}
			}
		}

		if (ImGui::CollapsingHeader("Assets", ImGuiTreeNodeFlags_DefaultOpen))
		{
			static char scenePathInput[512] = "";
			static std::string assetLoadStatus;
			static bool assetLoadStatusIsError = false;

			ImGui::Text("Current scene path: %s", scene->path.empty() ? "<empty>" : scene->path.c_str());
			ImGui::InputText("Load Path", scenePathInput, sizeof(scenePathInput));
#if defined(WIN32)
			ImGui::SameLine();
			if (ImGui::Button("Browse..."))
			{
				std::string selectedPath;
				if (ShowOpenSceneDialog(selectedPath))
				{
					strncpy_s(scenePathInput, sizeof(scenePathInput), selectedPath.c_str(), _TRUNCATE);
					assetLoadStatus = "Selected scene file.";
					assetLoadStatusIsError = false;
				}
			}
#endif
			ImGui::Text("Supported scene assets: .gltf / .glb");

			if (ImGui::Button("Load Scene"))
			{
				std::string requestPath = scenePathInput;
				if (requestPath.empty())
				{
					assetLoadStatus = "Please input a scene path first.";
					assetLoadStatusIsError = true;
				}
				else
				{
					const bool supported = IsSceneAssetPath(requestPath);
					if (!supported)
					{
						assetLoadStatus = "Unsupported scene format. Use .gltf or .glb.";
						assetLoadStatusIsError = true;
					}
					else if (!std::filesystem::exists(requestPath))
					{
						assetLoadStatus = "Scene file does not exist.";
						assetLoadStatusIsError = true;
					}
					else
					{
						RequestEditorSceneLoad(requestPath);
						assetLoadStatus = "Scene load request submitted.";
						assetLoadStatusIsError = false;
					}
				}
			}

			ImGui::Spacing();
#if defined(WIN32)
			if (ImGui::Button("Save Scene"))
			{
				if (scene->path.empty() || !IsSceneAssetPath(scene->path))
				{
					assetLoadStatus = "Current scene has no valid .gltf/.glb source path to serialize.";
					assetLoadStatusIsError = true;
				}
				else
				{
					std::string outPath;
					if (ShowSaveSceneStateDialog(outPath))
					{
						EditorSceneSnapshot snapshot{};
						snapshot.modelPath = scene->path;
						snapshot.camera = CaptureEditorCameraState(*scene);
						snapshot.renderSettings = CaptureEditorRenderSettings();
						snapshot.editorUi = CaptureEditorSceneUiState(*scene);
						snapshot.transformNodeLocals = CaptureEditorTransformNodeLocals(*scene);
						snapshot.materialOverrides = CaptureEditorMaterialOverrides(*scene);
						if (editorViewportWidth > 0u && editorViewportHeight > 0u)
						{
							snapshot.viewportWidth = editorViewportWidth;
							snapshot.viewportHeight = editorViewportHeight;
						}
						std::string saveError;
						if (SaveEditorSceneSnapshot(outPath, snapshot, &saveError))
						{
							assetLoadStatus = "Scene JSON saved.";
							assetLoadStatusIsError = false;
						}
						else
						{
							assetLoadStatus = "Failed to save scene JSON: " + saveError;
							assetLoadStatusIsError = true;
						}
					}
				}
			}

			ImGui::SameLine();
			if (ImGui::Button("Restore Scene"))
			{
				std::string inPath;
				if (ShowOpenSceneStateDialog(inPath))
				{
					RequestEditorSceneLoad(inPath);
					assetLoadStatus = "Scene restore request submitted.";
					assetLoadStatusIsError = false;
				}
			}

			static int dumpFormat = 0; // 0=PNG, 1=EXR
			static const char* dumpFormats[] = { "PNG (8-bit)", "EXR (FP16)" };
			ImGui::SetNextItemWidth(200.f);
			ImGui::Combo("Viewport Dump Format", &dumpFormat, dumpFormats, IM_ARRAYSIZE(dumpFormats));
			if (ImGui::Button("Dump Viewport"))
			{
				std::string outPath;
				const bool saveAsExr = dumpFormat == 1;
				if (ShowSaveViewportDumpDialog(outPath, saveAsExr))
				{
					std::filesystem::path dumpPath(outPath);
					if (dumpPath.extension().empty())
						dumpPath += saveAsExr ? ".exr" : ".png";

					editorViewportDumpRequested = true;
					editorViewportDumpSaveAsExr = saveAsExr;
					editorViewportDumpRequestPath = dumpPath.string();
					editorViewportDumpStatus = "Viewport dump request submitted.";
					editorViewportDumpStatusIsError = false;
				}
			}
#else
			ImGui::TextDisabled("Save/Restore/Dump Scene buttons are available on Win32.");
#endif

			if (!assetLoadStatus.empty())
			{
				ImVec4 color = assetLoadStatusIsError ? ImVec4(1.0f, 0.35f, 0.35f, 1.0f) : ImVec4(0.35f, 1.0f, 0.35f, 1.0f);
				ImGui::TextColored(color, "%s", assetLoadStatus.c_str());
			}
			if (!editorViewportDumpStatus.empty())
			{
				ImVec4 color = editorViewportDumpStatusIsError ? ImVec4(1.0f, 0.35f, 0.35f, 1.0f) : ImVec4(0.35f, 1.0f, 0.35f, 1.0f);
				ImGui::TextColored(color, "%s", editorViewportDumpStatus.c_str());
			}

			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();
			if (scene)
				DrawGltfDocumentTree(*scene);
		}

		ImGui::End();

		ImGui::SetNextWindowPos(ImVec2(mainViewport->WorkPos.x + panelWidth + panelGap, mainViewport->WorkPos.y), ImGuiCond_Always);
		ImGui::SetNextWindowSize(ImVec2(viewportWindowW, viewportWindowH), ImGuiCond_Always);
		ImGui::Begin("viewport");
		const ImVec2 windowPos = ImGui::GetWindowPos();
		const ImVec2 contentMin = ImGui::GetWindowContentRegionMin();
		const ImVec2 contentMax = ImGui::GetWindowContentRegionMax();
		g_editorViewportRectMin = ImVec2(windowPos.x + contentMin.x, windowPos.y + contentMin.y);
		g_editorViewportRectMax = ImVec2(windowPos.x + contentMax.x, windowPos.y + contentMax.y);
		g_editorViewportRectValid = true;
		const ImVec2 viewportSize = ImGui::GetContentRegionAvail();
		const uint32_t nextWidth = uint32_t(std::max(1.0f, viewportSize.x));
		const uint32_t nextHeight = uint32_t(std::max(1.0f, viewportSize.y));
		if (nextWidth != editorViewportWidth || nextHeight != editorViewportHeight)
		{
			editorViewportWidth = nextWidth;
			editorViewportHeight = nextHeight;
		}

		if (editorViewportDescriptorSet != VK_NULL_HANDLE)
		{
			ImGui::Image((ImTextureID)editorViewportDescriptorSet, viewportSize, ImVec2(0, 0), ImVec2(1, 1));
		}
		else
		{
			ImGui::Text("Viewport rendering target is preparing...");
		}
		ImGui::End();
		return;
	}

	static bool bDisplaySettings = true;
	static bool bDisplayProfiling = true;
	static bool bDisplayScene = true;
	if (ImGui::BeginMainMenuBar())
	{
		ImGui::Dummy(ImVec2(50.f, 0.f));
		ImGui::Checkbox("Settings", &bDisplaySettings);
		ImGui::Checkbox("Profiling", &bDisplayProfiling);
		ImGui::Checkbox("Scene", &bDisplayScene);
		ImGui::EndMainMenuBar();
	}

	if (bDisplaySettings)
	{
		ImGui::Begin("Global Settings");
		ImGui::Checkbox("Enable Mesh Shading", &meshShadingEnabled);
		ImGui::Checkbox("Enable Task Shading", &taskShadingEnabled);
		ImGui::Checkbox("Enable Culing", &cullingEnabled);
		ImGui::Checkbox("Enable Occlusion Culling", &occlusionEnabled);
		ImGui::Checkbox("Enable Cluster Occlusion Culling", &clusterOcclusionEnabled);
		ImGui::Checkbox("Enable Shadow", &shadowEnabled);
		ImGui::SetNextItemWidth(200.f);
		ImGui::SliderInt("Shadow Quality (0=low, 1=high)", &shadowQuality, 0, 1);
		ImGui::Checkbox("Enable Shadow Blurring", &shadowblurEnabled);
		ImGui::Checkbox("Enable Shadow Checkerboard", &shadowCheckerboard);
		ImGui::Checkbox("Enable TAA", &taaEnabled);
		if (taaEnabled)
		{
			ImGui::SetNextItemWidth(200.f);
			ImGui::SliderFloat("TAA Blend Alpha", &taaBlendAlpha, 0.01f, 1.0f, "%.2f");
		}
		ImGui::Checkbox("Screen-Space Refraction", &screenSpaceRefractionEnabled);
		ImGui::Checkbox("Enable LoD", &lodEnabled);
		if (lodEnabled)
		{
			ImGui::SetNextItemWidth(100.f);
			ImGui::DragInt("Level Index(LoD)", &debugLodStep, 1, 0, 9);
		}
		ImGui::Checkbox("Enable Reload Shaders", &reloadShaders);
		ImGui::SetNextItemWidth(200.f);
		ImGui::SliderInt("Debug Info Mode (0=off, 1=on, 2=verbose)", &debugGuiMode, 0, 2);
		ImGui::Checkbox("Enable Animation", &animationEnabled);
		ImGui::Text("Cluster Ray Tracing Enabled: %s", clusterRTEnabled ? "ON" : "OFF");
		ImGui::Checkbox("Enable Debug Sleep", &debugSleep);
		if (!wireframeDebugSupported && gbufferDebugViewMode == 1)
			gbufferDebugViewMode = 0;
		{
			static const char* gbufferViewItems[] = { "Lit (shaded)", "Wireframe", "Meshlet (random color)" };
			ImGui::SetNextItemWidth(240.f);
			ImGui::Combo("G-buffer debug view", &gbufferDebugViewMode, gbufferViewItems, IM_ARRAYSIZE(gbufferViewItems));
		}
		if (!wireframeDebugSupported)
			ImGui::TextDisabled("Wireframe needs GPU fillModeNonSolid.");
		ImGui::End();
	}

	if (bDisplayProfiling)
	{
		ImGui::Begin("Performance Monitor");
		{
			static float framerate = 0.0f;
			framerate = 0.9f * framerate + 0.1f * 1.0f / deltaTime;
			static TimeSeriesPlot frPlot(100);
			frPlot.addValue(framerate);
			ImGui::SetNextItemWidth(400.f);
			ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(0.0f, 1.0f, 0.0f, 1.0f));
			ImGui::PlotLines("##Avg Frame Rate",
			    frPlot.data(),
			    frPlot.size(),
			    frPlot.currentOffset(),
			    nullptr,
			    0.0f, 40.0f,
			    ImVec2(0, 80));
			ImGui::PopStyleColor();
			ImGui::SameLine();
			DisplayProfilingData("Avg Frame Rate: ", framerate, 60.f, 30.f, std::greater<float>());
		}
		{
			static TimeSeriesPlot cpuPlot(100);
			cpuPlot.addValue(frameCPUAvg);
			ImGui::SetNextItemWidth(400.f);
			ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(0.0f, 1.0f, 1.0f, 1.0f));
			ImGui::PlotLines("##Avg CPU Time",
			    cpuPlot.data(),
			    cpuPlot.size(),
			    cpuPlot.currentOffset(),
			    nullptr,
			    0.0f, 40.0f,
			    ImVec2(0, 80));
			ImGui::PopStyleColor();
			ImGui::SameLine();
			DisplayProfilingData("Avg CPU Time(ms): ", frameCPUAvg, 16.7, 33.4);
		}
		{
			static TimeSeriesPlot gpuPlot(100);
			gpuPlot.addValue(frameGPUAvg);
			ImGui::SetNextItemWidth(400.f);
			ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(1.0f, 1.0f, 0.0f, 1.0f));
			ImGui::PlotLines("##Avg GPU Time",
			    gpuPlot.data(),
			    gpuPlot.size(),
			    gpuPlot.currentOffset(),
			    nullptr,
			    0.0f, 40.0f,
			    ImVec2(0, 80));
			ImGui::PopStyleColor();
			ImGui::SameLine();
			DisplayProfilingData("Avg GPU Time(ms): ", frameGPUAvg, 16.7, 33.4);
		}
		{
			static TimeSeriesPlot gpuCullPlot(100);
			gpuCullPlot.addValue(cullGPUTime);
			ImGui::SetNextItemWidth(400.f);
			ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(1.0f, 0.0f, 1.0f, 1.0f));
			ImGui::PlotLines("##Culling GPU Time",
			    gpuCullPlot.data(),
			    gpuCullPlot.size(),
			    gpuCullPlot.currentOffset(),
			    nullptr,
			    0.0f, 40.0f,
			    ImVec2(0, 80));
			ImGui::PopStyleColor();
			ImGui::SameLine();
			DisplayProfilingData("Culling GPU Time(ms): ", cullGPUTime, 1.0, 2.0);
		}
		{
			static TimeSeriesPlot gpuCullLatePlot(100);
			gpuCullLatePlot.addValue(culllateGPUTime);
			ImGui::SetNextItemWidth(400.f);
			ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(0.5f, 1.0f, 0.0f, 1.0f));
			ImGui::PlotLines("##Culling Opaque GPU Time",
			    gpuCullLatePlot.data(),
			    gpuCullLatePlot.size(),
			    gpuCullLatePlot.currentOffset(),
			    nullptr,
			    0.0f, 40.0f,
			    ImVec2(0, 80));
			ImGui::PopStyleColor();
			ImGui::SameLine();
			DisplayProfilingData("Culling Opaque GPU Time(ms): ", culllateGPUTime, 1.0, 2.0);
		}
		{
			static TimeSeriesPlot gpuRenderingPlot(100);
			gpuRenderingPlot.addValue(renderGPUTime);
			ImGui::SetNextItemWidth(400.f);
			ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(0.0f, 1.0f, 0.5f, 1.0f));
			ImGui::PlotLines("##Rendering GPU Time",
			    gpuRenderingPlot.data(),
			    gpuRenderingPlot.size(),
			    gpuRenderingPlot.currentOffset(),
			    nullptr,
			    0.0f, 40.0f,
			    ImVec2(0, 80));
			ImGui::PopStyleColor();
			ImGui::SameLine();
			DisplayProfilingData("Rendering GPU Time(ms): ", renderGPUTime, 4.0, 8.0);
		}
		{
			static TimeSeriesPlot gpuRenderingLatePlot(100);
			gpuRenderingLatePlot.addValue(renderlateGPUTime);
			ImGui::SetNextItemWidth(400.f);
			ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(0.3f, 0.5f, 0.3f, 1.0f));
			ImGui::PlotLines("##Rendering Opaque GPU Time",
			    gpuRenderingLatePlot.data(),
			    gpuRenderingLatePlot.size(),
			    gpuRenderingLatePlot.currentOffset(),
			    nullptr,
			    0.0f, 40.0f,
			    ImVec2(0, 80));
			ImGui::PopStyleColor();
			ImGui::SameLine();
			DisplayProfilingData("Rendering Opaque GPU Time(ms): ", renderlateGPUTime, 4.0, 8.0);
		}
		{
			static TimeSeriesPlot depthPyramidPlot(100);
			depthPyramidPlot.addValue(pyramidGPUTime);
			ImGui::SetNextItemWidth(400.f);
			ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(0.5f, 0.0f, 0.3f, 1.0f));
			ImGui::PlotLines("##Depth Pyramid GPU Time",
			    depthPyramidPlot.data(),
			    depthPyramidPlot.size(),
			    depthPyramidPlot.currentOffset(),
			    nullptr,
			    0.0f, 40.0f,
			    ImVec2(0, 80));
			ImGui::PopStyleColor();
			ImGui::SameLine();
			DisplayProfilingData("Depth Pyramid GPU Time(ms): ", pyramidGPUTime, 1.0, 2.0);
		}
		{
			static TimeSeriesPlot taaGpuPlot(100);
			taaGpuPlot.addValue(taaGPUTime);
			ImGui::SetNextItemWidth(400.f);
			ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(0.8f, 0.6f, 0.2f, 1.0f));
			ImGui::PlotLines("##TAA GPU Time",
			    taaGpuPlot.data(),
			    taaGpuPlot.size(),
			    taaGpuPlot.currentOffset(),
			    nullptr,
			    0.0f, 40.0f,
			    ImVec2(0, 80));
			ImGui::PopStyleColor();
			ImGui::SameLine();
			DisplayProfilingData("TAA GPU Time(ms): ", taaGPUTime, 1.0, 2.0);
		}
		ImGui::End();
	}

	if (bDisplayScene)
	{
		ImGui::Begin("Scene");
		if (ImGui::CollapsingHeader("Camera"))
		{
			ImGui::DragFloat3("Camera Position", (float*)(&scene->camera.position), 0.01f);
			float rotations[3] = { pitch, yaw, roll };
			if (ImGui::DragFloat3("Camera Rotation (Pitch, Yaw, Roll)", rotations, 0.01f))
			{
				pitch = rotations[0];
				yaw = rotations[1];
				roll = rotations[2];
				cameraDirty = true;
			}
			ImGui::SetNextItemWidth(200.f);
			ImGui::DragFloat("Camera Moving Speed", &cameraSpeed, 0.01f, 0.0f, 10.f);
			if (ImGui::Checkbox("Enable Dolly Zoom", &enableDollyZoom))
			{
				// Update the camera origin for dolly zoom
				cameraOriginForDolly = scene->camera.position;
			}
			if (enableDollyZoom)
			{
				ImGui::SetNextItemWidth(200.f);
				ImGui::DragFloat("Dolly Zoom Ref Distance", &soRef, 0.01f, 1.0f, 100.f);
			}
		}
		ImGui::End();
	}
}

void VulkanContext::DestroyInstance()
{
	if (gInstance)
	{
		gInstance->Release();
		gInstance = nullptr;
	}
}

void VulkanContext::Release()
{
	VK_CHECK(vkDeviceWaitIdle(device));
	ReleaseViewportDumpReadbacks();

	if (depthPyramidHandle.IsValid())
	{
		for (uint32_t i = 0; i < depthPyramidLevels; ++i)
		{
			resourceManager.ReleaseImageView(depthPyramidMips[i]);
			depthPyramidMips[i] = VK_NULL_HANDLE;
		}

		resourceManager.ReleaseTexture(depthPyramidHandle);
	}
	if (shadowTargetHandle.IsValid())
		resourceManager.ReleaseTexture(shadowTargetHandle);
	if (shadowblurTargetHandle.IsValid())
		resourceManager.ReleaseTexture(shadowblurTargetHandle);
	if (sceneColorHDRHandle.IsValid())
		resourceManager.ReleaseTexture(sceneColorHDRHandle);
	if (sceneColorResolvedHandle.IsValid())
		resourceManager.ReleaseTexture(sceneColorResolvedHandle);
	for (int ti = 0; ti < 2; ++ti)
		if (taaHistoryHandles[ti].IsValid())
			resourceManager.ReleaseTexture(taaHistoryHandles[ti]);
	if (editorViewportDescriptorSet != VK_NULL_HANDLE)
	{
		ImGui_ImplVulkan_RemoveTexture(editorViewportDescriptorSet);
		editorViewportDescriptorSet = VK_NULL_HANDLE;
	}
	for (const PendingViewportDescriptorRelease& pending : pendingViewportDescriptorReleases)
	{
		if (pending.descriptorSet != VK_NULL_HANDLE)
			ImGui_ImplVulkan_RemoveTexture(pending.descriptorSet);
	}
	pendingViewportDescriptorReleases.clear();
	if (pendingTexturePoolPurgeAfterFrame != 0)
	{
		resourceManager.PurgeUnusedTextures();
		pendingTexturePoolPurgeAfterFrame = 0;
	}
	if (editorViewportTargetHandle.IsValid())
	{
		resourceManager.ReleaseTexture(editorViewportTargetHandle);
		editorViewportTargetHandle = {};
	}
	currentRenderWidth = 0;
	currentRenderHeight = 0;

	for (uint32_t i = 0; i < swapchain.imageCount; ++i)
		if (swapchainImageViews[i])
		{
			resourceManager.DestroyImageView(swapchainImageViews[i]);
			swapchainImageViews[i] = VK_NULL_HANDLE;
		}

	// Destroy pooled RG resources (textures/buffers/views) before destroying the device.
	resourceManager.DestroyAll();

	for (Image& image : scene->images)
	{
		resourceManager.DestroyImage(image);
	}

	for (uint32_t i = 0; i < gbufferCount; ++i)
		if (gbufferTargetHandles[i].IsValid())
			resourceManager.ReleaseTexture(gbufferTargetHandles[i]);

	if (depthTargetHandle.IsValid())
		resourceManager.ReleaseTexture(depthTargetHandle);

	resourceManager.DestroyBuffer(outlineDccb);
	resourceManager.DestroyBuffer(outlineDcb);
#if defined(WIN32)
	resourceManager.DestroyBuffer(editorAabbLineVb);
#endif
	resourceManager.DestroyBuffer(dccb);
	resourceManager.DestroyBuffer(dcb);
	resourceManager.DestroyBuffer(dvb);
	resourceManager.DestroyBuffer(db);

	resourceManager.DestroyBuffer(mb);
	resourceManager.DestroyBuffer(mtb);
	{
		resourceManager.DestroyBuffer(mlb);
		resourceManager.DestroyBuffer(mdb);
		resourceManager.DestroyBuffer(mvb);
		resourceManager.DestroyBuffer(cib);
		resourceManager.DestroyBuffer(ccb);
	}

	if (raytracingSupported)
	{
		vkDestroyAccelerationStructureKHR(device, tlas, 0);
		for (VkAccelerationStructureKHR as : blas)
			vkDestroyAccelerationStructureKHR(device, as, 0);

		resourceManager.DestroyBuffer(tlasBuffer);
		resourceManager.DestroyBuffer(blasBuffer);
		resourceManager.DestroyBuffer(tlasScratchBuffer);
		resourceManager.DestroyBuffer(tlasInstanceBuffer);
	}

	resourceManager.DestroyBuffer(ib);
	resourceManager.DestroyBuffer(vb);
	resourceManager.DestroyBuffer(scratch);

	for (size_t ii = 0; ii < MAX_FRAMES; ++ii)
	{
		vkFreeCommandBuffers(device, commandPools[ii], 1, &commandBuffers[ii]);
		vkDestroyCommandPool(device, commandPools[ii], 0);
	}

	destroySwapchain(device, swapchain);

	for (auto queryPoolTimestamp : queryPoolsTimestamp)
		vkDestroyQueryPool(device, queryPoolTimestamp, 0);

#if defined(WIN32)
	for (auto queryPoolPipeline : queryPoolsPipeline)
		vkDestroyQueryPool(device, queryPoolPipeline, 0);
#endif

	for (VkPipeline pipeline : pipelines)
		vkDestroyPipeline(device, pipeline, 0);

	destroyProgram(device, meshProgram, descriptorPool);
	destroyProgram(device, meshSelectionOutlineProgram, descriptorPool);
#if defined(WIN32)
	destroyProgram(device, editorAabbLineProgram, descriptorPool);
#endif
	destroyProgram(device, meshtaskProgram, descriptorPool);
	destroyProgram(device, debugtextProgram, descriptorPool);
	destroyProgram(device, drawcullProgram, descriptorPool);
	destroyProgram(device, tasksubmitProgram, descriptorPool);
	destroyProgram(device, clustersubmitProgram, descriptorPool);
	destroyProgram(device, clustercullProgram, descriptorPool);
	destroyProgram(device, clusterProgram, descriptorPool);
	destroyProgram(device, transparencyBlendMeshProgram, descriptorPool);
	destroyProgram(device, transparencyBlendMeshtaskProgram, descriptorPool);
	destroyProgram(device, transparencyBlendClusterProgram, descriptorPool);
	destroyProgram(device, depthreduceProgram, descriptorPool);
	destroyProgram(device, transmissionResolveProgram, descriptorPool);
	destroyProgram(device, taaProgram, descriptorPool);

	if (raytracingSupported)
	{
		destroyProgram(device, finalProgram, descriptorPool);
		destroyProgram(device, shadowProgram, descriptorPool);
		destroyProgram(device, shadowfillProgram, descriptorPool);
		destroyProgram(device, shadowblurProgram, descriptorPool);
	}

	vkDestroyDescriptorSetLayout(device, textureSetLayout, 0);

	vkDestroyDescriptorPool(device, descriptorPool, 0);
	vkDestroyDescriptorPool(device, scene->textureSet.first, 0);

#if defined(__ANDROID__) // Remove this when upgrading to Vulkan 1.4
	for (Shader& shader : shaderSet.shaders)
		if (shader.module)
			vkDestroyShaderModule(device, shader.module, 0);
#endif

	vkDestroySampler(device, textureSampler, 0);
	vkDestroySampler(device, readSampler, 0);
	vkDestroySampler(device, depthSampler, 0);

	for (auto frameFence : frameFences)
		vkDestroyFence(device, frameFence, 0);
	for (auto acquireSemaphore : acquireSemaphores)
		vkDestroySemaphore(device, acquireSemaphore, 0);
	for (auto releaseSemaphoreVector : releaseSemaphores)
		for (auto releaseSemaphore : releaseSemaphoreVector)
			vkDestroySemaphore(device, releaseSemaphore, 0);

	// move gui renderer
	auto guiRenderer = GuiRenderer::GetInstance();
	guiRenderer->Shutdown(device);

	vkDestroySurfaceKHR(instance, surface, 0);
#if defined(WIN32)
	glfwDestroyWindow(window);
#endif
	vkDestroyDevice(device, 0);

	if (debugCallback)
		vkDestroyDebugReportCallbackEXT(instance, debugCallback, 0);

	vkDestroyInstance(instance, 0);

	volkFinalize();
	frameResourcesInitialized = false;
}

Renderer::Renderer()
{
	VulkanContext::GetInstance();
#if defined(WIN32)
	lastFrame = glfwGetTime();
#endif
}

Renderer::~Renderer()
{
}

bool Renderer::DrawFrame()
{
	auto gContext = VulkanContext::GetInstance();
	return gContext->DrawFrame();
}