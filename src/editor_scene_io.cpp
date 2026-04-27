#include "editor_scene_io.h"

#include "asset_paths.h"

#include <fstream>
#include <sstream>

#include <glm/gtc/type_ptr.hpp>

#include "rapidjson/document.h"
#include "rapidjson/error/en.h"
#include "rapidjson/prettywriter.h"
#include "rapidjson/stringbuffer.h"

namespace
{
template <typename Writer>
void WriteVec3(Writer& writer, const char* key, const vec3& v)
{
	writer.Key(key);
	writer.StartArray();
	writer.Double(v.x);
	writer.Double(v.y);
	writer.Double(v.z);
	writer.EndArray();
}

template <typename Writer>
void WriteVec4(Writer& writer, const char* key, const vec4& v)
{
	writer.Key(key);
	writer.StartArray();
	writer.Double(v.x);
	writer.Double(v.y);
	writer.Double(v.z);
	writer.Double(v.w);
	writer.EndArray();
}

void AppendParseError(std::string& message, rapidjson::ParseResult parseResult)
{
	message = "JSON parse failed: ";
	message += rapidjson::GetParseError_En(parseResult.Code());
	message += " at offset ";
	message += std::to_string(parseResult.Offset());
}

bool ReadVec3(const rapidjson::Value& value, vec3& outValue)
{
	if (!value.IsArray() || value.Size() != 3 || !value[0].IsNumber() || !value[1].IsNumber() || !value[2].IsNumber())
		return false;

	outValue = vec3(
	    static_cast<float>(value[0].GetDouble()),
	    static_cast<float>(value[1].GetDouble()),
	    static_cast<float>(value[2].GetDouble()));
	return true;
}

bool ReadVec4(const rapidjson::Value& value, vec4& outValue)
{
	if (!value.IsArray() || value.Size() != 4 || !value[0].IsNumber() || !value[1].IsNumber() || !value[2].IsNumber() || !value[3].IsNumber())
		return false;

	outValue = vec4(
	    static_cast<float>(value[0].GetDouble()),
	    static_cast<float>(value[1].GetDouble()),
	    static_cast<float>(value[2].GetDouble()),
	    static_cast<float>(value[3].GetDouble()));
	return true;
}

void ReadBool(const rapidjson::Value& obj, const char* key, bool& target)
{
	auto it = obj.FindMember(key);
	if (it != obj.MemberEnd() && it->value.IsBool())
		target = it->value.GetBool();
}

void ReadInt(const rapidjson::Value& obj, const char* key, int& target)
{
	auto it = obj.FindMember(key);
	if (it != obj.MemberEnd() && it->value.IsInt())
		target = it->value.GetInt();
}

void ReadFloat(const rapidjson::Value& obj, const char* key, float& target)
{
	auto it = obj.FindMember(key);
	if (it != obj.MemberEnd() && it->value.IsNumber())
		target = static_cast<float>(it->value.GetDouble());
}

void ReadUint32Positive(const rapidjson::Value& obj, const char* key, uint32_t& target)
{
	auto it = obj.FindMember(key);
	if (it == obj.MemberEnd())
		return;
	const rapidjson::Value& v = it->value;
	if (v.IsUint() && v.GetUint() > 0u)
		target = v.GetUint();
	else if (v.IsInt() && v.GetInt() > 0)
		target = static_cast<uint32_t>(v.GetInt());
}

template <typename Writer>
void WriteMat4ColumnMajor(Writer& writer, const mat4& m)
{
	writer.StartArray();
	const float* p = glm::value_ptr(m);
	for (int i = 0; i < 16; ++i)
		writer.Double(static_cast<double>(p[i]));
	writer.EndArray();
}

bool ReadMat4ColumnMajor(const rapidjson::Value& arr, mat4& outM)
{
	if (!arr.IsArray() || arr.Size() != 16)
		return false;
	float c[16];
	for (rapidjson::SizeType i = 0; i < 16; ++i)
	{
		if (!arr[i].IsNumber())
			return false;
		c[i] = static_cast<float>(arr[i].GetDouble());
	}
	outM = glm::make_mat4(c);
	return true;
}
} // namespace

