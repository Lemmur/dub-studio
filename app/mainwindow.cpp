#include "mainwindow.h"

#include "linesmodel.h"

#include "dubstudio/database.h"
#include "dubstudio/importer.h"

#include <QAction>
#include <QApplication>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QKeySequence>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPalette>
#include <QStandardItemModel>
#include <QStatusBar>
#include <QStyle>
#include <QStyleFactory>
#include <QTableView>
#include <QTreeView>
#include <QVBoxLayout>

#include <sqlite3.h>

#include <chrono>

namespace dubstudio {
namespace {

// Роли для хранения id в элементах дерева.
constexpr int kRoleFileId = Qt::UserRole + 1;
constexpr int kRoleQuestId = Qt::UserRole + 2;

} // namespace

void applyDarkTheme() {
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    QPalette p;
    const QColor window(0x2b, 0x2b, 0x2b);
    const QColor base(0x1e, 0x1e, 0x1e);
    const QColor text(0xe0, 0xe0, 0xe0);
    const QColor highlight(0x4f, 0x7c, 0xb0);
    p.setColor(QPalette::Window, window);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, base);
    p.setColor(QPalette::AlternateBase, QColor(0x26, 0x26, 0x26));
    p.setColor(QPalette::ToolTipBase, QColor(0x33, 0x33, 0x33));
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, QColor(0x35, 0x35, 0x35));
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::Highlight, highlight);
    p.setColor(QPalette::HighlightedText, QColor(0xff, 0xff, 0xff));
    p.setColor(QPalette::Disabled, QPalette::Text, QColor(0x80, 0x80, 0x80));
    p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0x80, 0x80, 0x80));
    p.setColor(QPalette::PlaceholderText, QColor(0x88, 0x88, 0x88));
    QApplication::setPalette(p);
}

MainWindow::MainWindow(const QString& dbPath, QWidget* parent) : QMainWindow(parent) {
    dbPath_ = dbPath;
    db_ = std::make_unique<Database>(dbPath_.toStdString());
    buildUi();
    buildMenu();
    rebuildTree();
    reloadStats();
}

MainWindow::~MainWindow() = default;

