// TimelineWidget: отрисовка треков/волноформ/линейки/playhead (Фаза 1).
#include "timeline.h"

#include "dubstudio/audio_engine.h"
#include "dubstudio/clip_store.h"

#include <QFont>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace dubstudio {
namespace {

constexpr int kMinRowH = 3; // минимум строк под запас

QString formatTime(double seconds, bool ms) {
    if (seconds >= 60.0) {
        const int m = static_cast<int>(seconds) / 60;
        const double s = seconds - m * 60;
        return QString::fromUtf8("%1:%2").arg(m).arg(s, 5, 'f', ms ? 2 : 1, '0');
    }
    return QString::number(seconds, 'f', ms ? 3 : 2);
}

} // namespace

TimelineWidget::TimelineWidget(QWidget* parent) : QWidget(parent) {
    setMouseTracking(false);
    setMinimumHeight(kRulerH + kRowH * (2 + kMinRowH));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

QSize TimelineWidget::minimumSizeHint() const {
    return QSize(600, kRulerH + kRowH * 4);
}

void TimelineWidget::buildRows() {
    rows_.clear();
    Row ref;
    ref.isFixed = true;
    ref.title = QStringLiteral("Track 0 REF-EN");
    ref.color = 0x9d3b3b;
    ref.clipIndex = -1;
    ref.locked = true; // PLAN.md 6.2: locked, только mute/solo
    rows_.push_back(ref);

    Row master;
    master.isFixed = true;
    master.title = QStringLiteral("Track 1 MASTER-RU");
    master.color = 0x3b9d6e;
    master.clipIndex = -1;
    master.locked = false;
    rows_.push_back(master);

    if (store_) {
        const auto& takes = store_->takes();
        for (std::size_t i = 0; i < takes.size(); ++i) {
            Row r;
            r.isFixed = false;
            r.title = QString::fromStdString(takes[i].title);
            r.color = takes[i].colorRgb;
            r.clipIndex = static_cast<int>(i);
            r.locked = false;
            rows_.push_back(r);
        }
    }
}

void TimelineWidget::syncTracks() {
    buildRows();
    // Фиксированная высота = все дорожки: вертикальный скролл даёт
    // обёртка QScrollArea (MainWindow::buildUi), виджет не обрезается.
    setMinimumHeight(kRulerH + static_cast<int>(rows_.size()) * kRowH);
    clampView();
    update();
    updateGeometry();
}

double TimelineWidget::contentSamples() const {
    double len = 30.0 * 48000; // минимум 30 сек канвы
    if (store_) {
        for (const auto& c : store_->takes()) {
            len = std::max<double>(len, static_cast<double>(c.samples.size()));
        }
    }
    return len;
}

void TimelineWidget::clampView() {
    const double w = std::max(1.0, static_cast<double>(width() - kHeaderW));
    const double visible = w * samplesPerPixel_;
    const double total = contentSamples();
    if (viewStart_ > total) viewStart_ = static_cast<std::uint64_t>(std::max(0.0, total - visible));
}

void TimelineWidget::zoomAt(double x, double factor) {
    // x — позиция мыши в виджете; якорь зума под курсором.
    const double sampleAtCursor = static_cast<double>(viewStart_) + (x - kHeaderW) * samplesPerPixel_;
    const double newSpp = std::clamp(samplesPerPixel_ * factor, 1.0 / 64.0, 4096.0);
    samplesPerPixel_ = newSpp;
    viewStart_ = static_cast<std::uint64_t>(
        std::max(0.0, sampleAtCursor - (x - kHeaderW) * newSpp));
    clampView();
    update();
    emit infoChanged(QStringLiteral("Zoom: %1 сэмпл/пикс").arg(samplesPerPixel_, 0, 'f', 2));
}

void TimelineWidget::wheelEvent(QWheelEvent* event) {
    const double delta = event->angleDelta().y();
    zoomAt(static_cast<double>(event->position().x()), delta > 0 ? 0.8 : 1.25);
}

void TimelineWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && event->position().x() > kHeaderW) {
        dragging_ = true;
        dragLastX_ = static_cast<int>(event->position().x());
        setCursor(Qt::ClosedHandCursor);
    }
}

