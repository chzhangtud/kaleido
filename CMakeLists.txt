cmake_minimum_required(VERSION 3.10)

project(kaleido VERSION 0.1)

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

source_group("shaders" FILES ${GLSL_FILES} ${GLSL_HEADER_FILES})

file(GLOB_RECURSE SRC_FILES "src/*.cpp" "src/*.h")
list(FILTER SRC_FILES EXCLUDE REGEX "src/shaders/.*")
if(WIN32)
    add_executable(kaleido ${SRC_FILES} ${GLSL_FILES} ${GLSL_HEADER_FILES})
elseif(ANDROID)
    add_library(kaleido SHARED ${SRC_FILES} ${GLSL_FILES} ${GLSL_HEADER_FILES})
endif()


if (WIN32)
    target_compile_definitions(kaleido PRIVATE VK_USE_PLATFORM_WIN32_KHR
                                                GLFW_EXPOSE_NATIVE_WIN32
                                                NOMINMAX)
    message(STATUS "Windows platform detected: VK_USE_PLATFORM_WIN32_KHR defined.")
    target_include_directories(kaleido PRIVATE ${Vulkan_INCLUDE_DIRS}/spirv_cross)
elseif(ANDROID)
    target_compile_definitions(kaleido PRIVATE 
        VK_USE_PLATFORM_ANDROID_KHR
        ANDROID
    )
    message(STATUS "Android platform detected: VK_USE_PLATFORM_ANDROID_KHR defined.")
endif()

# ------------------ link libraries ------------------
if (WIN32)
    target_link_libraries(kaleido PRIVATE 
        glfw
        volk
        meshoptimizer
        zeux_objparser
        glm
        fast_obj
        cgltf
        stb_image
        ktx
        imgui
    )
elseif(ANDROID)
    target_link_libraries(kaleido PRIVATE
        volk
        meshoptimizer
        zeux_objparser
        glm
        fast_obj
        cgltf
        stb_image
        ktx
        imgui
#        spirv_cross_core
        spirv-cross-c
        android
        log
    )
endif()

# ------------------ Build Type ------------------
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type (Debug/Release)" FORCE)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(kaleido PRIVATE -D_DEBUG)
    message(STATUS "Debug mode enabled: assert is active.")
else()
    target_compile_definitions(kaleido PRIVATE -DNDEBUG)
    message(STATUS "Release mode enabled: assert is disabled.")
endif()

target_compile_definitions(kaleido PRIVATE
    GLFW_INCLUDE_NONE
    GLM_FORCE_XYZW_ONLY
    GLM_FORCE_QUAT_DATA_XYZW
    GLM_FORCE_QUAT_CTOR_XYZW
)

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
        COMMAND ${GLSLANG_VALIDATOR} --target-env vulkan1.3 ${GLSL_FILE} -V -o ${SPV_FILE}
        DEPENDS ${GLSL_FILE}
        COMMENT "Compiling shader ${GLSL_FILE} -> ${SPV_FILE}"
        VERBATIM
    )
endforeach()

add_custom_target(CompileShaders ALL DEPENDS ${SPV_FILES})

add_dependencies(kaleido CompileShaders)