void MainWindow::buildUi() {
    setWindowTitle(QStringLiteral("DubStudio — Фаза 0"));
    resize(1400, 800);

    // --- Левый док: дерево файл -> сцена ---
    auto* dock = new QDockWidget(QStringLiteral("Сцены"), this);
    dock->setObjectName(QStringLiteral("scenesDock"));
    dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    tree_ = new QTreeView(dock);
    treeModel_ = new QStandardItemModel(tree_);
    treeModel_->setHorizontalHeaderLabels({QStringLiteral("Файл / сцена")});
    tree_->setModel(treeModel_);
    tree_->setUniformRowHeights(true);
    tree_->setExpandsOnDoubleClick(true);
    tree_->header()->setStretchLastSection(true);
    connect(tree_->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &MainWindow::onTreeSelection);
    dock->setWidget(tree_);
    addDockWidget(Qt::LeftDockWidgetArea, dock);

    // --- Центр: поиск + таблица реплик ---
    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(6, 6, 6, 6);

    auto* searchRow = new QHBoxLayout();
    searchRow->addWidget(new QLabel(QStringLiteral("Поиск:"), central));
    search_ = new QLineEdit(central);
    search_->setPlaceholderText(
        QStringLiteral("FTS-поиск по всем репликам, например: Коэн  |  speaker_name:Lunka"));
    search_->setClearButtonEnabled(true);
    searchRow->addWidget(search_, 1);
    layout->addLayout(searchRow);

    table_ = new QTableView(central);
    model_ = new LinesSqlModel(*db_, table_);
    table_->setModel(model_);
    table_->setAlternatingRowColors(true);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    // Одинаковая высота строк (равномерный скролл на 39481 строке): фиксированный
    // размер секции вместо setUniformRowHeights (появился только в Qt 6.7).
    table_->verticalHeader()->hide();
    table_->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    table_->verticalHeader()->setDefaultSectionSize(24);
    table_->horizontalHeader()->setStretchLastSection(false);
    table_->horizontalHeader()->setSectionResizeMode(LinesSqlModel::ColStatus,
                                                     QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(LinesSqlModel::ColSpeaker,
                                                     QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(LinesSqlModel::ColEn,
                                                     QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(LinesSqlModel::ColRu,
                                                     QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(LinesSqlModel::ColDur,
                                                     QHeaderView::ResizeToContents);
    layout->addWidget(table_, 1);
    setCentralWidget(central);

    connect(search_, &QLineEdit::textChanged, this, &MainWindow::onSearchChanged);

    // --- Статус-бар ---
    statsLabel_ = new QLabel(this);
    dbLabel_ = new QLabel(this);
    statusBar()->addWidget(statsLabel_, 1);
    statusBar()->addPermanentWidget(dbLabel_);
}

void MainWindow::buildMenu() {
    // Файл
    QMenu* fileMenu = menuBar()->addMenu(QStringLiteral("&Файл"));
    QAction* openDb = fileMenu->addAction(QStringLiteral("Открыть/создать базу…"));
    connect(openDb, &QAction::triggered, this, &MainWindow::openDatabase);
    QAction* importAction = fileMenu->addAction(QStringLiteral("Импорт combined.json…"));
    importAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+I")));
    connect(importAction, &QAction::triggered, this, &MainWindow::importCombinedJson);
    fileMenu->addSeparator();
    QAction* quit = fileMenu->addAction(QStringLiteral("Выход"));
    quit->setShortcut(QKeySequence::Quit);
    connect(quit, &QAction::triggered, this, &QWidget::close);

    // Вид
    QMenu* viewMenu = menuBar()->addMenu(QStringLiteral("&Вид"));
    QAction* toggleScenes = viewMenu->addAction(QStringLiteral("Дерево сцен"));
    toggleScenes->setCheckable(true);
    toggleScenes->setChecked(true);
    connect(toggleScenes, &QAction::toggled, findChild<QDockWidget*>("scenesDock"),
            &QDockWidget::setVisible);

    // Справка
    QMenu* helpMenu = menuBar()->addMenu(QStringLiteral("&Справка"));
    QAction* about = helpMenu->addAction(QStringLiteral("О программе"));
    connect(about, &QAction::triggered, this, [this] {
        QMessageBox::information(this, QStringLiteral("О программе"),
            QStringLiteral("DubStudio — Фаза 0 (скелет).\n"
                           "Импорт combined.json, дерево файл→сцена→реплики, "
                           "FTS-поиск, виртуальная таблица.\n\n"
                           "База: %1").arg(dbPath_));
    });
}

void MainWindow::openDatabase() {
    QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Открыть или создать базу"),
                                                dbPath_, QStringLiteral("SQLite (*.db)"),
                                                nullptr, QFileDialog::DontConfirmOverwrite);
    if (path.isEmpty()) return;
    if (!path.endsWith(QStringLiteral(".db"), Qt::CaseInsensitive)) path += QStringLiteral(".db");
    try {
        db_ = std::make_unique<Database>(path.toStdString());
        dbPath_ = path;
        delete model_;
        model_ = new LinesSqlModel(*db_, table_);
        table_->setModel(model_);
        rebuildTree();
        reloadStats();
    } catch (const std::exception& e) {
        QMessageBox::critical(this, QStringLiteral("Ошибка базы"), QString::fromUtf8(e.what()));
    }
}

void MainWindow::importCombinedJson() {
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Выбрать combined.json"), QString(),
        QStringLiteral("combined.json (*.json);;Все файлы (*)"));
    if (path.isEmpty()) return;

    try {
        Importer importer(*db_);
        const ImportStats stats = importer.importFile(path.toStdString());
        rebuildTree();
        reloadStats();
        model_->setFilter({}, {}, search_->text());
        QMessageBox::information(
            this, QStringLiteral("Импорт завершён"),
            QStringLiteral("Файлов: %1\nСцен: %2\nРеплик: %3\n\nВремя импорта: %4 с")
                .arg(stats.files)
                .arg(stats.quests)
                .arg(stats.lines)
                .arg(stats.elapsedMs / 1000.0, 0, 'f', 2));
    } catch (const std::exception& e) {
        QMessageBox::critical(this, QStringLiteral("Ошибка импорта"),
                              QString::fromUtf8(e.what()));
    }
}

void MainWindow::rebuildTree() {
    treeModel_->removeRows(0, treeModel_->rowCount());

    sqlite3* db = db_->handle();

    // Файлы по order_index.
    std::vector<std::pair<QString, int>> files;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT file_id, order_index FROM game_files ORDER BY order_index;",
                           -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char* t = sqlite3_column_text(stmt, 0);
            files.emplace_back(t ? QString::fromUtf8(reinterpret_cast<const char*>(t)) : QString(),
                               sqlite3_column_int(stmt, 1));
        }
        sqlite3_finalize(stmt);
    }

    // Сцены + счётчик реплик одним запросом.
    for (const auto& [fileId, fileOrder] : files) {
        auto* fileItem = new QStandardItem(fileId);
        fileItem->setEditable(false);
        fileItem->setData(fileId, kRoleFileId);
        QFont bold = fileItem->font();
        bold.setBold(true);
        fileItem->setFont(bold);

        std::int64_t fileLines = 0;
        const QString sql =
            QStringLiteral(
                "SELECT q.quest_id, q.order_index, COUNT(l.line_pk) AS n"
                " FROM quests q LEFT JOIN lines l ON l.quest_id = q.quest_id"
                " WHERE q.file_id = ?1 GROUP BY q.quest_id, q.order_index"
                " ORDER BY q.order_index;");
        if (sqlite3_prepare_v2(db, sql.toUtf8().constData(), -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, fileId.toUtf8().constData(), -1, SQLITE_TRANSIENT);
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                const unsigned char* t = sqlite3_column_text(stmt, 0);
                const auto questId =
                    t ? QString::fromUtf8(reinterpret_cast<const char*>(t)) : QString();
                const auto n = sqlite3_column_int64(stmt, 2);
                fileLines += n;
                auto* questItem = new QStandardItem(QStringLiteral("%1 (%2)").arg(questId).arg(n));
                questItem->setEditable(false);
                questItem->setData(fileId, kRoleFileId);
                questItem->setData(questId, kRoleQuestId);
                questItem->setToolTip(questId);
                fileItem->appendRow(questItem);
            }
            sqlite3_finalize(stmt);
        }
        fileItem->setText(QStringLiteral("%1 (%2)").arg(fileId).arg(fileLines));
        treeModel_->appendRow(fileItem);
    }
    tree_->expandAll();
}