bool SaveEditorSceneSnapshot(const std::string& sceneFilePath, const EditorSceneSnapshot& snapshot, std::string* outError)
{
	rapidjson::StringBuffer buffer;
	rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
	writer.SetIndent(' ', 2);
	writer.StartObject();

	writer.Key("format");
	writer.String("kaleido_editor_scene");
	writer.Key("version");
	writer.Uint(5);
	writer.Key("modelPath");
	{
		const std::string serializable = MakeModelPathForSerialization(snapshot.modelPath);
		writer.String(serializable.c_str());
	}

	writer.Key("camera");
	writer.StartObject();
	WriteVec3(writer, "position", snapshot.camera.position);
	WriteVec3(writer, "eulerDegrees", snapshot.camera.eulerDegrees);
	writer.Key("fovY");
	writer.Double(snapshot.camera.fovY);
	writer.Key("znear");
	writer.Double(snapshot.camera.znear);
	writer.Key("moveSpeed");
	writer.Double(snapshot.camera.moveSpeed);
	writer.Key("dollyZoomEnabled");
	writer.Bool(snapshot.camera.dollyZoomEnabled);
	writer.Key("dollyZoomRefDistance");
	writer.Double(snapshot.camera.dollyZoomRefDistance);
	writer.EndObject();

	writer.Key("renderSettings");
	writer.StartObject();
	writer.Key("meshShadingEnabled");
	writer.Bool(snapshot.renderSettings.meshShadingEnabled);
	writer.Key("taskShadingEnabled");
	writer.Bool(snapshot.renderSettings.taskShadingEnabled);
	writer.Key("cullingEnabled");
	writer.Bool(snapshot.renderSettings.cullingEnabled);
	writer.Key("occlusionEnabled");
	writer.Bool(snapshot.renderSettings.occlusionEnabled);
	writer.Key("clusterOcclusionEnabled");
	writer.Bool(snapshot.renderSettings.clusterOcclusionEnabled);
	writer.Key("shadowEnabled");
	writer.Bool(snapshot.renderSettings.shadowEnabled);
	writer.Key("shadowQuality");
	writer.Int(snapshot.renderSettings.shadowQuality);
	writer.Key("shadowBlurEnabled");
	writer.Bool(snapshot.renderSettings.shadowBlurEnabled);
	writer.Key("shadowCheckerboard");
	writer.Bool(snapshot.renderSettings.shadowCheckerboard);
	writer.Key("taaEnabled");
	writer.Bool(snapshot.renderSettings.taaEnabled);
	writer.Key("taaBlendAlpha");
	writer.Double(snapshot.renderSettings.taaBlendAlpha);
	writer.Key("screenSpaceRefractionEnabled");
	writer.Bool(snapshot.renderSettings.screenSpaceRefractionEnabled);
	writer.Key("lodEnabled");
	writer.Bool(snapshot.renderSettings.lodEnabled);
	writer.Key("debugLodStep");
	writer.Int(snapshot.renderSettings.debugLodStep);
	writer.Key("animationEnabled");
	writer.Bool(snapshot.renderSettings.animationEnabled);
	writer.Key("reloadShaders");
	writer.Bool(snapshot.renderSettings.reloadShaders);
	writer.Key("debugGuiMode");
	writer.Int(snapshot.renderSettings.debugGuiMode);
	writer.Key("debugSleep");
	writer.Bool(snapshot.renderSettings.debugSleep);
	writer.Key("gbufferDebugViewMode");
	writer.Int(snapshot.renderSettings.gbufferDebugViewMode);
	writer.Key("clusterRTEnabled");
	writer.Bool(snapshot.renderSettings.clusterRTEnabled);
	writer.EndObject();

	if (snapshot.viewportWidth > 0u && snapshot.viewportHeight > 0u)
	{
		writer.Key("viewport");
		writer.StartObject();
		writer.Key("width");
		writer.Uint(snapshot.viewportWidth);
		writer.Key("height");
		writer.Uint(snapshot.viewportHeight);
		writer.EndObject();
	}

	writer.Key("editorUi");
	writer.StartObject();
	writer.Key("selectedGltfNode");
	if (snapshot.editorUi.selectedGltfNode.has_value())
		writer.Uint(*snapshot.editorUi.selectedGltfNode);
	else
		writer.Null();
	writer.Key("selectionOutlineEnabled");
	writer.Bool(snapshot.editorUi.selectionOutlineEnabled);
	writer.Key("showSelectedSubtreeAabb");
	writer.Bool(snapshot.editorUi.showSelectedSubtreeAabb);
	writer.Key("visualizeRenderGraph");
	writer.Bool(snapshot.editorUi.visualizeRenderGraph);
	writer.Key("renderGraphVisualizerWindowOpen");
	writer.Bool(snapshot.editorUi.renderGraphVisualizerWindowOpen);
	writer.Key("renderGraphVisualizerMode");
	writer.Int(snapshot.editorUi.renderGraphVisualizerMode);
	writer.Key("renderGraphVisualizerImportedPath");
	writer.String(snapshot.editorUi.renderGraphVisualizerImportedPath.c_str());
	writer.EndObject();

	writer.Key("transforms");
	writer.StartObject();
	writer.Key("nodeLocalMatrices");
	writer.StartArray();
	for (const mat4& m : snapshot.transformNodeLocals)
		WriteMat4ColumnMajor(writer, m);
	writer.EndArray();
	writer.EndObject();

	writer.Key("materialOverrides");
	writer.StartArray();
	for (const EditorMaterialOverride& ov : snapshot.materialOverrides)
	{
		writer.StartObject();
		writer.Key("gltfMaterialIndex");
		writer.Uint(ov.gltfMaterialIndex);
		WriteVec4(writer, "baseColorFactor", ov.baseColorFactor);
		WriteVec4(writer, "pbrFactor", ov.pbrFactor);
		WriteVec3(writer, "emissiveFactor", ov.emissiveFactor);
		writer.Key("normalScale");
		writer.Double(ov.normalScale);
		writer.Key("occlusionStrength");
		writer.Double(ov.occlusionStrength);
		writer.Key("alphaMode");
		writer.Int(ov.alphaMode);
		writer.Key("alphaCutoff");
		writer.Double(ov.alphaCutoff);
		writer.Key("doubleSided");
		writer.Bool(ov.doubleSided);
		writer.Key("transmissionFactor");
		writer.Double(ov.transmissionFactor);
		writer.Key("ior");
		writer.Double(ov.ior);
		writer.Key("emissiveStrength");
		writer.Double(ov.emissiveStrength);
		writer.EndObject();
	}
	writer.EndArray();

	writer.EndObject();

	std::ofstream out(sceneFilePath, std::ios::binary | std::ios::trunc);
	if (!out.is_open())
	{
		if (outError)
			*outError = "Failed to open scene file for writing.";
		return false;
	}

	out.write(buffer.GetString(), static_cast<std::streamsize>(buffer.GetSize()));
	if (!out.good())
	{
		if (outError)
			*outError = "Failed to write scene file.";
		return false;
	}

	return true;
}

