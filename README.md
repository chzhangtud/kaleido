# kaleido

For Windows, custom build command for glsl shaders using glslangValidator in visual studio:
$(VULKAN_SDK)\Bin\glslangValidator --target-env vulkan1.3 %(FullPath) -V -o %(RootDir)%(Directory)\%(Filename).spv

TODO:
1. import imgui to add checkboxes for switching different options and display performance profiling