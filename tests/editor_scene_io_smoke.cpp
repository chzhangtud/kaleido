#include "../src/editor_scene_io.h"

#include <cstdio>
#include <cmath>
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
	ov.normalScale = 2.5f;
	ov.occlusionStrength = 0.4f;
	ov.alphaMode = 1;
	ov.alphaCutoff = 0.35f;
	ov.doubleSided = true;
	ov.transmissionFactor = 0.6f;
	ov.ior = 1.9f;
	ov.emissiveStrength = 4.2f;
	snapshot.materialOverrides.push_back(ov);

	std::string error;
	if (!SaveEditorSceneSnapshot(v4Path.string(), snapshot, &error))
	{
		fprintf(stderr, "save v4 failed: %s\n", error.c_str());
		return 1;
	}

	std::ifstream saved(v4Path, std::ios::binary);
	std::string savedText((std::istreambuf_iterator<char>(saved)), std::istreambuf_iterator<char>());
	if (!ContainsText(savedText, "\"version\": 5") || !ContainsText(savedText, "\"materialOverrides\"") ||
	    !ContainsText(savedText, "\"normalScale\"") || !ContainsText(savedText, "\"doubleSided\"") ||
	    !ContainsText(savedText, "\"transmissionFactor\"") || !ContainsText(savedText, "\"ior\""))
	{
		fprintf(stderr, "v5 json missing required fields\n");
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
	if (!ContainsText(savedText, "\"emissiveStrength\""))
	{
		fprintf(stderr, "v5 json missing emissiveStrength\n");
		return 8;
	}
	const EditorMaterialOverride& loadedOv = loaded.materialOverrides[0];
	if (!loadedOv.hasNormalScale || !loadedOv.hasOcclusionStrength || !loadedOv.hasAlphaMode || !loadedOv.hasAlphaCutoff ||
	    !loadedOv.hasDoubleSided || !loadedOv.hasTransmissionFactor || !loadedOv.hasIor || !loadedOv.hasEmissiveStrength)
	{
		fprintf(stderr, "v5 override missing parsed fields\n");
		return 9;
	}
	if (loadedOv.alphaMode != 1 || !loadedOv.doubleSided)
	{
		fprintf(stderr, "v5 override key fields mismatch\n");
		return 10;
	}
	if (fabsf(loadedOv.normalScale - 2.5f) > 1e-5f || fabsf(loadedOv.ior - 1.9f) > 1e-5f)
	{
		fprintf(stderr, "v5 override scalar fields mismatch\n");
		return 11;
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

	const fs::path v4CompatPath = tempRoot / "scene_v4_compat.json";
	const char* v4CompatJson = R"({
  "format": "kaleido_editor_scene",
  "version": 4,
  "modelPath": "demo.glb",
  "camera": {},
  "renderSettings": {},
  "editorUi": {},
  "transforms": { "nodeLocalMatrices": [] },
  "materialOverrides": [
    {
      "gltfMaterialIndex": 1,
      "baseColorFactor": [1, 1, 1, 1],
      "pbrFactor": [1, 1, 0.5, 0.5],
      "emissiveFactor": [0, 0, 0]
    }
  ]
})";
	std::ofstream v4Compat(v4CompatPath, std::ios::binary | std::ios::trunc);
	v4Compat << v4CompatJson;
	v4Compat.close();

	EditorSceneSnapshot v4Loaded{};
	if (!LoadEditorSceneSnapshot(v4CompatPath.string(), v4Loaded, &error))
	{
		fprintf(stderr, "load v4 compat failed: %s\n", error.c_str());
		return 12;
	}
	if (v4Loaded.materialOverrides.size() != 1u)
	{
		fprintf(stderr, "v4 compat should keep one override\n");
		return 13;
	}
	const EditorMaterialOverride& v4Ov = v4Loaded.materialOverrides[0];
	if (v4Ov.hasNormalScale || v4Ov.hasTransmissionFactor || v4Ov.hasIor || v4Ov.hasEmissiveStrength)
	{
		fprintf(stderr, "v4 compat should not set v5-only fields\n");
		return 14;
	}

	return 0;
}
