// MainWindow Фазы 0+1: тёмная тема, дерево файл->сцена, таблица реплик с FTS,
// + аудио-транспорт (запись/плей/метроном/мониторинг), таймлайн Track 0/1/N
// с волноформой и zoom (PLAN.md 6, 13, 15.4).
#pragma once

#include <QDateTime>
#include <QMainWindow>
#include <QString>

#include <cstdint>
#include <memory>

class QLabel;
class QLineEdit;
class QStandardItem;
class QStandardItemModel;
class QTableView;
class QTimer;
class QTreeView;

namespace dubstudio {

struct Clip;
class AudioEngine;
class ClipStore;
class Database;
class EditStack;
class LevelMeter;
class LinesSqlModel;
class TimelineWidget;

struct EditCommand;
enum class EditType;

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

    // Фаза 2: редактура (PLAN.md 6.4)
    void onUndo();
    void onRedo();
    void onEditMenuAboutToShow();
    void onSplit();
    void onTrimSilence();
    void onTrimRange();
    void onSilence();
    void onReverseRange();
    void onNormalize();
    void onGain();
    void onFadeIn();
    void onFadeOut();
    void onCrossfade();
    void onFitToRef();
    void onAlign();
    void onClipMoved(int clipIndex, std::uint64_t newStart);
    void onProjectSettings();
    void onAutosaveTick();
    void onTableLineChanged();  // фильтр таймлайна по выбранной реплике
    void onDeleteTake(int clipIndex); // кнопка «✕» в хедере дорожки

private:
    void buildUi();
    void buildMenu();
    void buildTransport();
    void buildEditMenuActions(); // Правка + хоткеи S/Ctrl+Z/Ctrl+Y (Фаза 2)
    void connectTableSelection(); // selectionModel пересоздаётся со сменой модели
    void rebuildTree();
    void reloadStats();
    void finalizeTake();
    QString currentWemHash() const;
    bool openAudioDevice(const QString& deviceId, unsigned int sampleRate,
                         unsigned int bufferFrames);
    void updateAudioStatus(); // индикатор звука в статус-баре

    // Фаза 2
    bool applyEdit(const EditCommand& cmd);       // команда + статус-бар + обновление UI
    QString selectedTakeId() const;               // id выбранного в таймлайне клипа
    const Clip* crossfadeNeighbor();              // клип, перекрывающий выбранный (или nullptr)
    void updateUndoStatus();                      // счётчики undo/redo в статус-баре
    void markDirty();                             // автосейв: были изменения

    std::unique_ptr<Database> db_;
    QString dbPath_;

    QString myDubDir_; // рабочая директория проекта (PLAN.md 12)

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

    // Фаза 2
    std::unique_ptr<EditStack> edits_;
    QTimer* autosaveTimer_ = nullptr;
    QLabel* undoLabel_ = nullptr;   // undo/redo счётчики
    QLabel* autosaveLabel_ = nullptr; // последний автосейв
    QDateTime lastAutosave_;
    bool dirtySinceAutosave_ = false;
    bool suppressAutosaveError_ = false;
};

void applyDarkTheme(); // выставить Fusion + тёмная палитра (до создания окна)

} // namespace dubstudio
