#include "mainwindow.h"

#include "levelmeter.h"
#include "linesmodel.h"
#include "timeline.h"

#include "dubstudio/audio_engine.h"
#include "dubstudio/audio_ops.h"
#include "dubstudio/clip_store.h"
#include "dubstudio/database.h"
#include "dubstudio/edit_stack.h"
#include "dubstudio/importer.h"
#include "dubstudio/wav_writer.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QKeySequence>
#include <QLinearGradient>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPalette>
#include <QScrollArea>
#include <QSettings>
#include <QSlider>
#include <QStandardItemModel>
#include <QSpinBox>
#include <QStatusBar>
#include <QStyle>
#include <QStyleFactory>
#include <QToolBar>
#include <QTableView>
#include <QTimer>
#include <QTreeView>
#include <QVBoxLayout>

#include <sqlite3.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdexcept>

namespace dubstudio {
namespace {

// Роли для хранения id в элементах дерева.
constexpr int kRoleFileId = Qt::UserRole + 1;
constexpr int kRoleQuestId = Qt::UserRole + 2;

// Тонкий вертикальный градиентный разделитель между блоками статус-бара.
class StatusSeparator : public QWidget {
public:
    explicit StatusSeparator(QWidget* parent = nullptr) : QWidget(parent) {
        setFixedSize(11, 18);
    }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        QLinearGradient g(0, 2, 0, height() - 2);
        g.setColorAt(0.0, QColor(0x3a, 0x3a, 0x3a));
        g.setColorAt(0.5, QColor(0x88, 0x88, 0x88));
        g.setColorAt(1.0, QColor(0x3a, 0x3a, 0x3a));
        p.setPen(QPen(QBrush(g), 1));
        p.drawLine(width() / 2, 2, width() / 2, height() - 2);
    }
};

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
    // QMenu красит неактивные пункты через WindowText: без явного цвета
    // Windows рисует их «гравировкой» (белая тень под серым текстом).
    p.setColor(QPalette::Disabled, QPalette::WindowText, QColor(0x78, 0x78, 0x78));
    p.setColor(QPalette::PlaceholderText, QColor(0x88, 0x88, 0x88));
    QApplication::setPalette(p);
}

MainWindow::MainWindow(const QString& dbPath, QWidget* parent) : QMainWindow(parent) {
    dbPath_ = dbPath;
    myDubDir_ = QStringLiteral("MyDub"); // PLAN.md 12: рабочая директория
    db_ = std::make_unique<Database>(dbPath_.toStdString());

    engine_ = std::make_unique<AudioEngine>();
    store_ = std::make_unique<ClipStore>();
    edits_ = std::make_unique<EditStack>(*db_, *store_, myDubDir_.toStdString());

    buildUi();
    buildMenu();
    buildTransport();
    rebuildTree();
    reloadStats();

    // Фаза 2: восстановление сессии тейков после рестарта (PLAN.md 6.5) —
    // состояние каждого тейка = последний неотменённый шаг undo_log.
    const EditStack::SessionInfo session = edits_->loadSession();
    takeCounter_ = session.maxTakeNum;
    timeline_->syncTracks();
    connect(timeline_, &TimelineWidget::clipMoved, this, &MainWindow::onClipMoved);
    connect(timeline_, &TimelineWidget::clipDeleteRequested, this, &MainWindow::onDeleteTake);
    connect(timeline_, &TimelineWidget::rangeContextMenuRequested, this,
            &MainWindow::onRangeContextMenu);
    if (session.takes > 0) {
        statusBar()->showMessage(
            QStringLiteral("Сессия восстановлена: тейков %1").arg(session.takes), 6000);
    }
    updateUndoStatus();

    // Автосейв: быстрый снапшот WAL + manifest.json (PLAN.md 6.5).
    lastAutosave_ = QDateTime::currentDateTime();
    autosaveTimer_ = new QTimer(this);
    autosaveTimer_->setInterval(15000); // проверка условия каждые 15 с
    connect(autosaveTimer_, &QTimer::timeout, this, &MainWindow::onAutosaveTick);
    autosaveTimer_->start();
    autosaveLabel_->setText(QStringLiteral("Автосейв: —"));

    // Попытка автооткрытия аудио (последнее выбранное или первый ASIO).
    QSettings s;
    sampleRate_ = s.value(QStringLiteral("audio/sampleRate"), 48000).toUInt();
    bufferFrames_ = s.value(QStringLiteral("audio/buffer"), 256).toUInt();
    engine_->setTempo(s.value(QStringLiteral("metro/bpm"), 100).toInt(),
                      s.value(QStringLiteral("metro/beats"), 4).toInt());
    // Мониторинг: восстановить Direct Monitoring и громкость (PLAN.md 6.1).
    engine_->setDirectMonitoring(s.value(QStringLiteral("audio/directMonitor"), false).toBool());
    engine_->setMonitoringGain(
        static_cast<float>(s.value(QStringLiteral("audio/monitorGain"), 0.8).toDouble()));
    const QString lastDevice = s.value(QStringLiteral("audio/device")).toString();
    if (!lastDevice.isEmpty() && openAudioDevice(lastDevice, sampleRate_, bufferFrames_)) {
        // ок
    } else {
        // Первый ASIO, иначе WASAPI-вход.
        for (const auto& d : engine_->listDevices()) {
            if (d.inputChannels > 0) {
                const QString key = QStringLiteral("%1:%2")
                                        .arg(QString::fromStdString(d.apiName))
                                        .arg(d.deviceId);
                if (openAudioDevice(key, sampleRate_, bufferFrames_)) break;
            }
        }
    }

    // Раскладка интерфейса между запусками: геометрия окна + положение/размеры
    // доков и тулбаров (objectName доков уже заданы — scenesDock/timelineDock).
    QSettings geo;
    restoreGeometry(geo.value(QStringLiteral("ui/geometry")).toByteArray());
    restoreState(geo.value(QStringLiteral("ui/windowState")).toByteArray());

    tickTimer_ = new QTimer(this);
    tickTimer_->setInterval(33); // ~30 Гц: метры/playhead/живая волноформа
    connect(tickTimer_, &QTimer::timeout, this, &MainWindow::onTick);
    tickTimer_->start();
}

MainWindow::~MainWindow() = default;

void MainWindow::closeEvent(QCloseEvent* event) {
    QSettings s;
    s.setValue(QStringLiteral("ui/geometry"), saveGeometry());
    s.setValue(QStringLiteral("ui/windowState"), saveState());
    QMainWindow::closeEvent(event);
}

