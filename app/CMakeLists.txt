# GUI RuDub Studio (Widgets, тёмная тема, интерфейс только на русском)
qt_add_executable(rudub WIN32
  main.cpp
)
target_link_libraries(rudub PRIVATE
  rudub_project
  rudub_audio
  rudub_text
  Qt6::Widgets
  Qt6::Sql
  Qt6::Network
)

# Консольная проверка среды: версии Qt/компилятора, драйверы SQL, плагины
qt_add_executable(rudub_env_check
  env_check.cpp
)
target_link_libraries(rudub_env_check PRIVATE
  rudub_project
  Qt6::Core
  Qt6::Sql
)
