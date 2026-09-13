# Ядро DubStudio: Database + Importer (без Qt, только C++20 + SQLite).

add_library(dubstudio_core STATIC
  project/database.cpp
  project/importer.cpp
)

target_include_directories(dubstudio_core
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/nlohmann
    ${CMAKE_BINARY_DIR}/generated
)

target_link_libraries(dubstudio_core PUBLIC sqlite3)
