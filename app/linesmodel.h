// Табличная модель реплик с ленивой постраничной загрузкой (виртуализация):
// 39481 строк не грузятся в память целиком, QTableView дергает fetchMore
// при прокрутке. Колонки по PLAN.md 15.4: Статус | Спикер | EN | RU | Длит.
#pragma once

#include "dubstudio/database.h"

#include <QAbstractTableModel>
#include <QString>
#include <vector>

namespace dubstudio {

class LinesSqlModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column {
        ColStatus = 0,
        ColSpeaker,
        ColEn,
        ColRu,
        ColDur,
        ColCount,
    };

    explicit LinesSqlModel(Database& db, QObject* parent = nullptr);

    // Фильтры (пустая строка = без фильтра). Сбрасывает пейджинг.
    void setFilter(const QString& fileId, const QString& questId, const QString& ftsQuery);
    void refresh();

    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;
    bool canFetchMore(const QModelIndex& parent = {}) const override;
    void fetchMore(const QModelIndex& parent = {}) override;

    QString wemHashAt(int row) const;
    QString statusRu(const QString& status) const;

private:
    struct Row {
        QString wemHash;
        QString status;
        QString speaker;
        QString en;
        QString ru;
        qint64 durMs = 0;
    };

    void recountTotal();
    void fetchPage(int limit);

    Database& db_;
    QString fileId_;
    QString questId_;
    QString ftsQuery_;
    std::vector<Row> rows_;
    qint64 total_ = 0;

    static constexpr int kPageSize = 500;
};

} // namespace dubstudio
