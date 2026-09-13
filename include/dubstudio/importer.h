// DubStudio — импортёр combined.json (4 уровня, PLAN.md раздел 4):
//   {file_id: {quest_id: {guid: {en, ru, speaker_name, speaker_internal, dur}}}}
// Порядок ключей JSON = порядок в сцене → nlohmann::ordered_json.
#pragma once

#include "dubstudio/database.h"

#include <cstdint>
#include <string>

namespace dubstudio {

struct ImportStats {
    std::int64_t files = 0;   // строк в game_files после импорта
    std::int64_t quests = 0;  // строк в quests после импорта
    std::int64_t lines = 0;   // строк в lines после импорта
    std::int64_t ftsRows = 0; // строк в lines_fts после пересборки
    double elapsedMs = 0.0;   // полное время importFile, мс
};

class Importer {
public:
    // Батч строк на одну транзакцию (PLAN.md раздел 5.2).
    static constexpr int kBatchSize = 1000;

    explicit Importer(Database& db) : db_(db) {}

    // Импортирует файл, кидает std::runtime_error при ошибке парсинга/БД.
    ImportStats importFile(const std::string& jsonPath);

private:
    Database& db_;
};

} // namespace dubstudio
