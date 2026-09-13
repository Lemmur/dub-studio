cmake_minimum_required(VERSION 3.24)

project(RuDubStudio
  VERSION 0.1.0
  DESCRIPTION "RuDub Studio — DAW для сольного дубляжа игр EN->RU"
  LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Qt: автоматические moc/uic/rcc
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets Sql Network)

add_subdirectory(core)
add_subdirectory(app)

# Тесты (Catch2): -DRUDUB_BUILD_TESTS=ON
option(RUDUB_BUILD_TESTS "Собирать тесты (Catch2)" OFF)
if(RUDUB_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
