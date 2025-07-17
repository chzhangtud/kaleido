set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

add_subdirectory(glfw)
add_subdirectory(volk)
add_subdirectory(meshoptimizer)
add_subdirectory(zeux_objparser)
add_subdirectory(glm)
add_subdirectory(fast_obj)
add_subdirectory(ktx_software)

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

# glfw settings
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

# KTX settings
set(KTX_FEATURE_TESTS OFF CACHE BOOL "" FORCE)
set(KTX_FEATURE_ETC_UNPACK OFF CACHE BOOL "" FORCE)

if (MSVC)
    set_property(TARGET glfw PROPERTY FOLDER "external/glfw")
    set_property(TARGET update_mappings PROPERTY FOLDER "external/glfw")
    set_property(TARGET volk PROPERTY FOLDER "external")
    set_property(TARGET meshoptimizer PROPERTY FOLDER "external")
    set_property(TARGET zeux_objparser PROPERTY FOLDER "external")
    set_property(TARGET glm PROPERTY FOLDER "external")
    set_property(TARGET fast_obj_lib PROPERTY FOLDER "external")
    set_property(TARGET cgltf PROPERTY FOLDER "external")
    set_property(TARGET ktx PROPERTY FOLDER "external/ktx")
    set_property(TARGET astcenc-avx2-static PROPERTY FOLDER "external/ktx")
    set_property(TARGET ktx_read PROPERTY FOLDER "external/ktx")
    set_property(TARGET ktx_version PROPERTY FOLDER "external/ktx")
    set_property(TARGET obj_basisu_cbind PROPERTY FOLDER "external/ktx")
    set_property(TARGET objUtil PROPERTY FOLDER "external/ktx")
endif()
