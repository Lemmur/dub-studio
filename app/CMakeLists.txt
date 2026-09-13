# UI-слой: MainWindow + модели таблиц (Qt Widgets).

add_library(dubstudio_ui STATIC
  mainwindow.cpp
  linesmodel.cpp
)

target_include_directories(dubstudio_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(dubstudio_ui
  PUBLIC
    Qt6::Widgets
    dubstudio_core
)

add_executable(DubStudio WIN32 main.cpp)
target_link_libraries(DubStudio PRIVATE dubstudio_ui)