void MainWindow::onTreeSelection() {
    const QModelIndexList selected = tree_->selectionModel()->selectedIndexes();
    if (selected.isEmpty()) {
        model_->setFilter({}, {}, search_->text());
        return;
    }
    const QModelIndex idx = selected.first();
    const QString questId = idx.data(kRoleQuestId).toString();
    const QString fileId = idx.data(kRoleFileId).toString();
    // Выбрана сцена — фильтруем по ней; выбран файл — по файлу.
    model_->setFilter(questId.isEmpty() ? fileId : QString(), questId, search_->text());
}

void MainWindow::onSearchChanged() {
    // Фильтр из дерева сохраняем, FTS-запрос обновляем.
    const QModelIndexList selected = tree_->selectionModel()->selectedIndexes();
    QString fileId, questId;
    if (!selected.isEmpty()) {
        questId = selected.first().data(kRoleQuestId).toString();
        fileId = selected.first().data(kRoleFileId).toString();
    }
    model_->setFilter(questId.isEmpty() ? fileId : QString(), questId, search_->text());
}

void MainWindow::reloadStats() {
    const auto files = db_->scalarInt("SELECT COUNT(*) FROM game_files;");
    const auto quests = db_->scalarInt("SELECT COUNT(*) FROM quests;");
    const auto lines = db_->scalarInt("SELECT COUNT(*) FROM lines;");
    statsLabel_->setText(QStringLiteral("Файлов: %1  |  Сцен: %2  |  Реплик: %3")
                             .arg(files)
                             .arg(quests)
                             .arg(lines));
    dbLabel_->setText(QFileInfo(dbPath_).absoluteFilePath());
}

void MainWindow::applyDarkTheme() {
    dubstudio::applyDarkTheme();
}

} // namespace dubstudio