void TimelineWidget::mouseMoveEvent(QMouseEvent* event) {
    if (!dragging_) return;
    const int dx = static_cast<int>(event->position().x()) - dragLastX_;
    dragLastX_ = static_cast<int>(event->position().x());
    const double shiftSamples = -dx * samplesPerPixel_;
    const double maxStart = std::max(0.0, contentSamples() - (width() - kHeaderW) * samplesPerPixel_);
    viewStart_ = static_cast<std::uint64_t>(
        std::clamp(static_cast<double>(viewStart_) + shiftSamples, 0.0, maxStart));
    update();
}

void TimelineWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        dragging_ = false;
        setCursor(Qt::ArrowCursor);
    }
}

void TimelineWidget::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        // Двойной клик: показать всё.
        samplesPerPixel_ = contentSamples() / std::max(1.0, static_cast<double>(width() - kHeaderW));
        viewStart_ = 0;
        update();
    }
}

int TimelineWidget::rowAt(int y) const {
    const int idx = (y - kRulerH) / kRowH;
    if (idx < 0 || idx >= static_cast<int>(rows_.size())) return -1;
    return idx;
}

std::uint64_t TimelineWidget::playheadSample() const {
    if (!engine_) return 0;
    if (engine_->isRecording()) return engine_->recordedFrames();
    return engine_->playheadFrames();
}

void TimelineWidget::tick() {
    // Автоскролл за playhead во время записи/плейбека.
    const std::uint64_t ph = playheadSample();
    const double x = (static_cast<double>(ph) - static_cast<double>(viewStart_)) / samplesPerPixel_;
    const double w = static_cast<double>(width() - kHeaderW);
    if (x > w - 40.0 || x < 0.0) {
        viewStart_ = static_cast<std::uint64_t>(
            std::max(0.0, static_cast<double>(ph) - w * 0.25));
        clampView();
    }
    update();
}

void TimelineWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);

    const int waveW = width() - kHeaderW;
    if (waveW <= 0) return;

    // --- Фон ---
    p.fillRect(rect(), QColor(0x1e, 0x1e, 0x1e));

    // --- Линейка ---
    p.fillRect(0, 0, width(), kRulerH, QColor(0x2b, 0x2b, 0x2b));
    p.setPen(QColor(0x88, 0x88, 0x88));
    const double secondsVisible = waveW * samplesPerPixel_ / 48000.0;
    // Шаг делений: мин 70 px между метками.
    static const double kSteps[] = {3600, 600, 60, 30, 10, 5, 2, 1, 0.5, 0.2, 0.1, 0.05, 0.02,
                                    0.01, 0.005, 0.002, 0.001};
    double step = kSteps[0];
    for (double s : kSteps) {
        const double px = s / secondsVisible * waveW;
        if (px >= 70.0) { step = s; break; }
        step = s;
    }
    const double startSec = static_cast<double>(viewStart_) / 48000.0;
    const double endSec = startSec + secondsVisible;
    const bool ms = step < 1.0;
    QFont smallFont = font();
    smallFont.setPointSize(8);
    p.setFont(smallFont);
    for (double t = std::floor(startSec / step) * step; t <= endSec; t += step) {
        const int x = kHeaderW + static_cast<int>((t - startSec) / secondsVisible * waveW);
        p.drawLine(x, 12, x, kRulerH);
        p.drawText(x + 3, 11, formatTime(t, ms));
    }
    p.setPen(QColor(0x55, 0x55, 0x55));
    p.drawLine(0, kRulerH - 1, width(), kRulerH - 1);
