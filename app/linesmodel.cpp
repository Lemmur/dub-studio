#include "linesmodel.h"

#include <sqlite3.h>

#include <QColor>
#include <QRegularExpression>

namespace dubstudio {
namespace {

QString columnText(sqlite3_stmt* stmt, int col) {
    const unsigned char* text = sqlite3_column_text(stmt, col);
    return text ? QString::fromUtf8(reinterpret_cast<const char*>(text)) : QString();
}

// FTS5 по умолчанию матчит только целые токены: "Coe" не найдёт "Coen".
// Превращаем пользовательский ввод в префиксный запрос по каждому слову:
//   "coe look"      -> "coe* look*"
//   "speaker_name:Lunka" -> "speaker_name:Lunka*"
// Фразы в кавычках, операторы AND/OR/NOT и слова, уже заканчивающиеся на *,
// проходят без изменений.
QString toFtsPrefixExpr(const QString& raw) {
    static const QRegularExpression ws(QStringLiteral("\\s+"));
    const QStringList parts = raw.split(ws, Qt::SkipEmptyParts);
    QStringList out;
    out.reserve(parts.size());
    for (const QString& part : parts) {
        QString p = part;
        if (p.endsWith(QLatin1Char('*'))) {
            out << p; // уже префиксный
        } else if (p.startsWith(QLatin1Char('"'))) {
            out << p; // фраза — как есть
        } else if (p.compare(QLatin1String("AND"), Qt::CaseInsensitive) == 0 ||
                   p.compare(QLatin1String("OR"), Qt::CaseInsensitive) == 0 ||
                   p.compare(QLatin1String("NOT"), Qt::CaseInsensitive) == 0) {
            out << p; // оператор FTS
        } else if (const int colon = p.indexOf(QLatin1Char(':')); colon >= 0) {
            out << p.left(colon + 1) + p.mid(colon + 1) + QLatin1Char('*');
        } else {
            out << p + QLatin1Char('*');
        }
    }
    return out.join(QLatin1Char(' '));
}

} // namespace

LinesSqlModel::LinesSqlModel(Database& db, QObject* parent)
    : QAbstractTableModel(parent), db_(db) {}

void LinesSqlModel::setFilter(const QString& fileId, const QString& questId,
                              const QString& ftsQuery) {
    fileId_ = fileId;
    questId_ = questId;
    ftsQuery_ = toFtsPrefixExpr(ftsQuery.trimmed());
    refresh();
}

void LinesSqlModel::refresh() {
    beginResetModel();
    rows_.clear();
    recountTotal();
    if (total_ > 0) fetchPage(qMin<qint64>(kPageSize, total_));
    endResetModel();
}

void LinesSqlModel::recountTotal() {
    // JOIN всегда одинаковый, различаются только WHERE-условия.
    QString sql = "SELECT COUNT(*) FROM lines l";
    if (!ftsQuery_.isEmpty()) sql += " JOIN lines_fts fts ON fts.wem_hash = l.wem_hash";
    sql += " WHERE 1=1";
    std::vector<QString> binds;
    if (!questId_.isEmpty()) { sql += " AND l.quest_id = ?"; binds.push_back(questId_); }
    if (!fileId_.isEmpty())  { sql += " AND l.file_id = ?";  binds.push_back(fileId_); }
    if (!ftsQuery_.isEmpty()) { sql += " AND lines_fts MATCH ?"; binds.push_back(ftsQuery_); }

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_.handle(), sql.toUtf8().constData(), -1, &stmt, nullptr) != SQLITE_OK)
        return;
    for (int i = 0; i < static_cast<int>(binds.size()); ++i)
        sqlite3_bind_text(stmt, i + 1, binds[i].toUtf8().constData(), -1, SQLITE_TRANSIENT);
    total_ = (sqlite3_step(stmt) == SQLITE_ROW) ? sqlite3_column_int64(stmt, 0) : 0;
    sqlite3_finalize(stmt);
}

