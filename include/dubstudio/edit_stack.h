// Фаза 2 (редактура): Command-стек правок клипа. Все мутации сэмплов/позиций
// идут через EditStack: транзакция undo_log + takes + WAV-снапшот в MyDub/undo.
// Undo-история живёт в БД -> откат до старта переживает рестарт (PLAN.md 6.5).
#pragma once

#include "clip_store.h"
#include "database.h"

#include <cstdint>
#include <string>

namespace dubstudio {

enum class EditType {
    TrimSilence, // трим тишины по краям (порог -50 dBFS, PLAN.md 6.3)
    TrimRange,   // обрезать вне диапазона [from, to) в локальных сэмплах клипа
    Split,       // разделить по from (snap к zero-crossing) -> новый тейк
    Move,        // переместить: startSample = uValue
    Align,       // Align to Ref: startSample = 0 (начало референса)
    Gain,        // clip-gain dValue dB (применяется при рендере)
    Normalize,   // нормализация по пику до dValue dBFS
    Silence,     // заглушить [from, to) (to=0 -> весь клип)
    Reverse,     // реверс [from, to) (to=0 -> весь клип)
    FadeIn,      // фейд-ин от начала до from
    FadeOut,     // фейд-аут от from до конца
    Crossfade,   // слить takeId + takeId2 с equal-power кроссфейдом
    FitToRef,    // WSOLA-растяжение до lines.ref_duration_ms реплики тейка
};

struct EditCommand {
    EditType type = EditType::Move;
    std::string takeId;   // основной клип
    std::string takeId2;  // второй клип (Crossfade)
    std::uint64_t from = 0; // локальная позиция курсора / начало диапазона
    std::uint64_t to = 0;   // конец диапазона (0 = до конца)
    double dValue = 0.0;    // gain dB / target dBFS
    std::uint64_t uValue = 0; // новый startSample (Move)
};

class EditStack {
public:
    EditStack(Database& db, ClipStore& store, std::string myDubDir);

    // Применить команду: мутация ClipStore + транзакция (undo_log + takes)
    // + WAV-снапшот. Возвращает seq из undo_log. Ошибки — std::runtime_error
    // (при ошибке store не меняется).
    std::int64_t apply(const EditCommand& cmd);

    bool undo();
    bool redo();
    bool canUndo() const;
    bool canRedo() const;

    // Имена для меню Отменить/Повторить.
    std::string nextUndoAction() const;
    std::string nextRedoAction() const;

    // Восстановить тейки из БД (запуск приложения после рестарта):
    // строки takes + последний неотменённый снапшот каждого тейка.
    struct SessionInfo {
        int takes = 0;      // загружено клипов
        int maxTakeNum = 0; // max N из id "take_N_..." (нумерация новых тейков)
    };
    SessionInfo loadSession();

    // Быстрый автосейв (PLAN.md 6.5): PRAGMA wal_checkpoint(TRUNCATE) +
    // атомарная запись manifest.json. Возвращает unix-время снапшота.
    std::int64_t autosaveSnapshot();

    const std::string& myDubDir() const { return myDubDir_; }

    static const char* typeName(EditType t); // "Разделить" и т.п. (RU для UI)

private:
    void dropRedoBranch(); // DELETE undone=1 (wav-файлы собирает apply)

    Database& db_;
    ClipStore& store_;
    std::string myDubDir_;
};

} // namespace dubstudio