void MainWindow::buildUi() {
    setWindowTitle(QStringLiteral("DubStudio — Фаза 2 (редактура)"));
    resize(1500, 900);

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

    connectTableSelection();

    connect(search_, &QLineEdit::textChanged, this, &MainWindow::onSearchChanged);

    // --- Нижний док: таймлайн Track 0/1/N (Фаза 1) ---
    // Vertical QScrollArea: дорожки TAKE-NN растут вниз, видим все через скролл.
    // Горизонталь — своя (zoom колесом, drag-панорама), поэтому off.
    auto* tlDock = new QDockWidget(QStringLiteral("Таймлайн"), this);
    tlDock->setObjectName(QStringLiteral("timelineDock"));
    tlDock->setAllowedAreas(Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea);
    timeline_ = new TimelineWidget;
    timeline_->setEngine(engine_.get());
    timeline_->setStore(store_.get());
    auto* tlScroll = new QScrollArea(tlDock);
    tlScroll->setWidget(timeline_);
    tlScroll->setWidgetResizable(true);
    tlScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    tlScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    tlDock->setWidget(tlScroll);
    addDockWidget(Qt::BottomDockWidgetArea, tlDock);

    // --- Статус-бар: блоки с разделителями ---
    // [Статистика] │ [REC-время] │ [Xrun] │ [метр]  …  [Звук-чип] │ [БД]
    statsLabel_ = new QLabel(this);
    statsLabel_->setStyleSheet(QStringLiteral("color:#a8b2c0; padding:0 4px;"));
    recTimeLabel_ = new QLabel(this);
    recTimeLabel_->setStyleSheet(QStringLiteral("padding:0 4px;"));
    xrunLabel_ = new QLabel(this);
    xrunLabel_->setStyleSheet(QStringLiteral("padding:0 4px;"));
    level_ = new LevelMeter(this);
    audioLabel_ = new QLabel(this);
    audioLabel_->setStyleSheet(QStringLiteral(
        "background:#2e3444; border-radius:4px; padding:1px 8px; color:#9fc1ff;"));
    dbLabel_ = new QLabel(this);
    dbLabel_->setStyleSheet(QStringLiteral("color:#7a7a7a; padding:0 4px;"));
    statusBar()->addWidget(statsLabel_, 1);
    statusBar()->addWidget(recTimeLabel_);
    statusBar()->addWidget(new StatusSeparator(this));
    statusBar()->addWidget(xrunLabel_);
    statusBar()->addWidget(new StatusSeparator(this));
    statusBar()->addWidget(level_);
    // Фаза 2: undo/redo и автосейв в статус-баре.
    undoLabel_ = new QLabel(this);
    undoLabel_->setStyleSheet(QStringLiteral("color:#a8b2c0; padding:0 4px;"));
    statusBar()->addWidget(new StatusSeparator(this));
    statusBar()->addWidget(undoLabel_);
    autosaveLabel_ = new QLabel(this);
    autosaveLabel_->setStyleSheet(QStringLiteral("color:#7a9a7a; padding:0 4px;"));
    statusBar()->addPermanentWidget(autosaveLabel_);
    statusBar()->addPermanentWidget(new StatusSeparator(this));
    statusBar()->addPermanentWidget(audioLabel_); // звук: устройство · Гц · буфер
    statusBar()->addPermanentWidget(new StatusSeparator(this));
    statusBar()->addPermanentWidget(dbLabel_);
    updateAudioStatus();
}

void MainWindow::buildTransport() {
    auto* bar = addToolBar(QStringLiteral("Транспорт"));
    bar->setObjectName(QStringLiteral("transportBar"));
    bar->setMovable(false);

    QAction* rec = bar->addAction(QStringLiteral("● Запись"));
    rec->setObjectName(QStringLiteral("actRecord"));
    rec->setShortcut(QKeySequence(QStringLiteral("R")));
    rec->setToolTip(QStringLiteral("Запись тейка на выделенную реплику (R)"));
    connect(rec, &QAction::triggered, this, &MainWindow::onRecord);

    QAction* play = bar->addAction(QStringLiteral("▶ Плей"));
    play->setObjectName(QStringLiteral("actPlay"));
    play->setShortcut(QKeySequence(QStringLiteral("Space")));
    play->setToolTip(QStringLiteral("Плей последнего тейка / стоп (Space)"));
    connect(play, &QAction::triggered, this, &MainWindow::onPlay);

    QAction* stop = bar->addAction(QStringLiteral("■ Стоп"));
    stop->setObjectName(QStringLiteral("actStop"));
    stop->setToolTip(QStringLiteral("Стоп записи/плейбека"));
    connect(stop, &QAction::triggered, this, &MainWindow::onStop);

    bar->addSeparator();

    QAction* metro = bar->addAction(QStringLiteral("Метроном"));
    metro->setObjectName(QStringLiteral("actMetro"));
    metro->setCheckable(true);
    connect(metro, &QAction::toggled, this,
            [this](bool on) { engine_->setMetronomeEnabled(on); });

    QAction* monitor = bar->addAction(QStringLiteral("Мониторинг"));
    monitor->setObjectName(QStringLiteral("actMonitor"));
    monitor->setCheckable(true);
    monitor->setToolTip(QStringLiteral("Программный мониторинг входа (громкость — в Настройках аудио)"));
    connect(monitor, &QAction::toggled, this,
            [this](bool on) { engine_->setSoftwareMonitoring(on); });

    auto* bpmSpin = new QSpinBox(bar);
    bpmSpin->setRange(30, 300);
    bpmSpin->setValue(engine_->bpm());
    bpmSpin->setSuffix(QStringLiteral(" BPM"));
    bpmSpin->setToolTip(QStringLiteral("Темп метронома"));
    connect(bpmSpin, &QSpinBox::valueChanged, this, [this](int v) {
        engine_->setTempo(v, engine_->beatsPerBar());
        QSettings s;
        s.setValue(QStringLiteral("metro/bpm"), v);
    });
    bar->addWidget(bpmSpin);

    QAction* audio = bar->addAction(QStringLiteral("Аудио…"));
    audio->setObjectName(QStringLiteral("actAudioSettings"));
    connect(audio, &QAction::triggered, this, &MainWindow::onAudioSettings);

    // Фаза 2: операции дубляжа (PLAN.md 13: Record Loop Play Fit Ref Comp).
    bar->addSeparator();
    QAction* split = bar->addAction(QStringLiteral("✂ Разделить"));
    split->setObjectName(QStringLiteral("actSplit"));
    split->setShortcut(QKeySequence(QStringLiteral("S")));
    split->setToolTip(QStringLiteral("Разделить выбранный клип по курсору (S)"));
    connect(split, &QAction::triggered, this, &MainWindow::onSplit);

    QAction* fit = bar->addAction(QStringLiteral("⇔ Fit Ref"));
    fit->setObjectName(QStringLiteral("actFitRef"));
    fit->setToolTip(
        QStringLiteral("Растянуть тейк до длительности референса (питч сохраняется)"));
    connect(fit, &QAction::triggered, this, &MainWindow::onFitToRef);

    QAction* align = bar->addAction(QStringLiteral("⇤ Align"));
    align->setObjectName(QStringLiteral("actAlign"));
    align->setToolTip(QStringLiteral("Привязать начало клипа к началу референса"));
    connect(align, &QAction::triggered, this, &MainWindow::onAlign);
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

    // Правка (Фаза 2, PLAN.md 6.4): undo/redo, trim/split/fade/…
    buildEditMenuActions();

    // Аудио (Фаза 1)
    QMenu* audioMenu = menuBar()->addMenu(QStringLiteral("&Аудио"));
    QAction* settings = audioMenu->addAction(QStringLiteral("Настройки аудио…"));
    settings->setShortcut(QKeySequence(QStringLiteral("Ctrl+U")));
    connect(settings, &QAction::triggered, this, &MainWindow::onAudioSettings);

    // Настройки: автосейв (Фаза 2, PLAN.md 6.5)
    QMenu* optsMenu = menuBar()->addMenu(QStringLiteral("Н&астройки"));
    QAction* project = optsMenu->addAction(QStringLiteral("Проект…"));
    connect(project, &QAction::triggered, this, &MainWindow::onProjectSettings);

    // Вид
    QMenu* viewMenu = menuBar()->addMenu(QStringLiteral("&Вид"));
    QAction* toggleScenes = viewMenu->addAction(QStringLiteral("Дерево сцен"));
    toggleScenes->setCheckable(true);
    toggleScenes->setChecked(true);
    connect(toggleScenes, &QAction::toggled, findChild<QDockWidget*>("scenesDock"),
            &QDockWidget::setVisible);
    QAction* toggleTimeline = viewMenu->addAction(QStringLiteral("Таймлайн"));
    toggleTimeline->setCheckable(true);
    toggleTimeline->setChecked(true);
    connect(toggleTimeline, &QAction::toggled, findChild<QDockWidget*>("timelineDock"),
            &QDockWidget::setVisible);

    // Справка
    QMenu* helpMenu = menuBar()->addMenu(QStringLiteral("&Справка"));
    QAction* about = helpMenu->addAction(QStringLiteral("О программе"));
    connect(about, &QAction::triggered, this, [this] {
        QMessageBox::information(this, QStringLiteral("О программе"),
            QStringLiteral("DubStudio — Фаза 2 (редактура).\n\n"
                            "Фаза 1: RtAudio + ASIO/WASAPI, запись моно 48 кГц/24-bit,\n"
                            "треки Track 0 REF-EN / MASTER-RU / TAKE-N,\n"
                            "волноформа с zoom, метроном, мониторинг.\n\n"
                            "Фаза 2: trim/split/move/fade/crossfade/gain/normalize/\n"
                            "silence/reverse, Fit to Ref (WSOLA, питч сохраняется),\n"
                            "Align, Undo/Redo через рестарт, автосейв.\n\n"
                            "База: %1").arg(dbPath_));
    });
}

