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

add_executable(shader_graph_validate_tests
    shader_graph_validate_tests.cpp
    ${CMAKE_SOURCE_DIR}/src/shader_graph_types.cpp
    ${CMAKE_SOURCE_DIR}/src/shader_graph_validate.cpp
)
target_link_libraries(shader_graph_validate_tests PRIVATE glm rapidjson)
add_test(NAME shader_graph_validate_tests COMMAND shader_graph_validate_tests)

add_executable(shader_graph_io_tests
    shader_graph_io_tests.cpp
    ${CMAKE_SOURCE_DIR}/src/shader_graph_types.cpp
    ${CMAKE_SOURCE_DIR}/src/shader_graph_io.cpp
)
target_link_libraries(shader_graph_io_tests PRIVATE glm rapidjson)
add_test(NAME shader_graph_io_tests COMMAND shader_graph_io_tests)

add_executable(shader_graph_codegen_tests
    shader_graph_codegen_tests.cpp
    ${CMAKE_SOURCE_DIR}/src/shader_graph_types.cpp
    ${CMAKE_SOURCE_DIR}/src/shader_graph_validate.cpp
    ${CMAKE_SOURCE_DIR}/src/shader_graph_codegen_glsl.cpp
)
target_link_libraries(shader_graph_codegen_tests PRIVATE glm rapidjson)
add_test(NAME shader_graph_codegen_tests COMMAND shader_graph_codegen_tests)
