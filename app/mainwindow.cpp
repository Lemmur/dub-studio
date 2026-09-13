#include "mainwindow.h"

#include "levelmeter.h"
#include "linesmodel.h"
#include "timeline.h"

#include "dubstudio/audio_engine.h"
#include "dubstudio/clip_store.h"
#include "dubstudio/database.h"
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
#include <QLabel>
#include <QLineEdit>
#include <QKeySequence>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPalette>
#include <QSettings>
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

    engine_ = std::make_unique<AudioEngine>();
    store_ = std::make_unique<ClipStore>();

    buildUi();
    buildMenu();
    buildTransport();
    rebuildTree();
    reloadStats();

    // Попытка автооткрытия аудио (последнее выбранное или первый ASIO).
    QSettings s;
    sampleRate_ = s.value(QStringLiteral("audio/sampleRate"), 48000).toUInt();
    bufferFrames_ = s.value(QStringLiteral("audio/buffer"), 256).toUInt();
    engine_->setTempo(s.value(QStringLiteral("metro/bpm"), 100).toInt(),
                      s.value(QStringLiteral("metro/beats"), 4).toInt());
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

    tickTimer_ = new QTimer(this);
    tickTimer_->setInterval(33); // ~30 Гц: метры/playhead/живая волноформа
    connect(tickTimer_, &QTimer::timeout, this, &MainWindow::onTick);
    tickTimer_->start();
}

MainWindow::~MainWindow() = default;

void MainWindow::buildUi() {
    setWindowTitle(QStringLiteral("DubStudio — Фаза 1 (аудио)"));
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

    connect(search_, &QLineEdit::textChanged, this, &MainWindow::onSearchChanged);

    // --- Нижний док: таймлайн Track 0/1/N (Фаза 1) ---
    auto* tlDock = new QDockWidget(QStringLiteral("Таймлайн"), this);
    tlDock->setObjectName(QStringLiteral("timelineDock"));
    tlDock->setAllowedAreas(Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea);
    timeline_ = new TimelineWidget(tlDock);
    timeline_->setEngine(engine_.get());
    timeline_->setStore(store_.get());
    tlDock->setWidget(timeline_);
    addDockWidget(Qt::BottomDockWidgetArea, tlDock);

    // --- Статус-бар ---
    statsLabel_ = new QLabel(this);
    dbLabel_ = new QLabel(this);
    xrunLabel_ = new QLabel(this);
    recTimeLabel_ = new QLabel(this);
    level_ = new LevelMeter(this);
    statusBar()->addWidget(statsLabel_, 1);
    statusBar()->addWidget(recTimeLabel_);
    statusBar()->addWidget(xrunLabel_);
    statusBar()->addWidget(level_);
    statusBar()->addPermanentWidget(dbLabel_);
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

    // Аудио (Фаза 1)
    QMenu* audioMenu = menuBar()->addMenu(QStringLiteral("&Аудио"));
    QAction* settings = audioMenu->addAction(QStringLiteral("Настройки аудио…"));
    settings->setShortcut(QKeySequence(QStringLiteral("Ctrl+U")));
    connect(settings, &QAction::triggered, this, &MainWindow::onAudioSettings);

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
            QStringLiteral("DubStudio — Фаза 1 (аудио).\n\n"
                            "RtAudio + ASIO/WASAPI, запись моно 48 кГц/24-bit,\n"
                            "треки Track 0 REF-EN / MASTER-RU / TAKE-N,\n"
                            "волноформа с zoom, метроном, мониторинг.\n\n"
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
                statusBar()->showMessage(
                    QStringLiteral("Аудио: %1 [%2] %3 Гц, буфер %4")
                        .arg(QString::fromStdString(d.name), QString::fromStdString(d.apiName))
                        .arg(sampleRate_)
                        .arg(bufferFrames_), 8000);
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

    auto* direct = new QCheckBox(QStringLiteral("Direct Monitoring (аппаратный, софт-копия выключается)"), &dlg);
    form->addRow(QString(), direct);

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
    engine_->setTempo(engine_->bpm(), beatsSpin->value());
    QSettings s;
    s.setValue(QStringLiteral("audio/directMonitor"), direct->isChecked());
    s.setValue(QStringLiteral("metro/beats"), beatsSpin->value());
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
    Clip clip;
    clip.id = QStringLiteral("take_%1_%2").arg(takeCounter_).arg(hash.left(8)).toStdString();
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
    const auto& takes = store_->takes();
    if (takes.empty()) {
        statusBar()->showMessage(QStringLiteral("Нет тейков для плейбека"), 3000);
        return;
    }
    // Играем последний тейк (живой индекс приоритетнее).
    const std::size_t idx = liveTakeIndex_ >= 0 && liveTakeIndex_ < static_cast<int>(takes.size())
                                ? static_cast<std::size_t>(liveTakeIndex_)
                                : takes.size() - 1;
    engine_->play(takes[idx].samples.data(), takes[idx].samples.size());
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
    const char* err = nullptr;
    const char* sql =
        "BEGIN;"
        "INSERT INTO takes(take_id, wem_hash, file_cas, duration_ms, quality, rms_db, peak_db,"
        " is_master_candidate, comment) VALUES(?1,?2,?3,?4,?5,?6,?7,0,'Фаза 1: запись');"
        "INSERT INTO undo_log(action) VALUES('take.record:' || ?1);"
        "UPDATE lines SET status='recorded' WHERE wem_hash=?2;"
        "COMMIT;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_->handle(), sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, takeId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, hash.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, clip.filePath.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 4, durMs);
        sqlite3_bind_text(stmt, 5, quality.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(stmt, 6, clip.rmsDb);
        sqlite3_bind_double(stmt, 7, clip.peakDb);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    } else {
        err = sqlite3_errmsg(db_->handle());
    }
    if (err) {
        QMessageBox::warning(this, QStringLiteral("Ошибка БД"), QString::fromUtf8(err));
    }

    liveTakeIndex_ = -1;
    timeline_->syncTracks();
    model_->refresh();
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
    }
    level_->setLevel(engine_->inputPeak());
    const auto xruns = engine_->xrunCount();
    xrunLabel_->setText(xruns
                            ? QStringLiteral("Xrun: %1").arg(xruns)
                            : QStringLiteral("Xrun: 0"));
    timeline_->tick();
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

} // namespace dubstudio
