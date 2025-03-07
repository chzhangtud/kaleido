cmake_minimum_required(VERSION 3.10)

project(kaleido VERSION 0.1)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# search vulkan sdk
if (NOT DEFINED ENV{VULKAN_SDK})
    message(FATAL_ERROR "VULKAN_SDK environment variable is not set. Please install Vulkan SDK and set VULKAN_SDK.")
else()
    message(STATUS "Found Vulkan SDK at: $ENV{VULKAN_SDK}")
    set(VULKAN_INCLUDE_DIR "$ENV{VULKAN_SDK}/Include")
    set(VULKAN_LIBRARY "$ENV{VULKAN_SDK}/Lib/vulkan-1.lib")
endif()

# add external libs
add_subdirectory(external/glfw)

# find all glsl files and add them to a folder
file(GLOB GLSL_FILES "${CMAKE_SOURCE_DIR}/src/shaders/*.glsl")
source_group("shaders" FILES ${GLSL_FILES})
add_executable(kaleido src/kaleido.cpp ${GLSL_FILES})

target_include_directories(kaleido PRIVATE ${VULKAN_INCLUDE_DIR})

if (WIN32)
    target_compile_definitions(kaleido PRIVATE VK_USE_PLATFORM_WIN32_KHR
                                                GLFW_EXPOSE_NATIVE_WIN32
                                                NOMINMAX)
    message(STATUS "Windows platform detected: VK_USE_PLATFORM_WIN32_KHR defined.")
else()
    # unsupported platform
endif()

target_link_libraries(kaleido PRIVATE glfw ${VULKAN_LIBRARY})

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(kaleido PRIVATE -D_DEBUG) # enable debug mode
    message(STATUS "Debug mode enabled: assert is active.")
else()
    target_compile_definitions(kaleido PRIVATE -DNDEBUG) # disable assert
    message(STATUS "Release mode enabled: assert is disabled.")
endif()

# copy assets
set(SHADERS_SOURCE_DIR "${CMAKE_SOURCE_DIR}/src/shaders")
set(SHADERS_DEST_DIR "${CMAKE_BINARY_DIR}/shaders")

add_custom_target(copy_shaders ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${SHADERS_SOURCE_DIR} ${SHADERS_DEST_DIR}
    COMMENT "Copying shaders folder to build directory at runtime"
)

add_dependencies(kaleido copy_shaders)
