cmake_minimum_required(VERSION 3.10)

project(kaleido VERSION 0.1)
option(SHADER_DEBUG "Embed debug information into SPIV shaders" OFF)
option(BUILD_KALEIDO_EDITOR "Build desktop kaleido_editor launcher skeleton" ON)

if (WIN32)
if(NOT DEFINED ENV{VULKAN_SDK})
    message(FATAL_ERROR "VULKAN_SDK environment variable is not set. Please install Vulkan SDK.")
endif()
set(VULKAN_SDK $ENV{VULKAN_SDK})
message(STATUS "Found VULKAN_SDK: ${VULKAN_SDK}")
set(Vulkan_INCLUDE_DIRS "${VULKAN_SDK}/Include")
message(STATUS "Vulkan include dirs: ${Vulkan_INCLUDE_DIRS}")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(BUILD_TESTING OFF)

add_subdirectory(external)

file(GLOB GLSL_FILES "${CMAKE_SOURCE_DIR}/src/shaders/*.glsl")
file(GLOB GLSL_HEADER_FILES "${CMAKE_SOURCE_DIR}/src/shaders/*.h")

file(GLOB_RECURSE KALEIDO_ALL_CPP "src/*.cpp")
file(GLOB_RECURSE KALEIDO_ALL_HEADERS "src/*.h")
list(FILTER KALEIDO_ALL_CPP EXCLUDE REGEX "src/shaders/.*")
list(FILTER KALEIDO_ALL_HEADERS EXCLUDE REGEX "src/shaders/.*")

set(KALEIDO_ENTRY_SOURCE "${CMAKE_SOURCE_DIR}/src/kaleido.cpp")
set(KALEIDO_EDITOR_ENTRY_SOURCE "${CMAKE_SOURCE_DIR}/src/kaleido_editor.cpp")
set(KALEIDO_RUNTIME_CORE_SOURCES ${KALEIDO_ALL_CPP})
list(REMOVE_ITEM KALEIDO_RUNTIME_CORE_SOURCES ${KALEIDO_ENTRY_SOURCE})
list(REMOVE_ITEM KALEIDO_RUNTIME_CORE_SOURCES ${KALEIDO_EDITOR_ENTRY_SOURCE})

# Shared runtime core for standalone kaleido and future kaleido_editor launcher.
# Shaders live here too (shared by all launchers); they are IDE-only sources, compiled by CompileShaders.
add_library(kaleido_runtime_core OBJECT
    ${KALEIDO_RUNTIME_CORE_SOURCES}
    ${KALEIDO_ALL_HEADERS}
    ${GLSL_FILES}
    ${GLSL_HEADER_FILES}
)

set_source_files_properties(${GLSL_FILES} ${GLSL_HEADER_FILES} PROPERTIES HEADER_FILE_ONLY TRUE)

source_group("shaders" FILES ${GLSL_FILES} ${GLSL_HEADER_FILES})
source_group(TREE "${CMAKE_SOURCE_DIR}/src" PREFIX "src"
    FILES ${KALEIDO_RUNTIME_CORE_SOURCES} ${KALEIDO_ALL_HEADERS} ${KALEIDO_ENTRY_SOURCE} ${KALEIDO_EDITOR_ENTRY_SOURCE})

if(WIN32)
    add_executable(kaleido_standalone
        ${KALEIDO_ENTRY_SOURCE}
        $<TARGET_OBJECTS:kaleido_runtime_core>
    )
elseif(ANDROID)
    add_library(kaleido_standalone SHARED
        ${KALEIDO_ENTRY_SOURCE}
        $<TARGET_OBJECTS:kaleido_runtime_core>
    )
endif()

if(WIN32 AND BUILD_KALEIDO_EDITOR)
    add_executable(kaleido_editor
        ${KALEIDO_EDITOR_ENTRY_SOURCE}
        $<TARGET_OBJECTS:kaleido_runtime_core>
    )
endif()

set(KALEIDO_COMMON_DEFINITIONS
    GLFW_INCLUDE_NONE
    GLM_FORCE_XYZW_ONLY
    GLM_FORCE_QUAT_DATA_XYZW
    GLM_FORCE_QUAT_CTOR_XYZW
)

set(KALEIDO_LINK_LIBS
    volk
    meshoptimizer
    zeux_objparser
    glm
    fast_obj
    cgltf
    stb_image
    ktx
    imgui
    rapidjson
)

if(ANDROID)
    list(APPEND KALEIDO_LINK_LIBS
        spirv-cross-c
        android
        log
    )
endif()

if (WIN32)
    target_compile_definitions(kaleido_runtime_core PRIVATE VK_USE_PLATFORM_WIN32_KHR
                                                            GLFW_EXPOSE_NATIVE_WIN32
                                                            NOMINMAX)
    target_compile_definitions(kaleido_standalone PRIVATE VK_USE_PLATFORM_WIN32_KHR
                                                          GLFW_EXPOSE_NATIVE_WIN32
                                                          NOMINMAX)
    if(TARGET kaleido_editor)
        target_compile_definitions(kaleido_editor PRIVATE VK_USE_PLATFORM_WIN32_KHR
                                                          GLFW_EXPOSE_NATIVE_WIN32
                                                          NOMINMAX)
    endif()
    message(STATUS "Windows platform detected: VK_USE_PLATFORM_WIN32_KHR defined.")
    target_include_directories(kaleido_runtime_core PRIVATE ${Vulkan_INCLUDE_DIRS}/spirv_cross)
    target_include_directories(kaleido_standalone PRIVATE ${Vulkan_INCLUDE_DIRS}/spirv_cross)
    if(TARGET kaleido_editor)
        target_include_directories(kaleido_editor PRIVATE ${Vulkan_INCLUDE_DIRS}/spirv_cross)
    endif()