QString MainWindow::currentWemHash() const {
    const QModelIndex idx = table_->currentIndex();
    if (!idx.isValid()) return {};
    return model_->wemHashAt(idx.row());
}

bool MainWindow::openAudioDevice(const QString& deviceId, unsigned int sampleRate,
                                 unsigned int bufferFrames) {
    const QStringList parts = deviceId.split(QLatin1Char(':'));
    if (parts.size() != 2) return false;
    const QString apiName = parts[0];
    const unsigned int devId = parts[1].toUInt();
    for (const auto& d : engine_->listDevices()) {
        if (QString::fromStdString(d.apiName) == apiName && d.deviceId == devId) {
            try {
                engine_->open(d, sampleRate, bufferFrames);
                audioDeviceId_ = deviceId;
                sampleRate_ = engine_->sampleRate();
                bufferFrames_ = engine_->bufferFrames();
                QSettings s;
                s.setValue(QStringLiteral("audio/device"), deviceId);
                s.setValue(QStringLiteral("audio/sampleRate"), sampleRate_);
                s.setValue(QStringLiteral("audio/buffer"), bufferFrames_);
                updateAudioStatus();
                return true;
            } catch (const std::exception& e) {
                statusBar()->showMessage(QString::fromUtf8(e.what()), 8000);
                return false;
            }
        }
    }
    return false;
}

void MainWindow::onAudioSettings() {
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("Настройки аудио"));
    auto* form = new QFormLayout(&dlg);

    auto* deviceBox = new QComboBox(&dlg);
    const auto devices = engine_->listDevices();
    int selected = -1;
    for (const auto& d : devices) {
        const QString key = QStringLiteral("%1:%2")
                                .arg(QString::fromStdString(d.apiName))
                                .arg(d.deviceId);
        const QString label = QStringLiteral("[%1] %2 (%3 вх / %4 вых)")
                                  .arg(QString::fromStdString(d.apiName),
                                       QString::fromStdString(d.name))
                                  .arg(d.inputChannels)
                                  .arg(d.outputChannels);
        deviceBox->addItem(label, key);
        if (key == audioDeviceId_) selected = deviceBox->count() - 1;
    }
    if (devices.empty()) {
        deviceBox->addItem(QStringLiteral("Устройства не найдены"), QString());
    } else if (selected < 0) {
        selected = 0;
    }
    deviceBox->setCurrentIndex(std::max(0, selected));
    form->addRow(QStringLiteral("Устройство:"), deviceBox);

    auto* rateBox = new QComboBox(&dlg);
    rateBox->addItem(QStringLiteral("44100 Гц"), 44100);
    rateBox->addItem(QStringLiteral("48000 Гц (по умолчанию)"), 48000);
    rateBox->addItem(QStringLiteral("96000 Гц"), 96000);
    const int rateIdx = rateBox->findData(sampleRate_);
    rateBox->setCurrentIndex(rateIdx >= 0 ? rateIdx : 1);
    form->addRow(QStringLiteral("Частота проекта:"), rateBox);

    auto* bufferBox = new QComboBox(&dlg);
    bufferBox->addItem(QStringLiteral("128"), 128);
    bufferBox->addItem(QStringLiteral("256 (по умолчанию)"), 256);
    bufferBox->addItem(QStringLiteral("512"), 512);
    bufferBox->addItem(QStringLiteral("1024"), 1024);
    const int bufIdx = bufferBox->findData(static_cast<int>(bufferFrames_));
    bufferBox->setCurrentIndex(bufIdx >= 0 ? bufIdx : 1);
    form->addRow(QStringLiteral("Буфер, сэмплов:"), bufferBox);

    QSettings pre;
    auto* direct = new QCheckBox(
        QStringLiteral("Direct Monitoring (аппаратный, софт-копия выключается)"), &dlg);
    direct->setChecked(pre.value(QStringLiteral("audio/directMonitor"), false).toBool());
    form->addRow(QString(), direct);

    // Громкость программного мониторинга (PLAN.md 6.1: мониторинг с gain).
    auto* gainRow = new QWidget(&dlg);
    auto* gainLay = new QHBoxLayout(gainRow);
    gainLay->setContentsMargins(0, 0, 0, 0);
    auto* gainSlider = new QSlider(Qt::Horizontal, gainRow);
    gainSlider->setRange(0, 100);
    const double gainNow =
        pre.value(QStringLiteral("audio/monitorGain"), 0.8).toDouble();
    gainSlider->setValue(static_cast<int>(gainNow * 100.0));
    auto* gainVal = new QLabel(QStringLiteral("%1 %").arg(gainSlider->value()), gainRow);
    gainVal->setFixedWidth(48);
    QObject::connect(gainSlider, &QSlider::valueChanged, gainVal,
                     [gainVal](int v) { gainVal->setText(QStringLiteral("%1 %").arg(v)); });
    gainLay->addWidget(gainSlider, 1);
    gainLay->addWidget(gainVal);
    form->addRow(QStringLiteral("Громкость мониторинга:"), gainRow);

    auto* beatsSpin = new QSpinBox(&dlg);
    beatsSpin->setRange(1, 12);
    beatsSpin->setValue(engine_->beatsPerBar());
    form->addRow(QStringLiteral("Долей в такте:"), beatsSpin);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(buttons);

    if (dlg.exec() != QDialog::Accepted) return;

    const QString key = deviceBox->currentData().toString();
    const auto rate = static_cast<unsigned int>(rateBox->currentData().toUInt());
    const auto buf = static_cast<unsigned int>(bufferBox->currentData().toUInt());
    if (!key.isEmpty()) openAudioDevice(key, rate, buf);
    engine_->setDirectMonitoring(direct->isChecked());
    const float gain = static_cast<float>(gainSlider->value()) / 100.0f;
    engine_->setMonitoringGain(gain);
    engine_->setTempo(engine_->bpm(), beatsSpin->value());
    QSettings s;
    s.setValue(QStringLiteral("audio/directMonitor"), direct->isChecked());
    s.setValue(QStringLiteral("audio/monitorGain"), gain);
    s.setValue(QStringLiteral("metro/beats"), beatsSpin->value());
}

