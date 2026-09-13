// Фаза 2 (редактура, PLAN.md 6.4): чистые DSP-операции над сэмплами клипа.
// Без Qt/БД — только векторы float, всё тестируется юнит-тестами.
// Мутации сэмплов в приложении идут ТОЛЬКО через EditStack (Command + undo_log).
#pragma once

#include "clip_store.h"

#include <cstdint>
#include <vector>

namespace dubstudio {

// Нормализация по пику: масштабирует сэмплы так, чтобы |peak| = targetDbFs.
// Возвращает применённый гейн в dB (0 при тишине). Значения клампятся в [-1,1].
double normalizePeakToDb(std::vector<float>& samples, double targetDbFs);

// Заменяет диапазон [from, to) тишиной (кламп к размеру).
void silenceRange(std::vector<float>& samples, std::size_t from, std::size_t to);

// Реверс сэмплов (всего вектора или диапазона).
void reverseSamples(std::vector<float>& samples, std::size_t from, std::size_t to);

// Ближайшее к pos пересечение нуля (для split без щелчков, PLAN.md 6.4).
std::uint64_t snapToZeroCrossing(const std::vector<float>& samples, std::uint64_t pos);

// Диапазон звучащего [first, last): окнами RMS, порог -50 dBFS
// (согласован с PLAN.md 6.3 «RMS < -50 dBFS — тишина»).
struct SoundRange {
    std::uint64_t first = 0; // первый сэмпл звука
    std::uint64_t last = 0;  // за последним сэмплом звука
    bool empty = true;
};
SoundRange findSoundRange(const std::vector<float>& samples,
                          double thresholdDbFs = -50.0,
                          std::uint64_t windowSamples = 480);

// Прибавляет клип (startSample, gainDb, fadeIn/out) в микс out (out растёт до нужной длины).
void renderClipInto(const Clip& clip, std::vector<float>& out);

// Микс всех клипов: сумма дорожек с учётом позиций/гейнов/фейдов (плейбек).
std::vector<float> renderMix(const std::vector<Clip>& clips);

// Слияние двух клипов с equal-power кроссфейдом на перекрытии (cos/sin).
// Перекрытия нет — простое наложение. Гейны клипов учитываются, их фейды — нет
// (кроссфейд сам управляет амплитудой в зоне перекрытия).
struct MergedClip {
    std::vector<float> samples;
    std::uint64_t startSample = 0;
    std::uint64_t overlapSamples = 0; // длина зоны кроссфейда
};
MergedClip crossfadeMerge(const Clip& a, const Clip& b);

// Time-stretch с сохранением питча (WSOLA: окно Ханна, overlap 50%,
// поиск best-offset по нормированной кросс-корреляции).
// rubberband-заменa: GPL-библиотеку в ядро линковать нельзя (AGENTS.md),
// для моно-речи 0.25x..4.0x качества достаточно (docs/plans/phase-2-editing.md).
// Возвращает вектор длиной round(src.size() * ratio).
std::vector<float> timeStretchWsola(const std::vector<float>& src, double ratio,
                                    std::uint32_t sampleRate);

} // namespace dubstudio