// --- Строки треков ---
const int nRows = static_cast<int>(rows_.size());
Q_UNUSED(nRows);

    std::vector<float> mins, maxs;
    for (int r = 0; r < nRows; ++r) {
        const Row& row = rows_[static_cast<std::size_t>(r)];
        const int y0 = kRulerH + r * kRowH;

        // Хедер трека.
        p.fillRect(0, y0, kHeaderW, kRowH, QColor(0x26, 0x26, 0x26));
        p.setPen(Qt::NoPen);
        p.setBrush(QColor((row.color >> 16) & 0xFF, (row.color >> 8) & 0xFF, row.color & 0xFF));
        p.drawRect(6, y0 + 6, 8, kRowH - 12);
        p.setPen(QColor(0xe0, 0xe0, 0xe0));
        QFont f = font();
        f.setPointSize(9);
        p.setFont(f);
        p.drawText(QRect(20, y0, kHeaderW - 24, kRowH),
                   Qt::AlignVCenter | Qt::AlignLeft,
                   row.title + (row.locked ? QStringLiteral("  [lock]") : QString()));
        if (row.locked) {
            p.setPen(QColor(0x70, 0x70, 0x70));
            p.drawText(QRect(20, y0, kHeaderW - 24, kRowH),
                       Qt::AlignBottom | Qt::AlignLeft, QStringLiteral("locked"));
        }

        // Полотно волны.
        p.fillRect(kHeaderW, y0, waveW, kRowH, QColor(0x19, 0x19, 0x19));
        p.setPen(QColor(0x2f, 0x2f, 0x2f));
        p.drawLine(kHeaderW, y0 + kRowH / 2, width(), y0 + kRowH / 2);

        if (store_ && row.clipIndex >= 0 &&
            row.clipIndex < static_cast<int>(store_->takes().size())) {
            const Clip& clip = store_->takes()[static_cast<std::size_t>(row.clipIndex)];
            const double from = static_cast<double>(viewStart_);
            const double to = from + waveW * samplesPerPixel_;
            ClipStore::peaksForRange(clip, static_cast<std::uint64_t>(from),
                                     static_cast<std::uint64_t>(to), waveW, mins, maxs);
            const QColor wave((clip.colorRgb >> 16) & 0xFF, (clip.colorRgb >> 8) & 0xFF,
                              clip.colorRgb & 0xFF);
            const QColor waveDim = wave.darker(160);
            // Фон занятой области.
            const int clipW = static_cast<int>(static_cast<double>(clip.samples.size()) /
                                               samplesPerPixel_);
            p.fillRect(kHeaderW, y0 + 2, std::min(clipW, waveW), kRowH - 4,
                       QColor(wave.red(), wave.green(), wave.blue(), 28));
            const int mid = y0 + kRowH / 2;
            const double scaleY = (kRowH / 2 - 6);
            p.setPen(wave);
            for (int x = 0; x < waveW; ++x) {
                const float lo = mins[static_cast<std::size_t>(x)];
                const float hi = maxs[static_cast<std::size_t>(x)];
                const int yLo = mid - static_cast<int>(std::min(1.0f, std::fabs(lo)) * scaleY);
                const int yHi = mid - static_cast<int>(std::min(1.0f, std::fabs(hi)) * scaleY);
                // min/max столбик.
                p.drawLine(kHeaderW + x, yLo, kHeaderW + x, yHi);
                p.drawPoint(kHeaderW + x, mid - static_cast<int>(lo * scaleY));
                p.drawPoint(kHeaderW + x, mid - static_cast<int>(hi * scaleY));
            }
            // RMS-обводка.
            p.setPen(waveDim);
            p.drawText(QRect(kHeaderW + 6, y0 + 4, waveW - 12, 16), Qt::AlignLeft,
                       QStringLiteral("RMS %1 dB  Peak %2 dB")
                           .arg(clip.rmsDb, 0, 'f', 1)
                           .arg(clip.peakDb, 0, 'f', 1));
        } else if (!row.isFixed) {
            p.setPen(QColor(0x55, 0x55, 0x55));
        } else if (row.clipIndex < 0) {
            p.setPen(QColor(0x60, 0x60, 0x60));
            p.drawText(QRect(kHeaderW + 8, y0, waveW - 16, kRowH), Qt::AlignVCenter,
                       row.locked
                           ? QStringLiteral("— decode WEM недоступен до Фазы 3 —")
                           : QStringLiteral("— сюда попадёт финал после обработки —"));
        }

        // Разделитель строк.
        p.setPen(QColor(0x33, 0x33, 0x33));
        p.drawLine(0, y0 + kRowH - 1, width(), y0 + kRowH - 1);
    }
    p.setPen(QColor(0x44, 0x44, 0x44));
    p.drawLine(kHeaderW, 0, kHeaderW, height());

    // --- Playhead ---
    const std::uint64_t ph = playheadSample();
    const double phX = kHeaderW +
                       (static_cast<double>(ph) - static_cast<double>(viewStart_)) / samplesPerPixel_;
    if (phX >= kHeaderW && phX <= width()) {
        p.setPen(QColor(0xff, 0x55, 0x55));
        p.drawLine(static_cast<int>(phX), 0, static_cast<int>(phX), height());
    }
}

} // namespace dubstudio