void MainWindow::updateAudioStatus() {
    if (!audioLabel_) return;
    if (engine_ && engine_->isOpen()) {
        const auto& d = engine_->currentDevice();
        audioLabel_->setText(
            QStringLiteral("Звук: %1 [%2] · %3 Гц · буфер %4")
                .arg(QString::fromStdString(d.name), QString::fromStdString(d.apiName))
                .arg(engine_->sampleRate())
                .arg(engine_->bufferFrames()));
        audioLabel_->setToolTip(QStringLiteral("Устройство, частота проекта и размер буфера"));
    } else {
        audioLabel_->setText(QStringLiteral("Звук: не открыт (Аудио → Настройки аудио…)"));
    }
}

void MainWindow::onRecord() {
    auto* act = findChild<QAction*>("actRecord");
    if (!engine_->isOpen()) {
        QMessageBox::warning(this, QStringLiteral("Аудио не открыто"),
                             QStringLiteral("Сначала выберите устройство: Аудио → Настройки аудио…"));
        return;
    }
    if (engine_->isRecording()) { // R — тумблер
        onStop();
        return;
    }
    const QString hash = currentWemHash();
    if (hash.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Нет реплики"),
                                 QStringLiteral("Выберите реплику в таблице: тейк пишется на реплику."));
        return;
    }

    ++takeCounter_;
    // id тейка = "<wem_hash>_take_<N>" (полный хэш реплики): файл
    // MyDub/takes/<wem_hash>_take_<N>.wav зеркалит именование игры
    // "<GUID>_en.wem" и сразу видна принадлежность реплике.
    // Гарантия уникальности: счётчик после рестарта берётся из БД, но строки
    // могли остаться без WAV (MyDub удалён) — сверяемся с базой.
    auto takeIdExists = [this](const QString& id) {
        sqlite3_stmt* st = nullptr;
        bool exists = false;
        if (sqlite3_prepare_v2(db_->handle(), "SELECT 1 FROM takes WHERE take_id=?1;", -1,
                               &st, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(st, 1, id.toUtf8().constData(), -1, SQLITE_TRANSIENT);
            exists = sqlite3_step(st) == SQLITE_ROW;
            sqlite3_finalize(st);
        }
        return exists;
    };
    while (takeIdExists(QStringLiteral("%1_take_%2").arg(hash).arg(takeCounter_))) {
        ++takeCounter_;
    }
    Clip clip;
    clip.id = QStringLiteral("%1_take_%2").arg(hash).arg(takeCounter_).toStdString();
    clip.title = QStringLiteral("TAKE-%1").arg(takeCounter_, 2, 10, QLatin1Char('0')).toStdString();
    clip.wemHash = hash.toStdString();
    clip.sampleRate = engine_->sampleRate();
    clip.colorRgb = ClipStore::autoColor(takeCounter_ - 1);
    liveTakeIndex_ = static_cast<int>(store_->addTake(std::move(clip)));

    engine_->startRecord();
    timeline_->syncTracks();
    if (act) act->setText(QStringLiteral("■ Стоп записи"));
    recTimeLabel_->setText(QStringLiteral("REC 0.0 c"));
    statusBar()->showMessage(
        QStringLiteral("Запись: %1").arg(hash), 4000);
}

void MainWindow::onPlay() {
    if (engine_->isRecording()) return;
    // Микс ВИДИМЫХ клипов (фильтр по выбранной реплике): позиции/гейны/фейды.
    std::vector<Clip> visible;
    if (timeline_->lineFilterActive()) {
        const std::string& filter = timeline_->lineFilter();
        for (const auto& t : store_->takes()) {
            if (t.wemHash == filter) visible.push_back(t);
        }
    }
    const std::vector<float> mix = renderMix(visible);
    if (mix.empty()) {
        statusBar()->showMessage(
            timeline_->lineFilterActive()
                ? QStringLiteral("У этой реплики ещё нет тейков")
                : QStringLiteral("Выберите реплику в таблице — плей играет её тейки"),
            4000);
        return;
    }
    engine_->play(mix.data(), mix.size());
}

void MainWindow::onStop() {
    if (engine_->isRecording()) {
        finalizeTake();
    }
    engine_->stopPlayback();
    auto* act = findChild<QAction*>("actRecord");
    if (act) act->setText(QStringLiteral("● Запись"));
}

