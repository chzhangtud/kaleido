add_executable(transform_graph_smoke transform_graph_smoke.cpp)
target_link_libraries(transform_graph_smoke PRIVATE glm)
add_test(NAME transform_graph_smoke COMMAND transform_graph_smoke)

add_executable(editor_scene_io_smoke
    editor_scene_io_smoke.cpp
    ${CMAKE_SOURCE_DIR}/src/editor_scene_io.cpp
)
target_link_libraries(editor_scene_io_smoke PRIVATE glm rapidjson)
if (WIN32)
    target_compile_definitions(editor_scene_io_smoke PRIVATE WIN32 NOMINMAX)
endif()
add_test(NAME editor_scene_io_smoke COMMAND editor_scene_io_smoke)