void LinesSqlModel::fetchPage(int limit) {
    // line_pk возрастает в порядке импорта = порядок файл->сцена->реплика.
    QString sql =
        "SELECT l.wem_hash, l.status, l.speaker_name, t.en_original,"
        " COALESCE(NULLIF(t.ru_final,''), t.ru_adapted) AS ru, l.ref_duration_ms"
        " FROM lines l JOIN texts t ON t.wem_hash = l.wem_hash";
    if (!ftsQuery_.isEmpty()) sql += " JOIN lines_fts fts ON fts.wem_hash = l.wem_hash";
    sql += " WHERE 1=1";
    std::vector<QString> binds;
    if (!questId_.isEmpty()) { sql += " AND l.quest_id = ?"; binds.push_back(questId_); }
    if (!fileId_.isEmpty())  { sql += " AND l.file_id = ?";  binds.push_back(fileId_); }
    if (!ftsQuery_.isEmpty()) { sql += " AND lines_fts MATCH ?"; binds.push_back(ftsQuery_); }
    sql += " ORDER BY l.line_pk LIMIT ? OFFSET ?";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_.handle(), sql.toUtf8().constData(), -1, &stmt, nullptr) != SQLITE_OK)
        return;
    for (int i = 0; i < static_cast<int>(binds.size()); ++i)
        sqlite3_bind_text(stmt, i + 1, binds[i].toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, static_cast<int>(binds.size()) + 1, limit);
    sqlite3_bind_int64(stmt, static_cast<int>(binds.size()) + 2, static_cast<qint64>(rows_.size()));

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Row row;
        row.wemHash = columnText(stmt, 0);
        row.status = columnText(stmt, 1);
        row.speaker = columnText(stmt, 2);
        row.en = columnText(stmt, 3);
        row.ru = columnText(stmt, 4);
        row.durMs = sqlite3_column_int64(stmt, 5);
        rows_.push_back(std::move(row));
    }
    sqlite3_finalize(stmt);
}

int LinesSqlModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(rows_.size());
}

int LinesSqlModel::columnCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : ColCount;
}

QString LinesSqlModel::statusRu(const QString& status) const {
    if (status == QLatin1String("todo")) return QStringLiteral("К работе");
    if (status == QLatin1String("recorded")) return QStringLiteral("Записано");
    if (status == QLatin1String("in_review")) return QStringLiteral("На проверке");
    if (status == QLatin1String("done")) return QStringLiteral("Готово");
    return status;
}

QVariant LinesSqlModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= static_cast<int>(rows_.size())) return {};
    const Row& row = rows_[static_cast<size_t>(index.row())];

    if (role == Qt::DisplayRole || role == Qt::ToolTipRole) {
        switch (index.column()) {
            case ColStatus: return statusRu(row.status);
            case ColSpeaker: return row.speaker;
            case ColEn: return row.en;
            case ColRu: return row.ru;
            case ColDur: {
                const double sec = static_cast<double>(row.durMs) / 1000.0;
                return QString::number(sec, 'f', 2) + QStringLiteral(" с");
            }
            default: break;
        }
    } else if (role == Qt::ForegroundRole && index.column() == ColStatus) {
        // Подсказка цветом: серый «К работе», жёлтый «На проверке», зелёный «Готово».
        if (row.status == QLatin1String("done")) return QColor(110, 200, 120);
        if (row.status == QLatin1String("in_review")) return QColor(220, 190, 90);
        if (row.status == QLatin1String("todo")) return QColor(170, 170, 170);
        return QColor(230, 140, 120); // recorded
    }
    return {};
}

QVariant LinesSqlModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal) return {};
    switch (section) {
        case ColStatus: return QStringLiteral("Статус");
        case ColSpeaker: return QStringLiteral("Спикер");
        case ColEn: return QStringLiteral("EN");
        case ColRu: return QStringLiteral("RU");
        case ColDur: return QStringLiteral("Длит.");
        default: return {};
    }
}

bool LinesSqlModel::canFetchMore(const QModelIndex& parent) const {
    return !parent.isValid() && static_cast<qint64>(rows_.size()) < total_;
}

void LinesSqlModel::fetchMore(const QModelIndex& parent) {
    if (parent.isValid()) return;
    const qint64 remainder = total_ - static_cast<qint64>(rows_.size());
    const int itemsToFetch = static_cast<int>(qMin<qint64>(kPageSize, remainder));
    if (itemsToFetch <= 0) return;
    const int from = static_cast<int>(rows_.size());
    beginInsertRows(QModelIndex(), from, from + itemsToFetch - 1);
    fetchPage(itemsToFetch);
    endInsertRows();
}

QString LinesSqlModel::wemHashAt(int row) const {
    return (row >= 0 && row < static_cast<int>(rows_.size()))
               ? rows_[static_cast<size_t>(row)].wemHash
               : QString();
}

} // namespace dubstudio
