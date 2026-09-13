// TimelineWidget (Фаза 1+2): треки Track 0 REF-EN (locked) / MASTER-RU / TAKE-NN,
// волноформа с zoom колесом до сэмплов, линейка времени, playhead.
// Фаза 2: клипы живут по startSample, выбор клипа кликом, drag-move клипа,
// курсор (клик по линейке) + диапазон (Shift+клик), отрисовка фейдов (PLAN.md 6.4).
#pragma once

#include <QWidget>

#include <cstdint>
#include <vector>

namespace dubstudio {

class AudioEngine;
class ClipStore;

class TimelineWidget : public QWidget {
    Q_OBJECT
public:
    explicit TimelineWidget(QWidget* parent = nullptr);

    void setEngine(AudioEngine* engine) { engine_ = engine; }
    void setStore(ClipStore* store) { store_ = store; }

    // Обновить список треков по текущим тейкам ClipStore (Track 0/1 фиксированы).
    void syncTracks();

    // Транспорт дергает: перерисовать playhead/уровни (30 Гц).
    void tick();

    // --- Фаза 2: редактура -----------------------------------------------------
    // Фильтр дорожек по реплике: тейки на таймлайне — только выбранной реплики
    // (wem_hash). Пока реплика не выбрана — дорожки тейков пусты (clearLineFilter).
    void setLineFilter(const std::string& wemHash);
    void clearLineFilter();
    bool lineFilterActive() const { return lineFilterActive_; }
    const std::string& lineFilter() const { return lineFilter_; }

    int selectedClip() const { return selected_; } // индекс в ClipStore, -1 = нет
    void selectClip(int clipIndex);                // подсветка + перерисовка
    std::uint64_t cursorSample() const { return cursorSample_; }
    // setCursor НЕ переопределяем: он скрыл бы QWidget::setCursor(QCursor),
    // и setCursor(Qt::ArrowCursor) в release молча обнулял курсор
    // (Qt::ArrowCursor == 0 → cursorSample_ = 0).
    // Диапазон Shift+клика в глобальных сэмплах таймлайна; false если пуст.
    bool hasRange(std::uint64_t& from, std::uint64_t& to) const;

    QSize minimumSizeHint() const override;

signals:
    void infoChanged(const QString& info); // текущий zoom/позиция в статус-бар
    // Пользователь отпустил drag-move клипа: MainWindow применяет команду Move.
    void clipMoved(int clipIndex, std::uint64_t newStartSample);
    // Кнопка «✕» в хедере дорожки тейка: MainWindow применяет DeleteTake.
    void clipDeleteRequested(int clipIndex);
    // ПКМ внутри выделенного диапазона: MainWindow показывает меню правок.
    void rangeContextMenuRequested(const QPoint& globalPos);
    void selectionChanged();

protected:
    void paintEvent(QPaintEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    struct Row {
        bool isFixed;       // Track 0/1
        QString title;      // "Track 0 REF-EN", "TAKE-01"...
        std::uint32_t color;
        int clipIndex;      // индекс в ClipStore, -1 = пустой
        bool locked;
    };

    void buildRows();
    int rowAt(int y) const;
    double contentSamples() const; // полная длина контента в сэмплах
    void clampView();
    void zoomAt(double x, double factor);
    std::uint64_t playheadSample() const;
    std::uint64_t sampleAtX(double x) const;          // сэмпл таймлайна по x виджета
    int clipHit(double x, double y) const;            // индекс клипа или -1 (locked пропускаем)

    AudioEngine* engine_ = nullptr;
    ClipStore* store_ = nullptr;
    std::string lineFilter_;      // wem_hash выбранной реплики
    bool lineFilterActive_ = false; // false = реплика не выбрана -> тейков не показываем

    std::vector<Row> rows_;
    double samplesPerPixel_ = 512.0; // zoom: от full-view до 1/64 сэмпла на пиксель
    std::uint64_t viewStart_ = 0;    // первый видимый сэмпл

    bool dragging_ = false;   // панорама (СКМ или Alt+ЛКМ)
    int dragLastX_ = 0;
    // ЛКМ по дорожке: press = курсор; drag дальше 2px = выделение диапазона
    // (анкер стоит в точке нажатия); без движения = обычная установка курсора.
    bool cursorPress_ = false;
    bool selecting_ = false;
    double pressX_ = 0.0;
    int rangeRow_ = -1; // дорожка, где начато выделение (-1 = все, напр. с линейки)

    // Фаза 2
    int selected_ = -1;              // выбранный клип (индекс ClipStore)
    std::uint64_t cursorSample_ = 0; // позиция курсора редактирования
    std::uint64_t anchorSample_ = 0; // якорь диапазона (Shift+клик двигает курсор)
    bool movingClip_ = false;        // drag-move выбранного клипа
    double movePressX_ = 0.0;        // x захвата
    std::uint64_t moveStartBegin_ = 0; // startSample клипа на момент захвата

    static constexpr int kHeaderW = 150;
    static constexpr int kRowH = 64;
    static constexpr int kRulerH = 22;
    static constexpr int kMiniRulerH = 10; // мини-линейка над КАЖДОЙ дорожкой
    int rowStride() const { return kRowH + kMiniRulerH; }
    int rowTop(int r) const { return kRulerH + kMiniRulerH + r * rowStride(); }
    int miniTop(int r) const { return kRulerH + r * rowStride(); }
    bool inMiniRuler(int y) const {
        return y >= kRulerH && ((y - kRulerH) % rowStride()) < kMiniRulerH;
    }
    void setCursorAt(double x); // курсор = анкер (курсор редактирования)
};

} // namespace dubstudio
