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

# --- RtAudio 6.0.1 (vendored) + ASIO-заголовки из комплекта ------------------
# В third_party/rtaudio/rtaudio/include лежат заголовки ASIO 2.3 — внешний
# ASIO SDK для сборки не нужен (см. docs/plans/phase-1-audio.md).
# WASAPI собран как fallback (PLAN.md 6.1).
add_library(rtaudio STATIC
  third_party/rtaudio/rtaudio/RtAudio.cpp
  third_party/rtaudio/rtaudio/include/asio.cpp
  third_party/rtaudio/rtaudio/include/asiodrivers.cpp
  third_party/rtaudio/rtaudio/include/asiolist.cpp
  third_party/rtaudio/rtaudio/include/iasiothiscallresolver.cpp
)
target_include_directories(rtaudio
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/rtaudio/rtaudio
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/rtaudio/rtaudio/include
)
target_compile_definitions(rtaudio PRIVATE
  __WINDOWS_ASIO__     # ASIO приоритетно
  __WINDOWS_WASAPI__   # fallback (PLAN.md 6.1)
)
target_link_libraries(rtaudio
  PUBLIC winmm ole32 oleaut32 uuid ksuser mfplat mfuuid wmcodecdspuuid
)
# Линкуем большие obj в отдельные секции, чтобы не раздувать exe.
if(MSVC)
  target_compile_options(rtaudio PRIVATE /bigobj)
endif()

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