elseif(ANDROID)
    target_compile_definitions(kaleido_runtime_core PRIVATE
        VK_USE_PLATFORM_ANDROID_KHR
        ANDROID
    )
    target_compile_definitions(kaleido_standalone PRIVATE
        VK_USE_PLATFORM_ANDROID_KHR
        ANDROID
    )
    message(STATUS "Android platform detected: VK_USE_PLATFORM_ANDROID_KHR defined.")
endif()

# ------------------ link libraries ------------------
if (WIN32)
    target_link_libraries(kaleido_runtime_core PRIVATE glfw ${KALEIDO_LINK_LIBS})
    target_link_libraries(kaleido_standalone PRIVATE glfw ${KALEIDO_LINK_LIBS})
    if(TARGET kaleido_editor)
        target_link_libraries(kaleido_editor PRIVATE glfw ${KALEIDO_LINK_LIBS})
    endif()
elseif(ANDROID)
    target_link_libraries(kaleido_runtime_core PRIVATE ${KALEIDO_LINK_LIBS})
    target_link_libraries(kaleido_standalone PRIVATE ${KALEIDO_LINK_LIBS})
endif()

# ------------------ Build Type ------------------
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type (Debug/Release)" FORCE)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(kaleido_runtime_core PRIVATE -D_DEBUG)
    target_compile_definitions(kaleido_standalone PRIVATE -D_DEBUG)
    if(TARGET kaleido_editor)
        target_compile_definitions(kaleido_editor PRIVATE -D_DEBUG)
    endif()
    message(STATUS "Debug mode enabled: assert is active.")
else()
    target_compile_definitions(kaleido_runtime_core PRIVATE -DNDEBUG)
    target_compile_definitions(kaleido_standalone PRIVATE -DNDEBUG)
    if(TARGET kaleido_editor)
        target_compile_definitions(kaleido_editor PRIVATE -DNDEBUG)
    endif()
    message(STATUS "Release mode enabled: assert is disabled.")
endif()

target_compile_definitions(kaleido_runtime_core PRIVATE ${KALEIDO_COMMON_DEFINITIONS})
target_compile_definitions(kaleido_standalone PRIVATE ${KALEIDO_COMMON_DEFINITIONS})
if(TARGET kaleido_editor)
    target_compile_definitions(kaleido_editor PRIVATE ${KALEIDO_COMMON_DEFINITIONS})
endif()

set(SHADERS_SOURCE_DIR "${CMAKE_SOURCE_DIR}/src/shaders")
if(WIN32)
    set(SHADERS_DEST_DIR "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/shaders")
elseif(ANDROID)
    set(SHADERS_DEST_DIR "${CMAKE_SOURCE_DIR}/KaleidoAndroid/app/src/main/assets/shaders")
endif()

set(SPV_FILES)
if(WIN32)
    if(DEFINED VULKAN_SDK)
        set(GLSLANG_VALIDATOR "${VULKAN_SDK}/Bin/glslangValidator.exe")
    else()
        message(FATAL_ERROR "VULKAN_SDK environment variable not set!")
    endif()
elseif(ANDROID)
    find_program(GLSLANG_VALIDATOR NAMES glslangValidator glslc HINTS $ENV{ANDROID_NDK}/shader-tools)
    if(NOT GLSLANG_VALIDATOR)
        message(FATAL_ERROR "glslangValidator or glslc not found! Please install Vulkan SDK or Android shader tools.")
    endif()
else()
    set(GLSLANG_VALIDATOR "glslangValidator")
endif()

file(MAKE_DIRECTORY ${SHADERS_DEST_DIR})

set(GLSL_FLAGS --target-env vulkan1.3)

if (SHADER_DEBUG)
  list(APPEND GLSL_FLAGS -gVS)
endif()

foreach(GLSL_FILE IN LISTS GLSL_FILES)
    file(RELATIVE_PATH REL_PATH "${SHADERS_SOURCE_DIR}" ${GLSL_FILE})

    string(REPLACE "/" "_" SAFE_NAME "${REL_PATH}")
    string(REPLACE "\\" "_" SAFE_NAME "${SAFE_NAME}")
    string(REGEX REPLACE "\\.glsl$" "" BASE_NAME "${SAFE_NAME}")

    set(SPV_FILE "${SHADERS_DEST_DIR}/${BASE_NAME}.spv")
    list(APPEND SPV_FILES ${SPV_FILE})

    add_custom_command(
        OUTPUT ${SPV_FILE}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADERS_DEST_DIR}
        COMMAND ${GLSLANG_VALIDATOR} ${GLSL_FLAGS} ${GLSL_FILE} -V -o ${SPV_FILE}
        DEPENDS ${GLSL_FILE}
        COMMENT "Compiling shader ${GLSL_FILE} -> ${SPV_FILE}"
        VERBATIM
    )
endforeach()

add_custom_target(CompileShaders ALL DEPENDS ${SPV_FILES})

add_dependencies(kaleido_runtime_core CompileShaders)
