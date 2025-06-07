cmake_minimum_required(VERSION 3.10)

project(kaleido VERSION 0.1)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# add external libs
add_subdirectory(external/glfw)
add_subdirectory(external/meshoptimizer)
add_subdirectory(external/volk)
add_subdirectory(external/zeux_objparser)
add_subdirectory(external/glm)
add_subdirectory(external/fast_obj)

if (MSVC)
    set_property(TARGET glfw PROPERTY FOLDER "external")
    set_property(TARGET volk PROPERTY FOLDER "external")
    set_property(TARGET meshoptimizer PROPERTY FOLDER "external")
    set_property(TARGET zeux_objparser PROPERTY FOLDER "external")
    set_property(TARGET glm PROPERTY FOLDER "external")
    set_property(TARGET fast_obj PROPERTY FOLDER "external")
endif()

# find all glsl files and add them to a folder
file(GLOB GLSL_FILES "${CMAKE_SOURCE_DIR}/src/shaders/*.glsl" "${CMAKE_SOURCE_DIR}/src/shaders/*.h")
source_group("shaders" FILES ${GLSL_FILES})

file(GLOB_RECURSE SRC_FILES "src/*.cpp" "src/*.h")
list(FILTER SRC_FILES EXCLUDE REGEX "src/shaders/.*")
add_executable(kaleido ${SRC_FILES} ${GLSL_FILES})

# target_include_directories(kaleido PRIVATE ${VULKAN_INCLUDE_DIR})

if (WIN32)
    target_compile_definitions(kaleido PRIVATE VK_USE_PLATFORM_WIN32_KHR
                                                GLFW_EXPOSE_NATIVE_WIN32
                                                NOMINMAX)
    message(STATUS "Windows platform detected: VK_USE_PLATFORM_WIN32_KHR defined.")
else()
    # unsupported platform
endif()

target_link_libraries(kaleido PRIVATE   glfw
                                        volk
                                        meshoptimizer
                                        zeux_objparser
                                        glm
                                        fast_obj)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type (Debug/Release)" FORCE)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(kaleido PRIVATE -D_DEBUG)  # enable debug mode
    message(STATUS "Debug mode enabled: assert is active.")
else()
    target_compile_definitions(kaleido PRIVATE -DNDEBUG)  # disable assert()
    message(STATUS "Release mode enabled: assert is disabled.")
endif()

# copy assets
set(SHADERS_SOURCE_DIR "${CMAKE_SOURCE_DIR}/src/shaders")
set(SHADERS_DEST_DIR "${CMAKE_BINARY_DIR}/shaders")

file(GLOB SPV_FILES "${SHADERS_SOURCE_DIR}/*.spv")

add_custom_target(copy_shaders ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADERS_DEST_DIR}
    COMMENT "Copying .spv files to build directory"
)

foreach(SPV_FILE ${SPV_FILES})
    add_custom_command(TARGET copy_shaders PRE_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SPV_FILE} ${SHADERS_DEST_DIR}
        COMMENT "Copying ${SPV_FILE} to ${SHADERS_DEST_DIR}"
    )
endforeach()

add_dependencies(kaleido copy_shaders)

