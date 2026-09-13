// MainWindow Фазы 0+1: тёмная тема, дерево файл->сцена, таблица реплик с FTS,
// + аудио-транспорт (запись/плей/метроном/мониторинг), таймлайн Track 0/1/N
// с волноформой и zoom (PLAN.md 6, 13, 15.4).
#pragma once

#include <QMainWindow>
#include <QString>

#include <memory>

class QLabel;
class QLineEdit;
class QStandardItem;
class QStandardItemModel;
class QTableView;
class QTimer;
class QTreeView;

namespace dubstudio {

class AudioEngine;
class ClipStore;
class Database;
class LevelMeter;
class LinesSqlModel;
class TimelineWidget;

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

    // Фаза 1: транспорт
    void onRecord();
    void onPlay();
    void onStop();
    void onAudioSettings();
    void onTick();

private:
    void buildUi();
    void buildMenu();
    void buildTransport();
    void rebuildTree();
    void reloadStats();
    void finalizeTake();
    QString currentWemHash() const;
    bool openAudioDevice(const QString& deviceId, unsigned int sampleRate,
                         unsigned int bufferFrames);
    void updateAudioStatus(); // индикатор звука в статус-баре

    std::unique_ptr<Database> db_;
    QString dbPath_;

    LinesSqlModel* model_ = nullptr;
    QTreeView* tree_ = nullptr;
    QStandardItemModel* treeModel_ = nullptr;
    QTableView* table_ = nullptr;
    QLineEdit* search_ = nullptr;
    QLabel* statsLabel_ = nullptr;
    QLabel* dbLabel_ = nullptr;

    // Фаза 1
    std::unique_ptr<AudioEngine> engine_;
    std::unique_ptr<ClipStore> store_;
    TimelineWidget* timeline_ = nullptr;
    QTimer* tickTimer_ = nullptr;
    LevelMeter* level_ = nullptr;
    QLabel* xrunLabel_ = nullptr;
    QLabel* recTimeLabel_ = nullptr;
    QLabel* audioLabel_ = nullptr; // устройство/частота/буфер
    QString audioDeviceId_;      // "API:deviceId"
    unsigned int sampleRate_ = 48000;
    unsigned int bufferFrames_ = 256;
    int takeCounter_ = 0;
    int liveTakeIndex_ = -1;     // индекс клипа живой записи в ClipStore
};

void applyDarkTheme(); // выставить Fusion + тёмная палитра (до создания окна)

} // namespace dubstudio
