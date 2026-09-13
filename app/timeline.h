// TimelineWidget (Фаза 1): треки Track 0 REF-EN (locked) / MASTER-RU / TAKE-NN,
// волноформа с zoom колесом до сэмплов, линейка времени, playhead.
// PLAN.md 6.2, 6.4, 13.
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

    QSize minimumSizeHint() const override;

signals:
    void infoChanged(const QString& info); // текущий zoom/позиция в статус-бар

protected:
    void paintEvent(QPaintEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

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

    AudioEngine* engine_ = nullptr;
    ClipStore* store_ = nullptr;

    std::vector<Row> rows_;
    double samplesPerPixel_ = 512.0; // zoom: от full-view до 1/64 сэмпла на пиксель
    std::uint64_t viewStart_ = 0;    // первый видимый сэмпл

    bool dragging_ = false;
    int dragLastX_ = 0;

    static constexpr int kHeaderW = 150;
    static constexpr int kRowH = 64;
    static constexpr int kRulerH = 22;
};

} // namespace dubstudio
