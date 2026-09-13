#include "levelmeter.h"

#include <QPainter>

#include <algorithm>
#include <cmath>

namespace dubstudio {

LevelMeter::LevelMeter(QWidget* parent) : QWidget(parent) {
    setFixedSize(140, 16);
}

void LevelMeter::setLevel(float peak) {
    if (peak != level_) {
        level_ = peak;
        update();
    }
}

void LevelMeter::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), QColor(0x22, 0x22, 0x22));
    const double db = level_ > 1e-9f ? 20.0 * std::log10(level_) : -99.0;
    const double norm = std::clamp((db + 60.0) / 60.0, 0.0, 1.0); // -60..0 dBFS
    const int w = static_cast<int>(norm * (width() - 4));
    if (w > 0) {
        const int green = static_cast<int>((width() - 4) * 0.7);
        p.fillRect(2, 3, std::min(w, green), height() - 6, QColor(0x3f, 0xa8, 0x4f));
        if (w > green) {
            p.fillRect(2 + green, 3, w - green, height() - 6, QColor(0xd0, 0x9a, 0x2f));
        }
    }
    p.setPen(QColor(0x99, 0x99, 0x99));
    p.drawRect(1, 2, width() - 2, height() - 4);
    p.drawText(rect().adjusted(4, 0, -4, 0), Qt::AlignVCenter | Qt::AlignRight,
               QStringLiteral("%1 dB").arg(db, 0, 'f', 1));
}

} // namespace dubstudio
