// Компактный метр входного сигнала для статус-бара (Фаза 1).
#pragma once

#include <QWidget>

namespace dubstudio {

class LevelMeter : public QWidget {
    Q_OBJECT
public:
    explicit LevelMeter(QWidget* parent = nullptr);

    void setLevel(float peakLinear); // 0..1

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    float level_ = 0.0f;
};

} // namespace dubstudio
