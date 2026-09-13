// MainWindow Фазы 0: тёмная тема, слева дерево файл->сцена (QDockWidget),
// в центре QTableView (Статус|Спикер|EN|RU|Длит) с FTS-поиском и ленивой
// подгрузкой (виртуализация на 39481 строку). PLAN.md 13, 15.4.
#pragma once

#include <QMainWindow>
#include <QString>

#include <memory>

class QLabel;
class QLineEdit;
class QStandardItem;
class QStandardItemModel;
class QTableView;
class QTreeView;

namespace dubstudio {

class Database;
class ImportStats;
class LinesSqlModel;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(const QString& dbPath, QWidget* parent = nullptr);
    ~MainWindow() override;

private slots:
    void openDatabase();
    void importCombinedJson();
    void onTreeSelection();
    void onSearchChanged();

private:
    void buildUi();
    void buildMenu();
    void rebuildTree();
    void reloadStats();
    void applyDarkTheme();

    std::unique_ptr<Database> db_;
    QString dbPath_;

    LinesSqlModel* model_ = nullptr;
    QTreeView* tree_ = nullptr;
    QStandardItemModel* treeModel_ = nullptr;
    QTableView* table_ = nullptr;
    QLineEdit* search_ = nullptr;
    QLabel* statsLabel_ = nullptr;
    QLabel* dbLabel_ = nullptr;
};

void applyDarkTheme(); // выставить Fusion + тёмная палитра (до создания окна)

} // namespace dubstudio
