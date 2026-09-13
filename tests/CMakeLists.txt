# Тесты RuDub (Catch2 через vcpkg): D:\dev\vcpkg\vcpkg install catch2:x64-windows
# Сборка: cmake --preset debug -DRUDUB_BUILD_TESTS=ON && cmake --build --preset debug && ctest --preset debug

find_package(Catch2 QUIET)
if(NOT Catch2_FOUND)
  message(STATUS "Catch2 не найден — тесты пропускаются (vcpkg install catch2:x64-windows)")
  return()
endif()

add_executable(rudub_tests smoke.cpp)
target_link_libraries(rudub_tests PRIVATE Catch2::Catch2WithMain rudub_project)

add_test(NAME smoke COMMAND rudub_tests)
