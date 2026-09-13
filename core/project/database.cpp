#include "dubstudio/database.h"

#include <algorithm>
#include <stdexcept>
#include <vector>

// Сгенерировано CMake из schema.sql (корень репо).
#include "dubstudio/schema.h"

namespace dubstudio {
namespace {

[[noreturn]] void throwSqlite(sqlite3* db, const std::string& what) {
    throw std::runtime_error(what + ": " + (db ? sqlite3_errmsg(db) : "sqlite3 == nullptr"));
}

} // namespace

Database::Database(const std::string& path) {
    const int rc = sqlite3_open_v2(path.c_str(), &db_,
                                   SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
    if (rc != SQLITE_OK) {
        const std::string msg = db_ ? sqlite3_errmsg(db_) : "open failed";
        if (db_) sqlite3_close(db_);
        db_ = nullptr;
        throw std::runtime_error("Не удалось открыть базу '" + path + "': " + msg);
    }

    // Режимы из PLAN.md раздел 5.1 (schema.sql задаёт то же самое, но
    // synchronous — настройка соединения, дублируем на каждом открытии).
    sqlite3_exec(db_, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
    sqlite3_exec(db_, "PRAGMA synchronous=NORMAL;", nullptr, nullptr, nullptr);
    sqlite3_exec(db_, "PRAGMA foreign_keys=ON;", nullptr, nullptr, nullptr);

    applySchema();
}

Database::~Database() {
    if (db_) sqlite3_close(db_);
}

void Database::exec(const std::string& sql) {
    char* err = nullptr;
    if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
        const std::string msg = err ? err : "unknown error";
        sqlite3_free(err);
        throw std::runtime_error("SQL ошибка: " + msg + "\nSQL: " + sql);
    }
}

std::string Database::scalarText(const std::string& sql) {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
        throwSqlite(db_, "scalarText: prepare");
    std::string result;
    if (sqlite3_step(stmt) == SQLITE_ROW && sqlite3_column_count(stmt) > 0) {
        const unsigned char* text = sqlite3_column_text(stmt, 0);
        if (text) result.assign(reinterpret_cast<const char*>(text));
    }
    sqlite3_finalize(stmt);
    return result;
}

std::int64_t Database::scalarInt(const std::string& sql) {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
        throwSqlite(db_, "scalarInt: prepare");
    std::int64_t result = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW)
        result = sqlite3_column_int64(stmt, 0);
    sqlite3_finalize(stmt);
    return result;
}

std::string Database::journalMode() {
    return scalarText("PRAGMA journal_mode;");
}

void Database::applySchema() {
    try {
        exec(kSchemaSql);
    } catch (const std::runtime_error& e) {
        throw std::runtime_error(std::string("Не удалось применить schema.sql: ") + e.what());
    }
    migrateUndoLog();
}

// Фаза 2: undo_log получает scope/take_id/state_json (schema.sql уже создаёт
// их для новых БД; существующие — наращиваем ALTER-ом, sqlite3_exec на
// многострочном SQL останавливается на первой ошибке, поэтому по одному).
void Database::migrateUndoLog() {
    std::vector<std::string> have;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, "PRAGMA table_info(undo_log);", -1, &stmt, nullptr) ==
        SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char* name = sqlite3_column_text(stmt, 1);
            if (name) have.emplace_back(reinterpret_cast<const char*>(name));
        }
        sqlite3_finalize(stmt);
    }
    auto has = [&have](const char* col) {
        return std::find(have.begin(), have.end(), col) != have.end();
    };
    struct Col { const char* name; const char* ddl; };
    const Col cols[] = {
        {"scope", "ALTER TABLE undo_log ADD COLUMN scope TEXT DEFAULT '';"},
        {"take_id", "ALTER TABLE undo_log ADD COLUMN take_id TEXT DEFAULT '';"},
        {"state_json", "ALTER TABLE undo_log ADD COLUMN state_json TEXT DEFAULT '';"},
    };
    for (const auto& c : cols) {
        if (!has(c.name)) {
            char* err = nullptr;
            if (sqlite3_exec(db_, c.ddl, nullptr, nullptr, &err) != SQLITE_OK) {
                const std::string msg = err ? err : "unknown";
                sqlite3_free(err);
                throw std::runtime_error(std::string("Миграция undo_log: ") + msg);
            }
        }
    }
}

} // namespace dubstudio
