#include "../src/editor_scene_io.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

const std::string& GetAssetsRoot()
{
	static const std::string kEmpty;
	return kEmpty;
}

std::string ResolveModelPath(const std::string& rawModelPath)
{
	return rawModelPath;
}

std::string MakeModelPathForSerialization(const std::string& modelPath)
{
	return modelPath;
}

static bool ContainsText(const std::string& haystack, const char* needle)
{
	return haystack.find(needle) != std::string::npos;
}

int main()
{
	const fs::path tempRoot = fs::temp_directory_path() / "kaleido_editor_scene_io_smoke";
	fs::create_directories(tempRoot);
	const fs::path v4Path = tempRoot / "scene_v4.json";
	const fs::path v3Path = tempRoot / "scene_v3.json";

	EditorSceneSnapshot snapshot{};
	snapshot.modelPath = (tempRoot / "demo.glb").string();
	snapshot.camera.position = vec3(1.f, 2.f, 3.f);
	snapshot.camera.eulerDegrees = vec3(10.f, 20.f, 30.f);
	snapshot.camera.fovY = 1.23f;
	snapshot.camera.znear = 0.2f;
	snapshot.camera.moveSpeed = 4.f;
	snapshot.renderSettings.taaEnabled = false;
	snapshot.transformNodeLocals.push_back(mat4(1.f));

	EditorMaterialOverride ov{};
	ov.gltfMaterialIndex = 2u;
	ov.baseColorFactor = vec4(0.1f, 0.2f, 0.3f, 0.4f);
	ov.pbrFactor = vec4(0.5f, 0.6f, 0.7f, 0.8f);
	ov.emissiveFactor = vec3(0.9f, 0.8f, 0.7f);
	snapshot.materialOverrides.push_back(ov);

	std::string error;
	if (!SaveEditorSceneSnapshot(v4Path.string(), snapshot, &error))
	{
		fprintf(stderr, "save v4 failed: %s\n", error.c_str());
		return 1;
	}

	std::ifstream saved(v4Path, std::ios::binary);
	std::string savedText((std::istreambuf_iterator<char>(saved)), std::istreambuf_iterator<char>());
	if (!ContainsText(savedText, "\"version\": 4") || !ContainsText(savedText, "\"materialOverrides\""))
	{
		fprintf(stderr, "v4 json missing required fields\n");
		return 2;
	}

	EditorSceneSnapshot loaded{};
	if (!LoadEditorSceneSnapshot(v4Path.string(), loaded, &error))
	{
		fprintf(stderr, "load v4 failed: %s\n", error.c_str());
		return 3;
	}
	if (loaded.materialOverrides.size() != 1u)
	{
		fprintf(stderr, "expected one material override\n");
		return 4;
	}
	if (loaded.materialOverrides[0].gltfMaterialIndex != 2u)
	{
		fprintf(stderr, "material override index mismatch\n");
		return 5;
	}

	// v3 compatibility: no materialOverrides field should still parse.
	const char* v3Json = R"({
  "format": "kaleido_editor_scene",
  "version": 3,
  "modelPath": "demo.glb",
  "camera": {},
  "renderSettings": {},
  "editorUi": {
    "selectedGltfNode": null,
    "selectionOutlineEnabled": false,
    "showSelectedSubtreeAabb": false
  },
  "transforms": { "nodeLocalMatrices": [] }
})";
	std::ofstream legacy(v3Path, std::ios::binary | std::ios::trunc);
	legacy << v3Json;
	legacy.close();

	EditorSceneSnapshot legacyLoaded{};
	if (!LoadEditorSceneSnapshot(v3Path.string(), legacyLoaded, &error))
	{
		fprintf(stderr, "load v3 failed: %s\n", error.c_str());
		return 6;
	}
	if (!legacyLoaded.materialOverrides.empty())
	{
		fprintf(stderr, "v3 should not produce material overrides\n");
		return 7;
	}

	return 0;
}
