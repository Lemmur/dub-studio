// TimelineWidget: отрисовка треков/волноформ/линейки/playhead (Фаза 1)
// + редактура Фазы 2: выбор клипа, drag-move, курсор/диапазон, фейды.
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
    setMinimumHeight(kRulerH + (kRowH + kMiniRulerH) * (2 + kMinRowH));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

QSize TimelineWidget::minimumSizeHint() const {
    return QSize(600, kRulerH + (kRowH + kMiniRulerH) * 4);
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
            // Фильтр по реплике: на таймлайне только тейки выбранной реплики;
            // без выбранной реплики дорожки тейков пусты.
            if (!lineFilterActive_ || takes[i].wemHash != lineFilter_) continue;
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

void TimelineWidget::setLineFilter(const std::string& wemHash) {
    if (lineFilterActive_ && lineFilter_ == wemHash) return;
    lineFilter_ = wemHash;
    lineFilterActive_ = true;
    selected_ = -1; // выбранный клип мог уйти из вида
    syncTracks();
    emit selectionChanged();
}

void TimelineWidget::clearLineFilter() {
    if (!lineFilterActive_) return;
    lineFilterActive_ = false;
    selected_ = -1;
    syncTracks();
    emit selectionChanged();
}

void TimelineWidget::syncTracks() {
    buildRows();
    // Выделение могло выйти за пределы (undo split/загрузка сессии).
    if (store_ && selected_ >= static_cast<int>(store_->takes().size())) selected_ = -1;
    if (!store_) selected_ = -1;
    // Фиксированная высота = все дорожки (с мини-линейками): вертикальный
    // скролл даёт обёртка QScrollArea (MainWindow::buildUi).
    setMinimumHeight(kRulerH + static_cast<int>(rows_.size()) * rowStride());
    clampView();
    update();
    updateGeometry();
}

double TimelineWidget::contentSamples() const {
    double len = 30.0 * 48000; // минимум 30 сек канвы
    if (store_) {
        for (const auto& c : store_->takes()) {
            len = std::max<double>(len,
                                   static_cast<double>(c.startSample + c.samples.size()));
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

std::uint64_t TimelineWidget::sampleAtX(double x) const {
    const double s = static_cast<double>(viewStart_) +
                     std::max(0.0, x - kHeaderW) * samplesPerPixel_;
    return static_cast<std::uint64_t>(std::max(0.0, s));
}

int TimelineWidget::clipHit(double x, double y) const {
    const int r = rowAt(static_cast<int>(y));
    if (r < 0) return -1;
    const Row& row = rows_[static_cast<std::size_t>(r)];
    if (!store_ || row.locked || row.clipIndex < 0 ||
        row.clipIndex >= static_cast<int>(store_->takes().size()))
        return -1;
    if (x <= kHeaderW) return -1;
    const Clip& c = store_->takes()[static_cast<std::size_t>(row.clipIndex)];
    const double x0 = kHeaderW +
                      (static_cast<double>(c.startSample) - static_cast<double>(viewStart_)) /
                          samplesPerPixel_;
    const double x1 = x0 + static_cast<double>(c.samples.size()) / samplesPerPixel_;
    if (x >= x0 && x <= x1) return row.clipIndex;
    return -1;
}

void TimelineWidget::selectClip(int clipIndex) {
    selected_ = clipIndex;
    update();
    emit selectionChanged();
}

bool TimelineWidget::hasRange(std::uint64_t& from, std::uint64_t& to) const {
    if (anchorSample_ == cursorSample_) return false;
    from = std::min(anchorSample_, cursorSample_);
    to = std::max(anchorSample_, cursorSample_);
    return true;
}

void TimelineWidget::setCursorAt(double x) {
    cursorSample_ = anchorSample_ = sampleAtX(x);
    update();
    emit infoChanged(QStringLiteral("Курсор: сэмпл %1").arg(cursorSample_));
}

void TimelineWidget::mousePressEvent(QMouseEvent* event) {
    const double x = static_cast<double>(event->position().x());
    const double y = static_cast<double>(event->position().y());

    // Панорама: средняя кнопка или Alt+ЛКМ в любом месте полотна.
    if (event->button() == Qt::MiddleButton ||
        (event->button() == Qt::LeftButton && (event->modifiers() & Qt::AltModifier))) {
        dragging_ = true;
        dragLastX_ = static_cast<int>(x);
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    if (event->button() != Qt::LeftButton) return;

    // Shift+клик ГДЕ УГОДНО: конец диапазона (курсор двигается, анкер стоит).
    if (event->modifiers() & Qt::ShiftModifier) {
        cursorSample_ = sampleAtX(x);
        update();
        std::uint64_t f, t;
        if (hasRange(f, t)) {
            emit infoChanged(QStringLiteral("Диапазон: %1..%2 (%3 сэмпл)")
                                 .arg(f)
                                 .arg(t)
                                 .arg(t - f));
        }
        return;
    }

    // Ctrl+ЛКМ по клипу: выбрать и ПЕРЕМЕЩАТЬ (drag-move).
    const int hit = clipHit(x, y);
    if (hit >= 0 && (event->modifiers() & Qt::ControlModifier)) {
        selected_ = hit;
        movingClip_ = true;
        movePressX_ = x;
        moveStartBegin_ = store_->takes()[static_cast<std::size_t>(hit)].startSample;
        setCursor(Qt::SizeAllCursor);
        update();
        emit selectionChanged();
        return;
    }

    // Верхняя линейка ИЛИ мини-линейка над дорожкой: установка курсора.
    if (y < kRulerH || inMiniRuler(static_cast<int>(y))) {
        setCursorAt(x);
        cursorPress_ = true;  // зажатой кнопкой можно довести точно
        selecting_ = false;
        rangeRow_ = -1;       // с линейки выделение «на все дорожки»
        pressX_ = x;
        return;
    }

    // ЛКМ по клипу (без Ctrl): просто выбор.
    if (hit >= 0) {
        selected_ = hit;
        update();
        emit selectionChanged();
        return;
    }

    // Пустое место дорожки: press = курсор; drag дальше 2px = ВЫДЕЛЕНИЕ
    // диапазона по этой дорожке (анкер в точке нажатия).
    if (x > kHeaderW) {
        setCursorAt(x);
        cursorPress_ = true;
        selecting_ = false;
        rangeRow_ = rowAt(static_cast<int>(y));
        pressX_ = x;
    }
}

void TimelineWidget::mouseMoveEvent(QMouseEvent* event) {
    const double x = static_cast<double>(event->position().x());
    // ЛКМ на дорожке: до 2px — точная доводка курсора (анкер идёт за ним),
    // дальше — выделение диапазона: курсор тянется, анкер стоит в точке press.
    if (cursorPress_ && !movingClip_) {
        cursorSample_ = sampleAtX(x);
        if (!selecting_ && std::fabs(x - pressX_) > 2.0) {
            selecting_ = true;
        }
        if (!selecting_) {
            anchorSample_ = cursorSample_;
        }
        update();
        return;
    }
    if (movingClip_ && selected_ >= 0 && store_ &&
        selected_ < static_cast<int>(store_->takes().size())) {
        // Живой preview: сэмплы не трогаем, двигаем только позицию клипа.
        // Финальное состояние фиксирует команда Move на mouseRelease.
        const double dxSamples = (x - movePressX_) * samplesPerPixel_;
        const double newStart =
            std::max(0.0, static_cast<double>(moveStartBegin_) + dxSamples);
        store_->takes()[static_cast<std::size_t>(selected_)].startSample =
            static_cast<std::uint64_t>(newStart);
        update();
        return;
    }
    if (!dragging_) return;
    const int dx = static_cast<int>(x) - dragLastX_;
    dragLastX_ = static_cast<int>(x);
    const double shiftSamples = -dx * samplesPerPixel_;
    const double maxStart = std::max(0.0, contentSamples() - (width() - kHeaderW) * samplesPerPixel_);
    viewStart_ = static_cast<std::uint64_t>(
        std::clamp(static_cast<double>(viewStart_) + shiftSamples, 0.0, maxStart));
    update();
}

void TimelineWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton && event->button() != Qt::MiddleButton) return;
    if (movingClip_) {
        movingClip_ = false;
        setCursor(Qt::ArrowCursor);
        if (selected_ >= 0 && store_ && selected_ < static_cast<int>(store_->takes().size())) {
            emit clipMoved(selected_,
                           store_->takes()[static_cast<std::size_t>(selected_)].startSample);
        }
        return;
    }
    cursorPress_ = false;
    selecting_ = false;
    dragging_ = false;
    setCursor(Qt::ArrowCursor);
}

void TimelineWidget::mouseDoubleClickEvent(QMouseEvent* event) {
    // «Вписать всё» — ТОЛЬКО по верхней линейке. Двойной клик по дорожке
    // нельзя: серия быстрых установок курсора распознаётся как dblclick,
    // и сброс вида (viewStart=0 + зум) «уносил» только что поставленный
    // курсор — выглядело как «курсор улетает в нулевое положение».
    if (event->button() == Qt::LeftButton &&
        static_cast<double>(event->position().y()) < kRulerH) {
        // Вписать в окно КОНТЕНТ (край самого позднего клипа), а не канву-минимум
        // 30 с: короткий тейк не должен занимать десятую часть окна.
        double content = 0.0;
        if (store_) {
            for (const auto& c : store_->takes()) {
                content = std::max<double>(
                    content, static_cast<double>(c.startSample + c.samples.size()));
            }
        }
        if (content <= 0.0) content = contentSamples(); // нет клипов — канва 30 с
        const double w = std::max(1.0, static_cast<double>(width() - kHeaderW));
        samplesPerPixel_ = std::clamp(content / w, 1.0 / 64.0, 4096.0);
        viewStart_ = 0;
        update();
        emit infoChanged(QStringLiteral("Вписать: %1 сэмпл/пикс").arg(samplesPerPixel_, 0, 'f', 2));
    }
}

int TimelineWidget::rowAt(int y) const {
    const int idx = (y - kRulerH) / rowStride();
    if (idx < 0 || idx >= static_cast<int>(rows_.size())) return -1;
    return idx;
}

std::uint64_t TimelineWidget::playheadSample() const {
    if (!engine_) return 0;
    if (engine_->isRecording()) return engine_->recordedFrames();
    return engine_->playheadFrames();
}

void TimelineWidget::tick() {
    // Автоскролл за playhead ТОЛЬКО при активной записи/плейбеке.
    // В покое вид должен стоять на месте: после стопа playhead=0, и автоскролл
    // каждые 33 мс возвращал viewStart в 0 — вид «прыгал», а курсор/диапазон,
    // выставленные по старым координатам, улетали за экран.
    if (engine_ && (engine_->isRecording() || engine_->isPlaying())) {
        const std::uint64_t ph = playheadSample();
        const double x =
            (static_cast<double>(ph) - static_cast<double>(viewStart_)) / samplesPerPixel_;
        const double w = static_cast<double>(width() - kHeaderW);
        if (x > w - 40.0 || x < 0.0) {
            viewStart_ = static_cast<std::uint64_t>(
                std::max(0.0, static_cast<double>(ph) - w * 0.25));
            clampView();
        }
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

    // --- Строки треков (над каждой — мини-линейка с маркером курсора) ---
    const int nRows = static_cast<int>(rows_.size());
    std::vector<float> mins, maxs;
    const double cursorX =
        kHeaderW + (static_cast<double>(cursorSample_) - static_cast<double>(viewStart_)) /
                       samplesPerPixel_;
    for (int r = 0; r < nRows; ++r) {
        const Row& row = rows_[static_cast<std::size_t>(r)];
        const int mtop = miniTop(r);
        const int y0 = rowTop(r);
        const bool isSelected = selected_ == row.clipIndex && row.clipIndex >= 0;

        // Хедер трека.
        p.fillRect(0, y0, kHeaderW, kRowH,
                   isSelected ? QColor(0x33, 0x3d, 0x52) : QColor(0x26, 0x26, 0x26));
        p.setPen(Qt::NoPen);
        p.setBrush(QColor((row.color >> 16) & 0xFF, (row.color >> 8) & 0xFF, row.color & 0xFF));
        p.drawRect(6, y0 + 6, 8, kRowH - 12);
        p.setBrush(Qt::NoBrush); // дальше только контуры, иначе рамка клипа зальёт волну
        p.setPen(QColor(0xe0, 0xe0, 0xe0));
        QFont f = font();
        f.setPointSize(9);
        f.setBold(isSelected);
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
            const QColor wave((clip.colorRgb >> 16) & 0xFF, (clip.colorRgb >> 8) & 0xFF,
                              clip.colorRgb & 0xFF);

            // Прямоугольник клипа по startSample (Фаза 2: клип можно двигать).
            const double clipX0d = kHeaderW +
                (static_cast<double>(clip.startSample) - static_cast<double>(viewStart_)) /
                    samplesPerPixel_;
            const double clipWd = static_cast<double>(clip.samples.size()) / samplesPerPixel_;
            const int cx0 = std::max(kHeaderW, static_cast<int>(clipX0d));
            const int cx1 = std::min(width(), static_cast<int>(clipX0d + clipWd) + 1);

            if (cx1 > cx0) {
                const int cw = cx1 - cx0;
                // Фон занятой области.
                p.fillRect(cx0, y0 + 2, cw, kRowH - 4,
                           QColor(wave.red(), wave.green(), wave.blue(), 28));
                // Рамка (выделенный — ярче).
                p.setPen(isSelected ? QColor(0xff, 0xd7, 0x60) : wave.darker(140));
                p.drawRect(cx0, y0 + 2, cw - 1, kRowH - 4);

                // Волна внутри прямоугольника клипа.
                const std::uint64_t fromLocal =
                    viewStart_ > clip.startSample ? viewStart_ - clip.startSample : 0;
                const std::uint64_t toLocal =
                    fromLocal + static_cast<std::uint64_t>(cw * samplesPerPixel_);
                ClipStore::peaksForRange(clip, fromLocal, toLocal, cw, mins, maxs);
                const int mid = y0 + kRowH / 2;
                const double scaleY = (kRowH / 2 - 6);
                p.setPen(wave);
                for (int x = 0; x < cw; ++x) {
                    const float lo = mins[static_cast<std::size_t>(x)];
                    const float hi = maxs[static_cast<std::size_t>(x)];
                    const int yLo = mid - static_cast<int>(std::min(1.0f, std::fabs(lo)) * scaleY);
                    const int yHi = mid - static_cast<int>(std::min(1.0f, std::fabs(hi)) * scaleY);
                    p.drawLine(cx0 + x, yLo, cx0 + x, yHi);
                }

                // Фейды: наклонные линии от углов (Фаза 2).
                p.setPen(QColor(0xff, 0xd7, 0x60));
                if (clip.fadeInSamples > 0) {
                    const int fx = static_cast<int>(
                        static_cast<double>(clip.fadeInSamples) / samplesPerPixel_);
                    p.drawLine(cx0, mid, cx0 + std::min(fx, cw), y0 + 3);
                    p.drawLine(cx0, mid, cx0 + std::min(fx, cw), y0 + kRowH - 3);
                }
                if (clip.fadeOutSamples > 0) {
                    const int fx = static_cast<int>(
                        static_cast<double>(clip.fadeOutSamples) / samplesPerPixel_);
                    p.drawLine(cx1 - 1, mid, cx1 - 1 - std::min(fx, cw), y0 + 3);
                    p.drawLine(cx1 - 1, mid, cx1 - 1 - std::min(fx, cw), y0 + kRowH - 3);
                }

                // Подпись.
                p.setPen(wave.darker(160));
                QString label = QStringLiteral("RMS %1 dB  Peak %2 dB")
                                    .arg(clip.rmsDb, 0, 'f', 1)
                                    .arg(clip.peakDb, 0, 'f', 1);
                if (std::fabs(clip.gainDb) > 0.01)
                    label += QStringLiteral("  Gain %1 dB").arg(clip.gainDb, 0, 'f', 1);
                p.drawText(QRect(cx0 + 6, y0 + 4, cw - 12, 16), Qt::AlignLeft, label);
            }
        } else if (!row.isFixed) {
            p.setPen(QColor(0x55, 0x55, 0x55));
        } else if (row.clipIndex < 0) {
            p.setPen(QColor(0x60, 0x60, 0x60));
            p.drawText(QRect(kHeaderW + 8, y0, waveW - 16, kRowH), Qt::AlignVCenter,
                       row.locked
                           ? QStringLiteral("— decode WEM недоступен до Фазы 3 —")
                           : QStringLiteral("— сюда попадёт финал после обработки —"));
        }

        // Мини-линейка над дорожкой: близко к волне, удобно точно ставить курсор.
        p.fillRect(kHeaderW, mtop, waveW, kMiniRulerH, QColor(0x23, 0x23, 0x23));
        p.fillRect(0, mtop, kHeaderW, kMiniRulerH, QColor(0x2b, 0x2b, 0x2b));
        p.setPen(QColor(0x55, 0x55, 0x55));
        for (double t = std::floor(startSec / step) * step; t <= endSec; t += step) {
            const int mx = kHeaderW + static_cast<int>((t - startSec) / secondsVisible * waveW);
            p.drawLine(mx, mtop + kMiniRulerH - 3, mx, mtop + kMiniRulerH - 1);
        }
        // Маркер курсора — жёлтый треугольник вниз.
        if (cursorX >= kHeaderW && cursorX <= width()) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0xff, 0xd7, 0x60));
            const int cx = static_cast<int>(cursorX);
            QPolygon tri;
            tri << QPoint(cx - 4, mtop) << QPoint(cx + 4, mtop) << QPoint(cx, mtop + 5);
            p.drawPolygon(tri);
            p.setBrush(Qt::NoBrush);
        }

        // Разделитель строк.
        p.setPen(QColor(0x33, 0x33, 0x33));
        p.drawLine(0, y0 + kRowH - 1, width(), y0 + kRowH - 1);
    }
    p.setPen(QColor(0x44, 0x44, 0x44));
    p.drawLine(kHeaderW, 0, kHeaderW, height());

    // --- Диапазон: заливка ТОЛЬКО на дорожке, где начали выделение;
    // --- границы — сквозные через все дорожки (видно контекст). ---
    std::uint64_t rf, rt;
    if (hasRange(rf, rt)) {
        const double x0d = kHeaderW +
            (static_cast<double>(rf) - static_cast<double>(viewStart_)) / samplesPerPixel_;
        const double x1d = kHeaderW +
            (static_cast<double>(rt) - static_cast<double>(viewStart_)) / samplesPerPixel_;
        const int rx0 = std::max(kHeaderW, static_cast<int>(x0d));
        const int rx1 = std::min(width(), static_cast<int>(x1d) + 1);
        if (rx1 > rx0) {
            if (rangeRow_ >= 0 && rangeRow_ < static_cast<int>(rows_.size())) {
                const int ry0 = rowTop(rangeRow_);
                p.fillRect(rx0, ry0, rx1 - rx0, kRowH, QColor(0xff, 0xd7, 0x60, 26));
            } else {
                p.fillRect(rx0, kRulerH, rx1 - rx0, height() - kRulerH,
                           QColor(0xff, 0xd7, 0x60, 26));
            }
            p.setPen(QColor(0xff, 0xd7, 0x60, 120));
            p.drawLine(rx0, kRulerH, rx0, height());
            p.drawLine(rx1 - 1, kRulerH, rx1 - 1, height());
        }
    }

    // --- Курсор редактирования (жёлтый пунктир через все дорожки) ---
    if (cursorX >= kHeaderW && cursorX <= width()) {
        QPen dash(QColor(0xff, 0xd7, 0x60), 1, Qt::DashLine);
        p.setPen(dash);
        p.drawLine(static_cast<int>(cursorX), 0, static_cast<int>(cursorX), height());
        // Маркер и на верхней линейке.
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0xff, 0xd7, 0x60));
        QPolygon tri;
        tri << QPoint(static_cast<int>(cursorX) - 4, 0)
            << QPoint(static_cast<int>(cursorX) + 4, 0)
            << QPoint(static_cast<int>(cursorX), 5);
        p.drawPolygon(tri);
        p.setBrush(Qt::NoBrush);
    }

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