bool LoadEditorSceneSnapshot(const std::string& sceneFilePath, EditorSceneSnapshot& outSnapshot, std::string* outError)
{
	std::ifstream in(sceneFilePath, std::ios::binary);
	if (!in.is_open())
	{
		if (outError)
			*outError = "Failed to open scene file.";
		return false;
	}

	std::ostringstream content;
	content << in.rdbuf();

	rapidjson::Document document;
	rapidjson::ParseResult parseResult = document.Parse(content.str().c_str());
	if (!parseResult)
	{
		if (outError)
			AppendParseError(*outError, parseResult);
		return false;
	}

	if (!document.IsObject())
	{
		if (outError)
			*outError = "Scene file root must be an object.";
		return false;
	}

	const auto formatIt = document.FindMember("format");
	if (formatIt == document.MemberEnd() || !formatIt->value.IsString() || std::string(formatIt->value.GetString()) != "kaleido_editor_scene")
	{
		if (outError)
			*outError = "Unsupported scene file format.";
		return false;
	}

	const auto modelPathIt = document.FindMember("modelPath");
	if (modelPathIt == document.MemberEnd() || !modelPathIt->value.IsString() || modelPathIt->value.GetStringLength() == 0)
	{
		if (outError)
			*outError = "scene.modelPath is missing or empty.";
		return false;
	}

	EditorSceneSnapshot snapshot{};
	snapshot.modelPath = ResolveModelPath(modelPathIt->value.GetString());

	int sceneVersion = 3;
	const auto versionIt = document.FindMember("version");
	if (versionIt != document.MemberEnd() && versionIt->value.IsInt())
		sceneVersion = versionIt->value.GetInt();

	const auto cameraIt = document.FindMember("camera");
	if (cameraIt != document.MemberEnd() && cameraIt->value.IsObject())
	{
		const rapidjson::Value& camera = cameraIt->value;
		auto positionIt = camera.FindMember("position");
		if (positionIt != camera.MemberEnd())
		{
			if (!ReadVec3(positionIt->value, snapshot.camera.position))
			{
				if (outError)
					*outError = "camera.position must be an array with three numbers.";
				return false;
			}
		}

		auto eulerIt = camera.FindMember("eulerDegrees");
		if (eulerIt != camera.MemberEnd())
		{
			if (!ReadVec3(eulerIt->value, snapshot.camera.eulerDegrees))
			{
				if (outError)
					*outError = "camera.eulerDegrees must be an array with three numbers.";
				return false;
			}
		}

		ReadFloat(camera, "fovY", snapshot.camera.fovY);
		ReadFloat(camera, "znear", snapshot.camera.znear);
		ReadFloat(camera, "moveSpeed", snapshot.camera.moveSpeed);
		ReadBool(camera, "dollyZoomEnabled", snapshot.camera.dollyZoomEnabled);
		ReadFloat(camera, "dollyZoomRefDistance", snapshot.camera.dollyZoomRefDistance);
	}

	const auto renderSettingsIt = document.FindMember("renderSettings");
	if (renderSettingsIt != document.MemberEnd() && renderSettingsIt->value.IsObject())
	{
		const rapidjson::Value& rs = renderSettingsIt->value;
		ReadBool(rs, "meshShadingEnabled", snapshot.renderSettings.meshShadingEnabled);
		ReadBool(rs, "taskShadingEnabled", snapshot.renderSettings.taskShadingEnabled);
		ReadBool(rs, "cullingEnabled", snapshot.renderSettings.cullingEnabled);
		ReadBool(rs, "occlusionEnabled", snapshot.renderSettings.occlusionEnabled);
		ReadBool(rs, "clusterOcclusionEnabled", snapshot.renderSettings.clusterOcclusionEnabled);
		ReadBool(rs, "shadowEnabled", snapshot.renderSettings.shadowEnabled);
		ReadInt(rs, "shadowQuality", snapshot.renderSettings.shadowQuality);
		ReadBool(rs, "shadowBlurEnabled", snapshot.renderSettings.shadowBlurEnabled);
		ReadBool(rs, "shadowCheckerboard", snapshot.renderSettings.shadowCheckerboard);
		ReadBool(rs, "taaEnabled", snapshot.renderSettings.taaEnabled);
		ReadFloat(rs, "taaBlendAlpha", snapshot.renderSettings.taaBlendAlpha);
		ReadBool(rs, "screenSpaceRefractionEnabled", snapshot.renderSettings.screenSpaceRefractionEnabled);
		ReadBool(rs, "lodEnabled", snapshot.renderSettings.lodEnabled);
		ReadInt(rs, "debugLodStep", snapshot.renderSettings.debugLodStep);
		ReadBool(rs, "animationEnabled", snapshot.renderSettings.animationEnabled);
		ReadBool(rs, "reloadShaders", snapshot.renderSettings.reloadShaders);
		ReadInt(rs, "debugGuiMode", snapshot.renderSettings.debugGuiMode);
		ReadBool(rs, "debugSleep", snapshot.renderSettings.debugSleep);
		ReadInt(rs, "gbufferDebugViewMode", snapshot.renderSettings.gbufferDebugViewMode);
		ReadBool(rs, "clusterRTEnabled", snapshot.renderSettings.clusterRTEnabled);
	}

	const auto viewportIt = document.FindMember("viewport");
	if (viewportIt != document.MemberEnd() && viewportIt->value.IsObject())
	{
		const rapidjson::Value& vp = viewportIt->value;
		uint32_t vw = 0;
		uint32_t vh = 0;
		ReadUint32Positive(vp, "width", vw);
		ReadUint32Positive(vp, "height", vh);
		if (vw > 0u && vh > 0u)
		{
			snapshot.viewportWidth = vw;
			snapshot.viewportHeight = vh;
		}
	}

	const auto editorUiIt = document.FindMember("editorUi");
	if (editorUiIt != document.MemberEnd() && editorUiIt->value.IsObject())
	{
		const rapidjson::Value& eu = editorUiIt->value;
		ReadBool(eu, "selectionOutlineEnabled", snapshot.editorUi.selectionOutlineEnabled);
		ReadBool(eu, "showSelectedSubtreeAabb", snapshot.editorUi.showSelectedSubtreeAabb);
		ReadBool(eu, "visualizeRenderGraph", snapshot.editorUi.visualizeRenderGraph);
		ReadBool(eu, "renderGraphVisualizerWindowOpen", snapshot.editorUi.renderGraphVisualizerWindowOpen);
		ReadInt(eu, "renderGraphVisualizerMode", snapshot.editorUi.renderGraphVisualizerMode);
		const auto importedPathIt = eu.FindMember("renderGraphVisualizerImportedPath");
		if (importedPathIt != eu.MemberEnd() && importedPathIt->value.IsString())
			snapshot.editorUi.renderGraphVisualizerImportedPath = importedPathIt->value.GetString();
		const auto selIt = eu.FindMember("selectedGltfNode");
		if (selIt != eu.MemberEnd())
		{
			if (selIt->value.IsNull())
				snapshot.editorUi.selectedGltfNode.reset();
			else if (selIt->value.IsUint())
				snapshot.editorUi.selectedGltfNode = selIt->value.GetUint();
			else if (selIt->value.IsInt() && selIt->value.GetInt() >= 0)
				snapshot.editorUi.selectedGltfNode = static_cast<uint32_t>(selIt->value.GetInt());
		}
	}

	const auto transformsIt = document.FindMember("transforms");
	if (transformsIt != document.MemberEnd() && transformsIt->value.IsObject())
	{
		const rapidjson::Value& tf = transformsIt->value;
		const auto matricesIt = tf.FindMember("nodeLocalMatrices");
		if (matricesIt != tf.MemberEnd() && matricesIt->value.IsArray())
		{
			const rapidjson::Value& matrices = matricesIt->value;
			snapshot.transformNodeLocals.clear();
			snapshot.transformNodeLocals.reserve(matrices.Size());
			for (rapidjson::SizeType i = 0; i < matrices.Size(); ++i)
			{
				mat4 m{};
				if (!ReadMat4ColumnMajor(matrices[i], m))
				{
					if (outError)
						*outError = "transforms.nodeLocalMatrices[" + std::to_string(i) + "] must be an array of 16 numbers (column-major mat4).";
					return false;
				}
				snapshot.transformNodeLocals.push_back(m);
			}
		}
	}

	if (sceneVersion >= 4)
	{
		const auto overridesIt = document.FindMember("materialOverrides");
		if (overridesIt != document.MemberEnd())
		{
			if (!overridesIt->value.IsArray())
			{
				if (outError)
					*outError = "materialOverrides must be an array.";
				return false;
			}

			snapshot.materialOverrides.clear();
			snapshot.materialOverrides.reserve(overridesIt->value.Size());
			for (rapidjson::SizeType i = 0; i < overridesIt->value.Size(); ++i)
			{
				const rapidjson::Value& item = overridesIt->value[i];
				if (!item.IsObject())
				{
					if (outError)
						*outError = "materialOverrides[" + std::to_string(i) + "] must be an object.";
					return false;
				}

				const auto idxIt = item.FindMember("gltfMaterialIndex");
				const auto baseColorIt = item.FindMember("baseColorFactor");
				const auto pbrIt = item.FindMember("pbrFactor");
				const auto emissiveIt = item.FindMember("emissiveFactor");
				if (idxIt == item.MemberEnd() || !idxIt->value.IsUint() ||
				    baseColorIt == item.MemberEnd() || pbrIt == item.MemberEnd() || emissiveIt == item.MemberEnd())
				{
					if (outError)
						*outError = "materialOverrides[" + std::to_string(i) + "] is missing required fields.";
					return false;
				}

				EditorMaterialOverride ov{};
				ov.hasBaseColorFactor = false;
				ov.hasPbrFactor = false;
				ov.hasEmissiveFactor = false;
				ov.hasNormalScale = false;
				ov.hasOcclusionStrength = false;
				ov.hasAlphaMode = false;
				ov.hasAlphaCutoff = false;
				ov.hasDoubleSided = false;
				ov.hasTransmissionFactor = false;
				ov.hasIor = false;
				ov.hasEmissiveStrength = false;
				ov.gltfMaterialIndex = idxIt->value.GetUint();
				if (!ReadVec4(baseColorIt->value, ov.baseColorFactor))
				{
					if (outError)
						*outError = "materialOverrides[" + std::to_string(i) + "].baseColorFactor must be an array with four numbers.";
					return false;
				}
				if (!ReadVec4(pbrIt->value, ov.pbrFactor))
				{
					if (outError)
						*outError = "materialOverrides[" + std::to_string(i) + "].pbrFactor must be an array with four numbers.";
					return false;
				}
				if (!ReadVec3(emissiveIt->value, ov.emissiveFactor))
				{
					if (outError)
						*outError = "materialOverrides[" + std::to_string(i) + "].emissiveFactor must be an array with three numbers.";
					return false;
				}
				ov.hasBaseColorFactor = true;
				ov.hasPbrFactor = true;
				ov.hasEmissiveFactor = true;

				const auto normalScaleIt = item.FindMember("normalScale");
				if (normalScaleIt != item.MemberEnd())
				{
					if (!normalScaleIt->value.IsNumber())
					{
						if (outError)
							*outError = "materialOverrides[" + std::to_string(i) + "].normalScale must be a number.";
						return false;
					}
					ov.normalScale = static_cast<float>(normalScaleIt->value.GetDouble());
					ov.hasNormalScale = true;
				}

				const auto occlusionStrengthIt = item.FindMember("occlusionStrength");
				if (occlusionStrengthIt != item.MemberEnd())
				{
					if (!occlusionStrengthIt->value.IsNumber())
					{
						if (outError)
							*outError = "materialOverrides[" + std::to_string(i) + "].occlusionStrength must be a number.";
						return false;
					}
					ov.occlusionStrength = static_cast<float>(occlusionStrengthIt->value.GetDouble());
					ov.hasOcclusionStrength = true;
				}

				const auto alphaModeIt = item.FindMember("alphaMode");
				if (alphaModeIt != item.MemberEnd())
				{
					if (!alphaModeIt->value.IsInt())
					{
						if (outError)
							*outError = "materialOverrides[" + std::to_string(i) + "].alphaMode must be an integer.";
						return false;
					}
					ov.alphaMode = alphaModeIt->value.GetInt();
					ov.hasAlphaMode = true;
				}

				const auto alphaCutoffIt = item.FindMember("alphaCutoff");
				if (alphaCutoffIt != item.MemberEnd())
				{
					if (!alphaCutoffIt->value.IsNumber())
					{
						if (outError)
							*outError = "materialOverrides[" + std::to_string(i) + "].alphaCutoff must be a number.";
						return false;
					}
					ov.alphaCutoff = static_cast<float>(alphaCutoffIt->value.GetDouble());
					ov.hasAlphaCutoff = true;
				}

				const auto doubleSidedIt = item.FindMember("doubleSided");
				if (doubleSidedIt != item.MemberEnd())
				{
					if (!doubleSidedIt->value.IsBool())
					{
						if (outError)
							*outError = "materialOverrides[" + std::to_string(i) + "].doubleSided must be a boolean.";
						return false;
					}
					ov.doubleSided = doubleSidedIt->value.GetBool();
					ov.hasDoubleSided = true;
				}

				const auto transmissionFactorIt = item.FindMember("transmissionFactor");
				if (transmissionFactorIt != item.MemberEnd())
				{
					if (!transmissionFactorIt->value.IsNumber())
					{
						if (outError)
							*outError = "materialOverrides[" + std::to_string(i) + "].transmissionFactor must be a number.";
						return false;
					}
					ov.transmissionFactor = static_cast<float>(transmissionFactorIt->value.GetDouble());
					ov.hasTransmissionFactor = true;
				}

				const auto iorIt = item.FindMember("ior");
				if (iorIt != item.MemberEnd())
				{
					if (!iorIt->value.IsNumber())
					{
						if (outError)
							*outError = "materialOverrides[" + std::to_string(i) + "].ior must be a number.";
						return false;
					}
					ov.ior = static_cast<float>(iorIt->value.GetDouble());
					ov.hasIor = true;
				}

				const auto emissiveStrengthIt = item.FindMember("emissiveStrength");
				if (emissiveStrengthIt != item.MemberEnd())
				{
					if (!emissiveStrengthIt->value.IsNumber())
					{
						if (outError)
							*outError = "materialOverrides[" + std::to_string(i) + "].emissiveStrength must be a number.";
						return false;
					}
					ov.emissiveStrength = static_cast<float>(emissiveStrengthIt->value.GetDouble());
					ov.hasEmissiveStrength = true;
				}

				snapshot.materialOverrides.push_back(ov);
			}
		}
	}

	outSnapshot = std::move(snapshot);
	return true;
}
