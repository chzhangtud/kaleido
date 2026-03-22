# kaleido
This repo is a personal learning record of Niagara's streaming vulkan tutorials from Zeux
https://github.com/zeux/niagara

Besides the content from Niagara, some additional features are continuely added, such as building on Android, more rendering demos etc.

This readme is still working in progress.

# Prerequisites
- CMake >=3.22.1
- Vulkan >=1.3

For Windows
- Visual Studio

For Android
- Android Studio


# Clone
```bash
git clone https://github.com/chzhangtud/kaleido.git --recursive
# Or
git clone https://github.com/chzhangtud/kaleido.git
git submodule update --init --recursive
```

# Build
## Windows
```bash
mkdir build && cd build
# Debug:
cmake -DCMAKE_BUILD_TYPE=Debug ..
# Release:
cmake -DCMAKE_BUILD_TYPE=Release ..
```

## Android
Open KaleidoAndroid using Android Studio
Currently users need to copy shaders and model assets into assets folder.
- Copy "shaders" including spv files to KaleidoAndroid/app/src/main/assets
- Copy model assets into KaleidoAndroid/app/src/main/assets

# Run
## Windows
``` bash
kaleido.exe -h
kaleido.exe xxx.gltf
```

### RenderGraph barrier debug (cross-platform)
You can print auto-generated RenderGraph barriers by setting `RG_BARRIER_DEBUG=1`.

- Windows (PowerShell):
```powershell
$env:RG_BARRIER_DEBUG="1"
.\build\Debug\kaleido.exe
```
- Linux/macOS (bash/zsh):
```bash
RG_BARRIER_DEBUG=1 ./build/kaleido
```

Set `RG_BARRIER_DEBUG=0` (or unset it) to disable logging.

## Android
Launch app.

# Others
clang formatting:
``` bash
# install with powershell
winget install LLVM.LLVM

# validate the installation
clang-format --version

# create a .clang-format file and then use clang formatting
clang-format -i src/*.cpp src/*.h
```

For Windows, custom build command for glsl shaders using glslangValidator in visual studio:
$(VULKAN_SDK)\Bin\glslangValidator --target-env vulkan1.3 %(FullPath) -V -o %(RootDir)%(Directory)\%(Filename).spv

flags influencing pipeline:
- push descriptor flag