void MainWindow::finalizeTake() {
    auto tail = engine_->stopRecord();
    auto& takes = store_->takes();
    if (liveTakeIndex_ < 0 || liveTakeIndex_ >= static_cast<int>(takes.size())) return;
    Clip& clip = takes[static_cast<std::size_t>(liveTakeIndex_)];

    // Хвост из кольца добавляем к уже надрейненному.
    clip.samples.insert(clip.samples.end(), tail.samples.begin(), tail.samples.end());
    clip.rmsDb = rmsDb(clip.samples);
    clip.peakDb = peakDb(clip.samples);
    ClipStore::rebuildPeaks(clip);

    // WAV 24-bit в MyDub/takes/.
    QDir().mkpath(QStringLiteral("MyDub/takes"));
    const QString wavPath = QStringLiteral("MyDub/takes/%1.wav")
                                .arg(QString::fromStdString(clip.id));
    try {
        WavWriter::writePcm24(wavPath.toStdString(), clip.samples,
                              static_cast<std::uint32_t>(clip.sampleRate));
        clip.filePath = wavPath.toStdString();
    } catch (const std::exception& e) {
        QMessageBox::warning(this, QStringLiteral("Ошибка WAV"),
                             QString::fromUtf8(e.what()));
    }

    // БД: takes + undo_log + статус реплики (одной транзакцией).
    const std::string takeId = clip.id;
    const std::string hash = clip.wemHash;
    const auto durMs = static_cast<int>(clip.samples.size() * 1000.0 / clip.sampleRate);
    const std::string quality = clip.peakDb > -1.0 || clip.rmsDb < -50.0 ? "red" : "green";
    // ВАЖНО: sqlite3_prepare_v2 компилирует только ПЕРВОЕ выражение многострочного
    // SQL — «BEGIN;…;COMMIT;» одним prepare оставлял транзакцию открытой навсегда
    // (ломая все последующие BEGIN, в т.ч. команды правок Фазы 2). Поэтому
    // BEGIN/COMMIT отдельно через exec, выражения — по одному prepare.
    QString err;
    sqlite3* h = db_->handle();
    db_->exec("BEGIN");
    try {
        auto runStep = [&](const char* sql, auto bind) {
            sqlite3_stmt* st = nullptr;
            if (sqlite3_prepare_v2(h, sql, -1, &st, nullptr) != SQLITE_OK) {
                throw std::runtime_error(sqlite3_errmsg(h));
            }
            bind(st);
            if (sqlite3_step(st) != SQLITE_DONE) {
                const std::string msg = sqlite3_errmsg(h);
                sqlite3_finalize(st);
                throw std::runtime_error(msg);
            }
            sqlite3_finalize(st);
        };
        runStep("INSERT INTO takes(take_id, wem_hash, file_cas, duration_ms, quality,"
                " rms_db, peak_db, is_master_candidate, comment)"
                " VALUES(?1,?2,?3,?4,?5,?6,?7,0,'Фаза 1: запись');",
                [&](sqlite3_stmt* st) {
                    sqlite3_bind_text(st, 1, takeId.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(st, 2, hash.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(st, 3, clip.filePath.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_int(st, 4, durMs);
                    sqlite3_bind_text(st, 5, quality.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_double(st, 6, clip.rmsDb);
                    sqlite3_bind_double(st, 7, clip.peakDb);
                });
        runStep("INSERT INTO undo_log(action, scope) VALUES('take.record:' || ?1, 'take');",
                [&](sqlite3_stmt* st) {
                    sqlite3_bind_text(st, 1, takeId.c_str(), -1, SQLITE_TRANSIENT);
                });
        runStep("UPDATE lines SET status='recorded' WHERE wem_hash=?1;",
                [&](sqlite3_stmt* st) {
                    sqlite3_bind_text(st, 1, hash.c_str(), -1, SQLITE_TRANSIENT);
                });
        db_->exec("COMMIT");
    } catch (const std::exception& e) {
        try { db_->exec("ROLLBACK"); } catch (...) {}
        err = QString::fromUtf8(e.what());
    }
    if (!err.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Ошибка БД"), err);
    }

    liveTakeIndex_ = -1;
    timeline_->syncTracks();
    // Выделение строки реплики переживает refresh (мы продолжаем с ней работать).
    const QString keepHash = currentWemHash();
    model_->refresh();
    restoreTableSelection(keepHash);
    markDirty(); // автосейв: появилась запись
    recTimeLabel_->setText(QString());
    statusBar()->showMessage(
        QStringLiteral("Тейк записан: %1 мс, RMS %2 dB, Peak %3 dB, xrun %4")
            .arg(durMs)
            .arg(clip.rmsDb, 0, 'f', 1)
            .arg(clip.peakDb, 0, 'f', 1)
            .arg(tail.xruns),
        8000);
}

void MainWindow::onTick() {
    // Живую запись дрейним в клип и перерисовываем волночку.
    if (engine_->isRecording() && liveTakeIndex_ >= 0) {
        auto& takes = store_->takes();
        Clip& clip = takes[static_cast<std::size_t>(liveTakeIndex_)];
        engine_->drainRecorded(clip.samples);
        ClipStore::rebuildPeaks(clip);
        const double sec = static_cast<double>(engine_->recordedFrames()) / engine_->sampleRate();
        recTimeLabel_->setText(QStringLiteral("REC %1 c").arg(sec, 0, 'f', 1));
        recTimeLabel_->setStyleSheet(
            QStringLiteral("color:#ff6b6b; font-weight:bold; padding:0 4px;"));
    } else if (!recTimeLabel_->text().isEmpty()) {
        recTimeLabel_->clear();
        recTimeLabel_->setStyleSheet(QStringLiteral("padding:0 4px;"));
    }
    level_->setLevel(engine_->inputPeak());
    // Xrun: 0 — зелёный, >0 — красный (внимание).
    const auto xruns = engine_->xrunCount();
    xrunLabel_->setText(QStringLiteral("Xrun: %1").arg(xruns));
    xrunLabel_->setStyleSheet(xruns
                                  ? QStringLiteral("color:#ff6b6b; font-weight:bold; padding:0 4px;")
                                  : QStringLiteral("color:#6bd18b; padding:0 4px;"));
    timeline_->tick();
}

// --- Фаза 2: редактура (PLAN.md 6.4, 6.5) --------------------------------------

void MainWindow::buildEditMenuActions() {
    QMenu* m = menuBar()->addMenu(QStringLiteral("&Правка"));

    QAction* undo = m->addAction(QStringLiteral("Отменить"));
    undo->setObjectName(QStringLiteral("actUndo"));
    undo->setShortcut(QKeySequence::Undo); // Ctrl+Z
    connect(undo, &QAction::triggered, this, &MainWindow::onUndo);

    QAction* redo = m->addAction(QStringLiteral("Повторить"));
    redo->setObjectName(QStringLiteral("actRedo"));
    redo->setShortcut(QKeySequence::Redo); // Ctrl+Y
    connect(redo, &QAction::triggered, this, &MainWindow::onRedo);

    m->addSeparator();

    QAction* split = m->addAction(QStringLiteral("Разделить по курсору"));
    split->setObjectName(QStringLiteral("actEditSplit"));
    // S уже назначен на тулбарной кнопке — дублировать не нужно.
    connect(split, &QAction::triggered, this, &MainWindow::onSplit);

    QAction* trimSil = m->addAction(QStringLiteral("Трим тишины по краям (−50 dBFS)"));
    trimSil->setObjectName(QStringLiteral("actTrimSilence"));
    connect(trimSil, &QAction::triggered, this, &MainWindow::onTrimSilence);

    QAction* trimRange = m->addAction(QStringLiteral("Обрезать вне диапазона"));
    trimRange->setObjectName(QStringLiteral("actTrimRange"));
    connect(trimRange, &QAction::triggered, this, &MainWindow::onTrimRange);

    QAction* silence = m->addAction(QStringLiteral("Тишина (диапазон или весь клип)"));
    silence->setObjectName(QStringLiteral("actSilence"));
    connect(silence, &QAction::triggered, this, &MainWindow::onSilence);

    QAction* reverse = m->addAction(QStringLiteral("Реверс (диапазон или весь клип)"));
    reverse->setObjectName(QStringLiteral("actReverse"));
    connect(reverse, &QAction::triggered, this, &MainWindow::onReverseRange);

    m->addSeparator();

    QAction* normalize = m->addAction(QStringLiteral("Нормализовать…"));
    normalize->setObjectName(QStringLiteral("actNormalize"));
    connect(normalize, &QAction::triggered, this, &MainWindow::onNormalize);

    QAction* gain = m->addAction(QStringLiteral("Усиление…"));
    gain->setObjectName(QStringLiteral("actGain"));
    connect(gain, &QAction::triggered, this, &MainWindow::onGain);

    QAction* fadeIn = m->addAction(QStringLiteral("Фейд-ин до курсора"));
    fadeIn->setObjectName(QStringLiteral("actFadeIn"));
    connect(fadeIn, &QAction::triggered, this, &MainWindow::onFadeIn);

    QAction* fadeOut = m->addAction(QStringLiteral("Фейд-аут от курсора"));
    fadeOut->setObjectName(QStringLiteral("actFadeOut"));
    connect(fadeOut, &QAction::triggered, this, &MainWindow::onFadeOut);

    QAction* crossfade = m->addAction(QStringLiteral("Кроссфейд с перекрывающимся клипом"));
    crossfade->setObjectName(QStringLiteral("actCrossfade"));
    connect(crossfade, &QAction::triggered, this, &MainWindow::onCrossfade);

    m->addSeparator();

    QAction* fit = m->addAction(QStringLiteral("Fit to Ref — под длительность референса"));
    fit->setObjectName(QStringLiteral("actFitRefMenu"));
    connect(fit, &QAction::triggered, this, &MainWindow::onFitToRef);

    QAction* align = m->addAction(QStringLiteral("Выровнять по началу референса"));
    align->setObjectName(QStringLiteral("actAlignMenu"));
    connect(align, &QAction::triggered, this, &MainWindow::onAlign);

    // Доступность пунктов обновляется при открытии меню.
    connect(m, &QMenu::aboutToShow, this, &MainWindow::onEditMenuAboutToShow);
}

void MainWindow::onEditMenuAboutToShow() {
    const bool haveClip = !selectedTakeId().isEmpty();
    const bool haveRange = [this] {
        std::uint64_t f, t;
        return timeline_->hasRange(f, t);
    }();
    auto setEnabled = [this](const char* name, bool on) {
        if (auto* a = findChild<QAction*>(name)) a->setEnabled(on);
    };
    setEnabled("actUndo", edits_->canUndo());
    setEnabled("actRedo", edits_->canRedo());
    setEnabled("actEditSplit", haveClip);
    setEnabled("actTrimSilence", haveClip);
    setEnabled("actTrimRange", haveClip && haveRange);
    setEnabled("actSilence", haveClip);
    setEnabled("actReverse", haveClip);
    setEnabled("actNormalize", haveClip);
    setEnabled("actGain", haveClip);
    setEnabled("actFadeIn", haveClip);
    setEnabled("actFadeOut", haveClip);
    setEnabled("actCrossfade", false); // уточняется ниже
    setEnabled("actFitRefMenu", haveClip);
    setEnabled("actAlignMenu", haveClip);

    // Человеческие имена команд для «Отменить/Повторить: …».
    auto editName = [](const std::string& a) -> QString {
        static const struct { const char* key; const char* ru; } kNames[] = {
            {"edit.trim_silence", "трим тишины"}, {"edit.trim_range", "обрезка диапазона"},
            {"edit.split", "разделение"},         {"edit.move", "перемещение"},
            {"edit.align", "выравнивание"},       {"edit.gain", "усиление"},
            {"edit.normalize", "нормализация"},   {"edit.silence", "тишина"},
            {"edit.reverse", "реверс"},           {"edit.fade_in", "фейд-ин"},
            {"edit.fade_out", "фейд-аут"},        {"edit.crossfade", "кроссфейд"},
            {"edit.fit_ref", "Fit to Ref"},       {"edit.delete", "удаление тейка"},
        };
        for (const auto& n : kNames)
            if (a == n.key) return QString::fromUtf8(n.ru);
        return QString();
    };
    if (auto* undo = findChild<QAction*>("actUndo")) {
        const std::string a = edits_->nextUndoAction();
        const QString ru = editName(a);
        undo->setText(ru.isEmpty() ? QStringLiteral("Отменить")
                                   : QStringLiteral("Отменить: %1").arg(ru));
    }
    if (auto* redo = findChild<QAction*>("actRedo")) {
        const std::string a = edits_->nextRedoAction();
        const QString ru = editName(a);
        redo->setText(ru.isEmpty() ? QStringLiteral("Повторить")
                                   : QStringLiteral("Повторить: %1").arg(ru));
    }
    // Кроссфейд: нужен перекрывающийся сосед у выбранного клипа.
    if (auto* cf = findChild<QAction*>("actCrossfade")) {
        cf->setEnabled(crossfadeNeighbor() != nullptr);
    }
}

QString MainWindow::selectedTakeId() const {
    const int idx = timeline_->selectedClip();
    if (!store_ || idx < 0 || idx >= static_cast<int>(store_->takes().size())) return {};
    return QString::fromStdString(store_->takes()[static_cast<std::size_t>(idx)].id);
}

const Clip* MainWindow::crossfadeNeighbor() {
    const int idx = timeline_->selectedClip();
    if (!store_ || idx < 0 || idx >= static_cast<int>(store_->takes().size())) return nullptr;
    const Clip& a = store_->takes()[static_cast<std::size_t>(idx)];
    const Clip* best = nullptr;
    std::uint64_t bestGap = 0;
    for (std::size_t i = 0; i < store_->takes().size(); ++i) {
        if (static_cast<int>(i) == idx) continue;
        const Clip& b = store_->takes()[i];
        const std::uint64_t o0 = std::max(a.startSample, b.startSample);
        const std::uint64_t o1 = std::min(a.startSample + a.samples.size(),
                                          b.startSample + b.samples.size());
        if (o1 > o0) {
            const std::uint64_t gap = b.startSample > a.startSample
                                          ? b.startSample - a.startSample
                                          : a.startSample - b.startSample;
            if (!best || gap < bestGap) {
                best = &b;
                bestGap = gap;
            }
        }
    }
    return best;
}

bool MainWindow::applyEdit(const EditCommand& cmd) {
    try {
        edits_->apply(cmd);
    } catch (const std::exception& e) {
        QMessageBox::warning(this, QStringLiteral("Правка"), QString::fromUtf8(e.what()));
        return false;
    }
    timeline_->syncTracks();
    refreshTableKeepingSelection(); // статус реплики мог измениться
    updateUndoStatus();
    markDirty();
    return true;
}

// refresh() модели с сохранением выделения строки реплики.
void MainWindow::refreshTableKeepingSelection() {
    const QString keepHash = currentWemHash();
    model_->refresh();
    restoreTableSelection(keepHash);
}

void MainWindow::updateUndoStatus() {
    if (!undoLabel_ || !edits_) return;
    const auto undoCount = db_->scalarInt(
        "SELECT COUNT(*) FROM undo_log WHERE scope='edit' AND undone=0;");
    const auto redoCount = db_->scalarInt(
        "SELECT COUNT(*) FROM undo_log WHERE scope='edit' AND undone=1;");
    undoLabel_->setText(QStringLiteral("Отмена: %1  Повтор: %2").arg(undoCount).arg(redoCount));
}

void MainWindow::markDirty() { dirtySinceAutosave_ = true; }

// Локальная позиция курсора внутри выбранного клипа.
static std::uint64_t localCursor(const TimelineWidget* tl, const Clip& c) {
    const std::uint64_t cur = tl->cursorSample();
    return cur > c.startSample ? cur - c.startSample : 0;
}

void MainWindow::onUndo() {
    if (!edits_->canUndo()) return;
    try {
        if (!edits_->undo()) return;
    } catch (const std::exception& e) {
        QMessageBox::warning(this, QStringLiteral("Отмена"), QString::fromUtf8(e.what()));
        return;
    }
    timeline_->syncTracks();
    refreshTableKeepingSelection();
    updateUndoStatus();
    markDirty();
}

void MainWindow::onRedo() {
    if (!edits_->canRedo()) return;
    try {
        if (!edits_->redo()) return;
    } catch (const std::exception& e) {
        QMessageBox::warning(this, QStringLiteral("Повтор"), QString::fromUtf8(e.what()));
        return;
    }
    timeline_->syncTracks();
    refreshTableKeepingSelection();
    updateUndoStatus();
    markDirty();
}

void MainWindow::onSplit() {
    const QString id = selectedTakeId();
    if (id.isEmpty()) {
        statusBar()->showMessage(QStringLiteral("Выберите клип на таймлайне"), 4000);
        return;
    }
    const Clip& c = store_->takes()[static_cast<std::size_t>(timeline_->selectedClip())];
    EditCommand cmd;
    cmd.type = EditType::Split;
    cmd.takeId = id.toStdString();
    cmd.from = localCursor(timeline_, c);
    if (applyEdit(cmd))
        statusBar()->showMessage(QStringLiteral("Клип разделён по курсору"), 4000);
}

void MainWindow::onTrimSilence() {
    const QString id = selectedTakeId();
    if (id.isEmpty()) return;
    EditCommand cmd;
    cmd.type = EditType::TrimSilence;
    cmd.takeId = id.toStdString();
    if (applyEdit(cmd))
        statusBar()->showMessage(QStringLiteral("Тишина по краям обрезана"), 4000);
}

void MainWindow::onTrimRange() {
    const QString id = selectedTakeId();
    if (id.isEmpty()) return;
    const Clip& c = store_->takes()[static_cast<std::size_t>(timeline_->selectedClip())];
    std::uint64_t gf, gt;
    if (!timeline_->hasRange(gf, gt)) {
        statusBar()->showMessage(
            QStringLiteral("Сначала задайте диапазон: клик по линейке + Shift+клик"), 6000);
        return;
    }
    EditCommand cmd;
    cmd.type = EditType::TrimRange;
    cmd.takeId = id.toStdString();
    cmd.from = gf > c.startSample ? gf - c.startSample : 0;
    cmd.to = gt > c.startSample ? gt - c.startSample : 0;
    if (applyEdit(cmd))
        statusBar()->showMessage(QStringLiteral("Обрезано вне диапазона"), 4000);
}

void MainWindow::onSilence() {
    const QString id = selectedTakeId();
    if (id.isEmpty()) return;
    const Clip& c = store_->takes()[static_cast<std::size_t>(timeline_->selectedClip())];
    std::uint64_t gf, gt;
    EditCommand cmd;
    cmd.type = EditType::Silence;
    cmd.takeId = id.toStdString();
    if (timeline_->hasRange(gf, gt)) {
        cmd.from = gf > c.startSample ? gf - c.startSample : 0;
        cmd.to = gt > c.startSample ? gt - c.startSample : 0;
    } // иначе to=0 -> весь клип
    if (applyEdit(cmd))
        statusBar()->showMessage(QStringLiteral("Диапазон заглушён"), 4000);
}

void MainWindow::onReverseRange() {
    const QString id = selectedTakeId();
    if (id.isEmpty()) return;
    const Clip& c = store_->takes()[static_cast<std::size_t>(timeline_->selectedClip())];
    std::uint64_t gf, gt;
    EditCommand cmd;
    cmd.type = EditType::Reverse;
    cmd.takeId = id.toStdString();
    if (timeline_->hasRange(gf, gt)) {
        cmd.from = gf > c.startSample ? gf - c.startSample : 0;
        cmd.to = gt > c.startSample ? gt - c.startSample : 0;
    }
    if (applyEdit(cmd))
        statusBar()->showMessage(QStringLiteral("Реверс применён"), 4000);
}

void MainWindow::onNormalize() {
    const QString id = selectedTakeId();
    if (id.isEmpty()) return;
    bool ok = false;
    const double target = QInputDialog::getDouble(
        this, QStringLiteral("Нормализация"),
        QStringLiteral("Целевой пик, dBFS:"), -3.0, -24.0, 0.0, 1, &ok);
    if (!ok) return;
    EditCommand cmd;
    cmd.type = EditType::Normalize;
    cmd.takeId = id.toStdString();
    cmd.dValue = target;
    if (applyEdit(cmd))
        statusBar()->showMessage(QStringLiteral("Пик нормализован к %1 dBFS").arg(target), 4000);
}

void MainWindow::onGain() {
    const QString id = selectedTakeId();
    if (id.isEmpty()) return;
    bool ok = false;
    const double gain = QInputDialog::getDouble(
        this, QStringLiteral("Усиление клипа"),
        QStringLiteral("Гейн, dB (применяется при воспроизведении):"),
        0.0, -60.0, 24.0, 1, &ok);
    if (!ok) return;
    EditCommand cmd;
    cmd.type = EditType::Gain;
    cmd.takeId = id.toStdString();
    cmd.dValue = gain;
    if (applyEdit(cmd))
        statusBar()->showMessage(QStringLiteral("Гейн клипа: %1 dB").arg(gain), 4000);
}

void MainWindow::onFadeIn() {
    const QString id = selectedTakeId();
    if (id.isEmpty()) return;
    const Clip& c = store_->takes()[static_cast<std::size_t>(timeline_->selectedClip())];
    EditCommand cmd;
    cmd.type = EditType::FadeIn;
    cmd.takeId = id.toStdString();
    cmd.from = localCursor(timeline_, c);
    if (applyEdit(cmd))
        statusBar()->showMessage(QStringLiteral("Фейд-ин до курсора"), 4000);
}

void MainWindow::onFadeOut() {
    const QString id = selectedTakeId();
    if (id.isEmpty()) return;
    const Clip& c = store_->takes()[static_cast<std::size_t>(timeline_->selectedClip())];
    EditCommand cmd;
    cmd.type = EditType::FadeOut;
    cmd.takeId = id.toStdString();
    cmd.from = localCursor(timeline_, c);
    if (applyEdit(cmd))
        statusBar()->showMessage(QStringLiteral("Фейд-аут от курсора"), 4000);
}

void MainWindow::onCrossfade() {
    const QString id = selectedTakeId();
    if (id.isEmpty()) return;
    const Clip* b = crossfadeNeighbor();
    if (!b) {
        statusBar()->showMessage(
            QStringLiteral("Нет клипа, перекрывающего выбранный по времени"), 6000);
        return;
    }
    const std::string secondId = b->id; // applyEdit удалит клип из store
    EditCommand cmd;
    cmd.type = EditType::Crossfade;
    cmd.takeId = id.toStdString();
    cmd.takeId2 = secondId;
    if (applyEdit(cmd))
        statusBar()->showMessage(
            QStringLiteral("Кроссфейд: слит с %1").arg(QString::fromStdString(secondId)), 5000);
}

void MainWindow::onFitToRef() {
    const QString id = selectedTakeId();
    if (id.isEmpty()) {
        statusBar()->showMessage(QStringLiteral("Выберите клип на таймлайне"), 4000);
        return;
    }
    EditCommand cmd;
    cmd.type = EditType::FitToRef;
    cmd.takeId = id.toStdString();
    if (applyEdit(cmd))
        statusBar()->showMessage(
            QStringLiteral("Fit to Ref: тейк растянут под референс (питч сохранён)"), 5000);
}

void MainWindow::onAlign() {
    const QString id = selectedTakeId();
    if (id.isEmpty()) return;
    EditCommand cmd;
    cmd.type = EditType::Align;
    cmd.takeId = id.toStdString();
    if (applyEdit(cmd))
        statusBar()->showMessage(QStringLiteral("Клип выровнен по началу референса"), 4000);
}

void MainWindow::onDeleteTake(int clipIndex) {
    if (clipIndex < 0 || clipIndex >= static_cast<int>(store_->takes().size())) return;
    const Clip& c = store_->takes()[static_cast<std::size_t>(clipIndex)];
    const QString title = QString::fromStdString(c.title);
    const QString id = QString::fromStdString(c.id);
    const auto answer = QMessageBox::question(
        this, QStringLiteral("Удаление тейка"),
        QStringLiteral("Удалить тейк %1 полностью?\n(вернуть можно через Ctrl+Z)").arg(title),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) return;
    EditCommand cmd;
    cmd.type = EditType::DeleteTake;
    cmd.takeId = id.toStdString();
    if (applyEdit(cmd))
        statusBar()->showMessage(
            QStringLiteral("Тейк %1 удалён (Ctrl+Z вернёт)").arg(title), 5000);
}

void MainWindow::onClipMoved(int clipIndex, std::uint64_t newStart) {
    if (clipIndex < 0 || clipIndex >= static_cast<int>(store_->takes().size())) return;
    // Живой drag уже сдвинул клип — фиксируем позицию командой Move (undo!).
    EditCommand cmd;
    cmd.type = EditType::Move;
    cmd.takeId = store_->takes()[static_cast<std::size_t>(clipIndex)].id;
    cmd.uValue = newStart;
    applyEdit(cmd);
}

void MainWindow::onProjectSettings() {
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("Проект"));
    auto* form = new QFormLayout(&dlg);

    QSettings pre;
    auto* enabled = new QCheckBox(QStringLiteral("Автосейв включён"), &dlg);
    enabled->setChecked(pre.value(QStringLiteral("project/autosaveEnabled"), true).toBool());
    form->addRow(QString(), enabled);

    auto* minutes = new QSpinBox(&dlg);
    minutes->setRange(1, 120);
    minutes->setValue(pre.value(QStringLiteral("project/autosaveMin"), 5).toInt());
    minutes->setSuffix(QStringLiteral(" мин"));
    form->addRow(QStringLiteral("Интервал автосейва:"), minutes);

    auto* onlyChanged = new QCheckBox(
        QStringLiteral("Сохранять только если были изменения"), &dlg);
    onlyChanged->setChecked(
        pre.value(QStringLiteral("project/autosaveOnlyIfChanged"), true).toBool());
    form->addRow(QString(), onlyChanged);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(buttons);

    if (dlg.exec() != QDialog::Accepted) return;
    QSettings s;
    s.setValue(QStringLiteral("project/autosaveEnabled"), enabled->isChecked());
    s.setValue(QStringLiteral("project/autosaveMin"), minutes->value());
    s.setValue(QStringLiteral("project/autosaveOnlyIfChanged"), onlyChanged->isChecked());
}

void MainWindow::onAutosaveTick() {
    QSettings s;
    if (!s.value(QStringLiteral("project/autosaveEnabled"), true).toBool()) return;
    const int intervalMin = s.value(QStringLiteral("project/autosaveMin"), 5).toInt();
    const bool onlyChanged =
        s.value(QStringLiteral("project/autosaveOnlyIfChanged"), true).toBool();
    if (onlyChanged && !dirtySinceAutosave_) return;
    if (lastAutosave_.secsTo(QDateTime::currentDateTime()) < intervalMin * 60) return;
    try {
        edits_->autosaveSnapshot();
        lastAutosave_ = QDateTime::currentDateTime();
        dirtySinceAutosave_ = false;
        suppressAutosaveError_ = false;
        autosaveLabel_->setText(
            QStringLiteral("Автосейв: %1").arg(lastAutosave_.toString(QStringLiteral("HH:mm"))));
    } catch (const std::exception& e) {
        // Не спамим: одна ошибка на серию неудач.
        if (!suppressAutosaveError_) {
            suppressAutosaveError_ = true;
            statusBar()->showMessage(QString::fromUtf8(e.what()), 8000);
        }
    }
}

// Фильтр таймлайна по выбранной реплике: показываем только её тейки
// (все тейки остаются в ClipStore и в undo-истории).
void MainWindow::connectTableSelection() {
    connect(table_->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
            &MainWindow::onTableLineChanged);
}

void MainWindow::onTableLineChanged() {
    const QString hash = currentWemHash();
    if (hash.isEmpty()) {
        timeline_->clearLineFilter(); // ничего не выбрано — дорожки тейков пусты
    } else {
        timeline_->setLineFilter(hash.toStdString());
    }
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
        connectTableSelection(); // у новой модели свой selectionModel
        // Фаза 2: стек правок смотрит на новую БД, тейки перезагружаем.
        store_ = std::make_unique<ClipStore>();
        edits_ = std::make_unique<EditStack>(*db_, *store_, myDubDir_.toStdString());
        timeline_->setStore(store_.get());
        const EditStack::SessionInfo session = edits_->loadSession();
        takeCounter_ = session.maxTakeNum;
        timeline_->syncTracks();
        updateUndoStatus();
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
    // Дерево по умолчанию свёрнуто: 42 файла не раскрывают всё подряд,
    // пользователь сам разворачивает нужный файл (первый запуск/импорт).
    tree_->collapseAll();
}

// Вернуть выделение строки реплики по wem_hash после refresh/смены фильтра:
// строка не слетает, пока пользователь сам не переключится в таблице.
void MainWindow::restoreTableSelection(const QString& wemHash) {
    if (wemHash.isEmpty() || !model_) return;
    const int rows = model_->rowCount();
    for (int i = 0; i < rows; ++i) {
        if (model_->wemHashAt(i) == wemHash) {
            const QModelIndex idx = model_->index(i, 0);
            table_->setCurrentIndex(idx);
            table_->scrollTo(idx, QAbstractItemView::PositionAtCenter);
            return;
        }
    }
}

// ПКМ внутри выделенного диапазона на таймлайне: меню правок диапазона.
void MainWindow::onRangeContextMenu(const QPoint& globalPos) {
    const bool haveClip = !selectedTakeId().isEmpty();
    QMenu menu(this);
    auto addRange = [&](const QString& text, auto handler) {
        QAction* a = menu.addAction(text);
        a->setEnabled(haveClip);
        connect(a, &QAction::triggered, this, handler);
    };
    addRange(QStringLiteral("Тишина в диапазоне"), &MainWindow::onSilence);
    addRange(QStringLiteral("Реверс диапазона"), &MainWindow::onReverseRange);
    addRange(QStringLiteral("Обрезать вне диапазона"), &MainWindow::onTrimRange);
    menu.addSeparator();
    addRange(QStringLiteral("Нормализовать…"), &MainWindow::onNormalize);
    addRange(QStringLiteral("Усиление…"), &MainWindow::onGain);
    menu.addSeparator();
    addRange(QStringLiteral("Фейд-ин до курсора"), &MainWindow::onFadeIn);
    addRange(QStringLiteral("Фейд-аут от курсора"), &MainWindow::onFadeOut);
    addRange(QStringLiteral("Разделить по курсору"), &MainWindow::onSplit);
    menu.addSeparator();
    addRange(QStringLiteral("Fit to Ref"), &MainWindow::onFitToRef);
    if (!haveClip) {
        QAction* hint = menu.addAction(
            QStringLiteral("Сначала выделите тейк (клик/выделение по его дорожке)"));
        hint->setEnabled(false);
    }
    menu.exec(globalPos);
}

void MainWindow::onTreeSelection() {
    const QString keepHash = currentWemHash();
    const QModelIndexList selected = tree_->selectionModel()->selectedIndexes();
    if (selected.isEmpty()) {
        model_->setFilter({}, {}, search_->text());
    } else {
        const QModelIndex idx = selected.first();
        const QString questId = idx.data(kRoleQuestId).toString();
        const QString fileId = idx.data(kRoleFileId).toString();
        // Выбрана сцена — фильтруем по ней; выбран файл — по файлу.
        model_->setFilter(questId.isEmpty() ? fileId : QString(), questId, search_->text());
    }
    restoreTableSelection(keepHash);
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

} // namespace dubstudio
