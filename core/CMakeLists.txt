# Ядро RuDub: доменные модули L2 (слои по PLAN.md, раздел 3).
# Интерфейсы живут в include/rudub/, реализации — в core/<модуль>/.

set(RUDUB_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/include)

# Проект/БД/импортёр — наполняется в Фазе 0 (schema.sql, combined.json)
add_library(rudub_project STATIC project/project_placeholder.cpp)
target_include_directories(rudub_project PUBLIC ${RUDUB_INCLUDE_DIR})
target_link_libraries(rudub_project PUBLIC Qt6::Core Qt6::Sql)

# Аудио-ядро — RtAudio/ASIO, запись, волноформа (Фаза 1)
add_library(rudub_audio STATIC audio/audio_placeholder.cpp)
target_include_directories(rudub_audio PUBLIC ${RUDUB_INCLUDE_DIR})
target_link_libraries(rudub_audio PUBLIC Qt6::Core)

# Текст, слоги, липсинк — LLM-адаптация (Фаза 5)
add_library(rudub_text STATIC text/text_placeholder.cpp)
target_include_directories(rudub_text PUBLIC ${RUDUB_INCLUDE_DIR})
target_link_libraries(rudub_text PUBLIC Qt6::Core)
