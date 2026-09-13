# Ядро DubStudio: Database + Importer + Audio (без Qt, только C++20 + SQLite + RtAudio).

add_library(dubstudio_core STATIC
  project/database.cpp
  project/importer.cpp
  project/edit_stack.cpp
  audio/audio_engine.cpp
  audio/clip_store.cpp
  audio/audio_ops.cpp
  audio/wav_writer.cpp
)

target_include_directories(dubstudio_core
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/dr_wav
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/nlohmann
    ${CMAKE_BINARY_DIR}/generated
)

target_link_libraries(dubstudio_core
  PUBLIC
    sqlite3
    rtaudio
)
