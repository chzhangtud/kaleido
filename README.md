# kaleido

Personal learning project based on [Zeux’s Niagara streaming Vulkan tutorials](https://github.com/zeux/niagara). The codebase extends that material with extra experiments—Android builds, additional rendering paths, and editor-style tooling.

## Screenshots

### Windows (`kaleido_standalone`)

**Chess scene — debug split view.** Wireframe, fully shaded (materials and shadows), and cluster / meshlet visualization in one viewport; left panel is the **Kaleido editor** (rendering and scene options).

![Windows: chess scene with wireframe, shaded, and cluster visualization](imgs/windows_demo_01.png)

**Street scene — lit shading.** Large glTF environment (cobblestones, buildings, shadows). Sidebar shows camera values, loaded asset path, and toggles for mesh/task shading, culling, TAA, refraction, and LoD.

![Windows: cobblestone street scene with editor UI](imgs/windows_demo_02.png)

### Android

![Android: street scene with performance monitor and global settings](imgs/android_demo_01.jpg)

**Android build** running the same style of content on device: **Performance Monitor** (frame rate, Settings / Profiling / Scene tabs) and **Global Settings** for culling, shadows, TAA, refraction, LoD, and debug modes. Mesh shading features may be unavailable depending on GPU capabilities (see on-screen notes such as wireframe / fill mode support).

---

## Prerequisites

- CMake ≥ 3.22.1  
- Vulkan ≥ 1.3  

**Windows**

- Visual Studio  

**Android**

- Android Studio  

## Clone

```bash
git clone https://github.com/chzhangtud/kaleido.git --recursive
# Or
git clone https://github.com/chzhangtud/kaleido.git
git submodule update --init --recursive
```

## Build

### Windows

```bash
mkdir build && cd build
# Debug:
cmake -DCMAKE_BUILD_TYPE=Debug ..
# Release:
cmake -DCMAKE_BUILD_TYPE=Release ..
```

Then build `kaleido_standalone` (and related targets) from Visual Studio or CMake.

### External glTF assets (desktop / editor)

Large models are kept **outside** this repository (for example next to the clone). Editor scene files under `testcases/` store `modelPath` as a **path relative to the assets root**, not as machine-specific absolute paths.

**Assets root resolution (first match wins):**

1. Environment variable `KALEIDO_ASSETS_ROOT` — must point to a directory that contains asset folders (e.g. `ABeautifulGame/ABeautifulGame.gltf` under that root).
2. If unset, the runtime falls back to **`<repository>/../assets`** (sibling folder named `assets` next to the kaleido repo).

Example layout:

```text
CMakeRepos/
  kaleido/          <- this repository (CMAKE_SOURCE_DIR)
  assets/           <- default assets root: ../assets from the repo
    ABeautifulGame/
      ABeautifulGame.gltf
```

**PowerShell (optional override):**

```powershell
$env:KALEIDO_ASSETS_ROOT="D:\path\to\your\assets"
```

### Android

Open the `KaleidoAndroid` project in Android Studio.

You currently need to copy **shaders** (including `.spv`) and **model assets** into the app assets tree:

- Copy the `shaders` folder (with SPIR-V) to `KaleidoAndroid/app/src/main/assets/`
- Copy model assets into `KaleidoAndroid/app/src/main/assets/`

## Run

### Windows

```bash
kaleido_standalone.exe -h
kaleido_standalone.exe path\to\scene.gltf
```

#### Editor automation (`kaleido_editor`)

`kaleido_editor` supports viewport dump and RenderGraph snapshot export for regression automation:

```powershell
.\build\Debug\kaleido_editor.exe "testcases\ABeautifulGame\scene.json" `
  --auto-dump-exr "$env:TEMP\kaleido_view.png" `
  --auto-dump-rendergraph-json "$env:TEMP\kaleido_rg.json" `
  --auto-dump-rendergraph-dot "$env:TEMP\kaleido_rg.dot" `
  --auto-dump-frames 64 `
  --auto-dump-rendergraph-frames 64
```

Editor UI also provides a `Visualize RenderGraph` toggle in the `Rendering` panel and a `RenderGraph Visualizer` window with Live/Imported mode, DOT/JSON export, and JSON import.

#### Shader Graph viewport regression

```powershell
.\build\Debug\kaleido_editor.exe "testcases\ABeautifulGame_ShaderGraph_TimeNoise\scene.json" `
  --auto-dump-exr "$env:TEMP\shader_graph_time_noise.png" `
  --auto-dump-frames 64

python .cursor/skills/kaleido-viewport-image-test/scripts/compare_viewport_pngs.py `
  "testcases\ABeautifulGame_ShaderGraph_TimeNoise\scene.png" `
  "$env:TEMP\shader_graph_time_noise.png"
```

#### RenderGraph barrier debug (cross-platform)

Set `RG_BARRIER_DEBUG=1` to print auto-generated RenderGraph barriers.

**PowerShell**

```powershell
$env:RG_BARRIER_DEBUG="1"
.\build\Debug\kaleido_standalone.exe
```

**bash / zsh**

```bash
RG_BARRIER_DEBUG=1 ./build/kaleido_standalone
```

Set `RG_BARRIER_DEBUG=0` or unset the variable to disable.

To register extra external images for the render graph (beyond the swapchain), extend `VulkanContext::PrepareRenderGraphPassContext`, or call `ClearRenderGraphExternalImages()` then `RegisterRenderGraphExternalImage(name, vkImage, format, usage)` before `RenderGraph::execute`. The map key is `name`; the value stores `{ image, format, usage }`.

### Android

Build and launch the app from Android Studio.
