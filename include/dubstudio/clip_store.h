// ClipStore — тейки/клипы Фазы 1: моно float, блочные min/max пики (блок 256)
// для отрисовки волноформы на любом zoom без прохода по всем сэмплам.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace dubstudio {

struct ClipPeaks {
    std::vector<float> mins;
    std::vector<float> maxs;
    std::uint64_t blockSize = 256;
};

struct Clip {
    std::string id;        // "take_1_<hash8>"
    std::string title;     // "TAKE-01"
    std::string wemHash;   // линия, к которой записан тейк (может быть "")
    std::string filePath;  // WAV на диске ("" если не сохранён)
    std::vector<float> samples; // моно
    std::uint64_t sampleRate = 48000;
    std::uint32_t colorRgb = 0x4f7cb0; // автоцвет тейка
    double rmsDb = -99.0;
    double peakDb = -99.0;
    ClipPeaks peaks;       // кэш пиков
    // Фаза 2 (редактура, PLAN.md 6.4): позиция/гейн/фейды применяются
    // при рендере, сэмплы меняют только команды EditStack.
    std::uint64_t startSample = 0;   // позиция клипа на таймлайне (move/align)
    double gainDb = 0.0;             // clip-gain (не деструктивно)
    std::uint64_t fadeInSamples = 0;    // линейный фейд от начала
    std::uint64_t fadeOutSamples = 0;   // линейный фейд к концу
};

class ClipStore {
public:
    // Добавляет клип и считает пики. Возвращает индекс.
    std::size_t addTake(Clip clip);
    // Пересчитать пики (например, после live-дозаписи сэмплов).
    static void rebuildPeaks(Clip& clip);
    // Пики для отрисовки диапазона [fromSample, toSample) в columns колонок:
    // прямые сэмплы при zoom до сэмплов, иначе блочные пики.
    static void peaksForRange(const Clip& clip, std::uint64_t fromSample,
                              std::uint64_t toSample, int columns,
                              std::vector<float>& mins, std::vector<float>& maxs);

    const std::vector<Clip>& takes() const { return takes_; }
    std::vector<Clip>& takes() { return takes_; }
    std::size_t takeCount() const { return takes_.size(); }
    // Фаза 2: доступ по id тейка (-1 если нет) и удаление (undo split).
    int takeIndexById(const std::string& id) const;
    bool removeTakeById(const std::string& id);

    // Автоцвет по номеру тейка (различимые оттенки, тёмная тема).
    static std::uint32_t autoColor(int takeIndex);

private:
    std::vector<Clip> takes_;
};

} // namespace dubstudio
