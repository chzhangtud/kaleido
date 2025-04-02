# kaleido

For Windows, custom build command for glsl shaders using glslangValidator in visual studio:
$(VULKAN_SDK)\Bin\glslangValidator --target-env vulkan1.3 %(FullPath) -V -o %(RootDir)%(Directory)\%(Filename).spv