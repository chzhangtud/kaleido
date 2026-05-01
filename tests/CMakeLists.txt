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

add_executable(shader_graph_editor_session_tests
    shader_graph_editor_session_tests.cpp
    ${CMAKE_SOURCE_DIR}/src/shader_graph_editor_session.cpp
    ${CMAKE_SOURCE_DIR}/src/shader_graph_compile_report.cpp
)
target_link_libraries(shader_graph_editor_session_tests PRIVATE glm rapidjson)
add_test(NAME shader_graph_editor_session_tests COMMAND shader_graph_editor_session_tests)

add_executable(shader_graph_edge_delete_tests
    shader_graph_edge_delete_tests.cpp
)
target_link_libraries(shader_graph_edge_delete_tests PRIVATE glm rapidjson)
add_test(NAME shader_graph_edge_delete_tests COMMAND shader_graph_edge_delete_tests)

add_executable(shader_reload_policy_tests
    shader_reload_policy_tests.cpp
)
target_link_libraries(shader_reload_policy_tests PRIVATE glm rapidjson)
add_test(NAME shader_reload_policy_tests COMMAND shader_reload_policy_tests)

add_executable(shader_graph_reachability_compile_tests
    shader_graph_reachability_compile_tests.cpp
    ${CMAKE_SOURCE_DIR}/src/shader_graph_types.cpp
    ${CMAKE_SOURCE_DIR}/src/shader_graph_validate.cpp
    ${CMAKE_SOURCE_DIR}/src/shader_graph_codegen_glsl.cpp
)
target_link_libraries(shader_graph_reachability_compile_tests PRIVATE glm rapidjson)
add_test(NAME shader_graph_reachability_compile_tests COMMAND shader_graph_reachability_compile_tests)

add_executable(shader_graph_output_rules_tests
    shader_graph_output_rules_tests.cpp
    ${CMAKE_SOURCE_DIR}/src/shader_graph_types.cpp
    ${CMAKE_SOURCE_DIR}/src/shader_graph_validate.cpp
    ${CMAKE_SOURCE_DIR}/src/shader_graph_codegen_glsl.cpp
)
target_link_libraries(shader_graph_output_rules_tests PRIVATE glm rapidjson)
add_test(NAME shader_graph_output_rules_tests COMMAND shader_graph_output_rules_tests)

add_executable(shader_graph_type_link_tests
    shader_graph_type_link_tests.cpp
    ${CMAKE_SOURCE_DIR}/src/shader_graph_types.cpp
)
target_link_libraries(shader_graph_type_link_tests PRIVATE glm rapidjson)
add_test(NAME shader_graph_type_link_tests COMMAND shader_graph_type_link_tests)

add_executable(graph_canvas_input_tests
    graph_canvas_input_tests.cpp
    ${CMAKE_SOURCE_DIR}/src/graph_canvas_input.cpp
)
target_link_libraries(graph_canvas_input_tests PRIVATE glm rapidjson imgui imnodes)
add_test(NAME graph_canvas_input_tests COMMAND graph_canvas_input_tests)

set(_kaleido_test_targets
    transform_graph_smoke
    editor_scene_io_smoke
    shader_graph_validate_tests
    shader_graph_io_tests
    shader_graph_codegen_tests
    shader_graph_editor_session_tests
    shader_graph_edge_delete_tests
    shader_reload_policy_tests
    shader_graph_reachability_compile_tests
    shader_graph_output_rules_tests
    shader_graph_type_link_tests
    graph_canvas_input_tests
)
foreach(_t IN LISTS _kaleido_test_targets)
    set_property(TARGET ${_t} PROPERTY FOLDER "tests")
endforeach()
