// DubStudio — обёртка SQLite (WAL) с авто-применением schema.sql.
// Фаза 0, PLAN.md раздел 5.1.
#pragma once

#include <sqlite3.h>

#include <cstdint>
#include <string>

namespace dubstudio {

class Database {
public:
    // Открывает базу (создаёт при необходимости) и применяет схему.
    // Кидаёт std::runtime_error с текстом sqlite3_errmsg при ошибке.
    explicit Database(const std::string& path);
    ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    sqlite3* handle() const noexcept { return db_; }

    // Выполнить произвольный (в т.ч. многострочный) SQL без результатов.
    void exec(const std::string& sql);

    // Первый столбец первой строки результата (для COUNT(*) и PRAGMA).
    std::string scalarText(const std::string& sql);
    std::int64_t scalarInt(const std::string& sql);

    // Текущий режим журнала (ожидаем "wal" после открытия).
    std::string journalMode();

private:
    void applySchema();

    sqlite3* db_ = nullptr;
};

} // namespace dubstudio
