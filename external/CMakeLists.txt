if (WIN32)
# glfw settings
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

add_subdirectory(glfw)
endif()

# KTX settings
set(KTX_FEATURE_TOOLS OFF CACHE BOOL "" FORCE)
set(KTX_FEATURE_TESTS OFF CACHE BOOL "" FORCE)
set(KTX_FEATURE_DOC OFF CACHE BOOL "" FORCE)
set(KTX_FEATURE_LOADTEST OFF CACHE BOOL "" FORCE)
set(KTX_FEATURE_GL_UPLOAD OFF CACHE BOOL "" FORCE)
set(KTX_FEATURE_ETC_UNPACK OFF CACHE BOOL "" FORCE)

add_subdirectory(volk)
add_subdirectory(meshoptimizer)
add_subdirectory(zeux_objparser)
add_subdirectory(glm)
add_subdirectory(fast_obj)
add_subdirectory(ktx_software)

if (ANDROID)
# spriv-cross settings
set(SPIRV_CROSS_EXCEPTIONS_TO_ASSERTIONS OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_SHARED OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_STATIC ON CACHE BOOL "" FORCE)
set(SPIRV_CROSS_CLI OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_TESTS OFF CACHE BOOL "" FORCE)

set(SPIRV_CROSS_ENABLE_GLSL ON CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_HLSL OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_MSL OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_CPP OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_REFLECT ON CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_C_API ON CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_UTIL OFF CACHE BOOL "" FORCE)

set(SPIRV_CROSS_SANITIZE_ADDRESS OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_SANITIZE_MEMORY OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_SANITIZE_THREADS OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_SANITIZE_UNDEFINED OFF CACHE BOOL "" FORCE)

set(SPIRV_CROSS_NAMESPACE_OVERRIDE "" CACHE STRING "" FORCE)
set(SPIRV_CROSS_FORCE_STL_TYPES OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_SKIP_INSTALL OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_WERROR OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_MISC_WARNINGS OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_FORCE_PIC OFF CACHE BOOL "" FORCE)

add_subdirectory(spirv_cross)
endif()

add_library(cgltf INTERFACE)
target_include_directories(cgltf INTERFACE ${CMAKE_CURRENT_LIST_DIR}/cgltf)
target_sources(cgltf INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/cgltf/cgltf.h
    ${CMAKE_CURRENT_LIST_DIR}/cgltf/cgltf_write.h
)
source_group("Header Files\\cgltf" FILES
    ${CMAKE_CURRENT_LIST_DIR}/cgltf/cgltf.h
    ${CMAKE_CURRENT_LIST_DIR}/cgltf/cgltf_write.h
)

add_library(stb_image INTERFACE)
target_include_directories(stb_image INTERFACE ${CMAKE_CURRENT_LIST_DIR}/stb)
target_sources(cgltf INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/stb/stb_image.h
)
source_group("Header Files\\stb" FILES
    ${CMAKE_CURRENT_LIST_DIR}/stb/stb_image.h
)

# ImGui - standalone external project
add_library(imgui STATIC)

if (WIN32)
target_sources(imgui PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/imgui/imgui.cpp
    ${CMAKE_CURRENT_LIST_DIR}/imgui/imgui_draw.cpp
    ${CMAKE_CURRENT_LIST_DIR}/imgui/imgui_tables.cpp
    ${CMAKE_CURRENT_LIST_DIR}/imgui/imgui_widgets.cpp
    ${CMAKE_CURRENT_LIST_DIR}/imgui/backends/imgui_impl_glfw.cpp
    ${CMAKE_CURRENT_LIST_DIR}/imgui/backends/imgui_impl_vulkan.cpp
)
elseif(ANDROID)
target_sources(imgui PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/imgui/imgui.cpp
    ${CMAKE_CURRENT_LIST_DIR}/imgui/imgui_draw.cpp
    ${CMAKE_CURRENT_LIST_DIR}/imgui/imgui_tables.cpp
    ${CMAKE_CURRENT_LIST_DIR}/imgui/imgui_widgets.cpp
    ${CMAKE_CURRENT_LIST_DIR}/imgui/backends/imgui_impl_android.cpp
    ${CMAKE_CURRENT_LIST_DIR}/imgui/backends/imgui_impl_vulkan.cpp
)    
endif()

target_include_directories(imgui PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/imgui
    ${CMAKE_CURRENT_LIST_DIR}/imgui/backends
)

if (WIN32)
target_link_libraries(imgui PUBLIC glfw volk)
endif()

source_group("imgui" FILES
    ${CMAKE_CURRENT_LIST_DIR}/imgui/imgui.cpp
    ${CMAKE_CURRENT_LIST_DIR}/imgui/imgui_draw.cpp
    ${CMAKE_CURRENT_LIST_DIR}/imgui/imgui_tables.cpp
    ${CMAKE_CURRENT_LIST_DIR}/imgui/imgui_widgets.cpp
    ${CMAKE_CURRENT_LIST_DIR}/imgui/imgui_demo.cpp
)
source_group("imgui\\backends" FILES
if (WIN32)
    ${CMAKE_CURRENT_LIST_DIR}/imgui/backends/imgui_impl_glfw.cpp
endif()
    ${CMAKE_CURRENT_LIST_DIR}/imgui/backends/imgui_impl_vulkan.cpp
)
target_compile_definitions(imgui PRIVATE VK_NO_PROTOTYPES) # for avoid conflict when using volk

if (MSVC)
    set_property(TARGET glfw PROPERTY FOLDER "external/glfw")
    set_property(TARGET update_mappings PROPERTY FOLDER "external/glfw")
    set_property(TARGET volk PROPERTY FOLDER "external")
    set_property(TARGET meshoptimizer PROPERTY FOLDER "external")
    set_property(TARGET zeux_objparser PROPERTY FOLDER "external")
    set_property(TARGET glm PROPERTY FOLDER "external")
    set_property(TARGET fast_obj_lib PROPERTY FOLDER "external")
    set_property(TARGET cgltf PROPERTY FOLDER "external")
    set_property(TARGET stb_image PROPERTY FOLDER "external")
    set_property(TARGET ktx PROPERTY FOLDER "external/ktx")
    set_property(TARGET astcenc-avx2-static PROPERTY FOLDER "external/ktx")
    set_property(TARGET ktx_read PROPERTY FOLDER "external/ktx")
    set_property(TARGET ktx_version PROPERTY FOLDER "external/ktx")
    set_property(TARGET obj_basisu_cbind PROPERTY FOLDER "external/ktx")
    set_property(TARGET objUtil PROPERTY FOLDER "external/ktx")
    set_property(TARGET imgui PROPERTY FOLDER "external")
endif()