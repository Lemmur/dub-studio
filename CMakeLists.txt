cmake_minimum_required(VERSION 3.22) # ENVIRONMENT_MODIFICATION в тестах

project(DubStudio
  VERSION 0.1.0
  DESCRIPTION "DubStudio — DAW для сольного дубляжа игр EN->RU"
  LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  # UTF-8: в исходниках русский UI-текст (AGENTS.md, правило 1).
  add_compile_options(/utf-8 /permissive- /Zc:preprocessor)
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 6.5 COMPONENTS Widgets REQUIRED)

# --- SQLite (amalgamation, vendored) -----------------------------------------
add_library(sqlite3 STATIC third_party/sqlite/sqlite3.c)
target_include_directories(sqlite3 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/sqlite)
target_compile_definitions(sqlite3 PRIVATE
  SQLITE_ENABLE_FTS5          # lines_fts (PLAN.md, раздел 5.1)
  SQLITE_ENABLE_MATH_FUNCTIONS
  SQLITE_OMIT_DEPRECATED
)

# --- Встраивание schema.sql в бинарник ---------------------------------------
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/schema.sql DUBSTUDIO_SCHEMA_SQL)
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/schema.h.in
  ${CMAKE_CURRENT_BINARY_DIR}/generated/dubstudio/schema.h
  @ONLY)

add_subdirectory(core)
add_subdirectory(app)

enable_testing()
add_subdirectory(tests)
