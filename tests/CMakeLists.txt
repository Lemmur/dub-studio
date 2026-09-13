# Catch2 (amalgamated) — тесты готовности фаз (PLAN.md, раздел 14).

add_executable(dubstudio_tests
  third_party/catch2/catch_amalgamated.cpp
  test_database.cpp
  test_importer.cpp
  test_linesmodel.cpp
  test_audio.cpp
  test_editing.cpp
)

target_include_directories(dubstudio_tests
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/catch2
    ${CMAKE_CURRENT_SOURCE_DIR}/../app
)

target_link_libraries(dubstudio_tests
  PRIVATE
    dubstudio_core
    dubstudio_ui
    Qt6::Widgets
)

add_test(NAME dubstudio_tests COMMAND dubstudio_tests)

# Qt DLL лежат в bin Qt, добавляем их в PATH теста (иначе 0xc0000135).
set_tests_properties(dubstudio_tests PROPERTIES
  ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
