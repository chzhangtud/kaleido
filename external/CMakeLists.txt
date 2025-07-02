set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

add_subdirectory(glfw)
add_subdirectory(volk)
add_subdirectory(meshoptimizer)
add_subdirectory(zeux_objparser)
add_subdirectory(glm)
add_subdirectory(fast_obj)

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

if (MSVC)
    set_property(TARGET glfw PROPERTY FOLDER "external")
    set_property(TARGET volk PROPERTY FOLDER "external")
    set_property(TARGET meshoptimizer PROPERTY FOLDER "external")
    set_property(TARGET zeux_objparser PROPERTY FOLDER "external")
    set_property(TARGET glm PROPERTY FOLDER "external")
    set_property(TARGET fast_obj PROPERTY FOLDER "external")
    set_property(TARGET cgltf PROPERTY FOLDER "external")
endif()

