#include "editor_scene_io.h"

#include <fstream>
#include <sstream>

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
	writer.Uint(1);
	writer.Key("modelPath");
	writer.String(snapshot.modelPath.c_str());

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
	snapshot.modelPath = modelPathIt->value.GetString();

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

	outSnapshot = std::move(snapshot);
	return true;
}
