// Общие утилиты тестов: временные каталоги, фикстуры combined.json.
#pragma once

#include <catch_amalgamated.hpp>

#include <filesystem>
#include <fstream>
#include <random>
#include <string>

namespace fs = std::filesystem;

// Временный каталог с авто-удалением.
struct TempDir {
    fs::path p;

    TempDir() {
        static std::mt19937 rng{std::random_device{}()};
        auto base = fs::temp_directory_path();
        for (int attempt = 0; attempt < 16; ++attempt) {
            const auto candidate = base / ("dubstudio_test_" + std::to_string(rng()));
            std::error_code ec;
            if (fs::create_directory(candidate, ec)) {
                p = candidate;
                return;
            }
        }
        p = base / "dubstudio_test_fallback";
        fs::create_directories(p);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(p, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    fs::path path() const { return p; }
};

// Небольшой combined.json на 2 файла / 3 сцены / 6 реплик с особыми кейсами:
// пустой speaker_name -> UNKNOWN, суффикс "(whisper)", dur с дробью.
inline std::string smallCombinedJson() {
    // Внутри есть "(whisper)" — последовательность )" порвала бы raw-string
    // с пустым делимитером, поэтому используем делимитер json.
    return R"json({
  "q000_intro": {
    "cs_q000_1_opening": {
      "6046256F4DF7E505F0906FBE58C09951": {
        "en": "Oh! Coen, look!",
        "ru": "О! Коэн, смотри!",
        "speaker_name": "Lunka",
        "speaker_internal": "Character.Secondary.Lunka",
        "dur": 1.861
      },
      "A0000000000000000000000000000001": {
        "en": "Quiet now.",
        "ru": "Тихо сейчас.",
        "speaker_name": "",
        "speaker_internal": "",
        "dur": 0.5
      },
      "A0000000000000000000000000000002": {
        "en": "Follow me.",
        "ru": "Следуй за мной.",
        "speaker_name": "Ambrus (whisper)",
        "speaker_internal": "Character.Main.Ambrus",
        "dur": 1.0
      }
    },
    "cs_q000_2_gate": {
      "A0000000000000000000000000000003": {
        "en": "The gate is closed.",
        "ru": "Ворота закрыты.",
        "speaker_name": "Coen",
        "speaker_internal": "Character.Main.Coen",
        "dur": 2.349
      }
    }
  },
  "sq708_ambrus": {
    "ambrus_whispers": {
      "A0000000000000000000000000000004": {
        "en": "Psst... over here.",
        "ru": "Псс... сюда.",
        "speaker_name": "Ambrus (whisper)",
        "speaker_internal": "Character.Main.Ambrus",
        "dur": 0.999
      },
      "A0000000000000000000000000000005": {
        "en": "I must go.",
        "ru": "Мне пора.",
        "speaker_name": "Tharael",
        "speaker_internal": "Character.Main.Tharael",
        "dur": 3.14159
      }
    }
  }
})json";
}

// Фейковый combined.json игрового масштаба (PLAN.md 1.2):
// 42 файла, 2181 сцена, 39481 реплика. Детерминированный (seed фиксирован).
struct BigJson {
    fs::path path;
    int files = 0;
    int quests = 0;
    int lines = 0;
    std::string firstGuid;
    std::string lastGuid;
};

inline BigJson generateBigCombinedJson(const fs::path& out, int filesCount = 42,
                                       int questsCount = 2181, int linesCount = 39481) {
    BigJson result;
    result.files = filesCount;
    result.quests = questsCount;
    result.lines = linesCount;

    // Раскладка сцен по файлам: 51 на файл, первые (questsCount % filesCount) на одну больше.
    const int baseQuests = questsCount / filesCount;
    const int extraQuests = questsCount % filesCount;

    // Раскладка реплик по сценам: по 18, первые (linesCount % questsCount) на одну больше.
    const int baseLines = linesCount / questsCount;
    const int extraLines = linesCount % questsCount;

    static const char* kSpeakers[] = {"Lunka", "Coen", "Jespar", "Ambrus (whisper)",
                                      "Tharael", "", "Qalian (whisper)", "Tealor",
                                      "Nemesis", "Yuslan"};
    static const char* kEn[] = {"Watch out!", "The path is long.", "I do not understand.",
                                "Can you hear it?", "Over here!", "Stay behind me.",
                                "It is done.", "Not yet..."};
    static const char* kRu[] = {"Осторожно!", "Путь долог.", "Я не понимаю.",
                                "Слышишь?", "Сюда!", "Держись за мной.",
                                "Готово.", "Ещё нет..."};

    std::mt19937 rng(20260913);
    std::uniform_real_distribution<double> durDist(0.25, 8.75);
    std::uniform_int_distribution<int> pick(0, 7);

    std::ofstream f(out, std::ios::binary);
    REQUIRE(f);

    unsigned guidCounter = 1;
    auto nextGuid = [&guidCounter] {
        char buf[33];
        std::snprintf(buf, sizeof(buf), "%032X", guidCounter++);
        return std::string(buf);
    };

    f << "{";
    int questIndex = 0;
    for (int fi = 0; fi < filesCount; ++fi) {
        if (fi > 0) f << ",";
        f << "\n  \"q" << (fi < 10 ? "00" : (fi < 100 ? "0" : "")) << fi << "_fake\": {";
        const int questsHere = baseQuests + (fi < extraQuests ? 1 : 0);
        for (int qi = 0; qi < questsHere; ++qi, ++questIndex) {
            if (qi > 0) f << ",";
            f << "\n    \"cs_q" << questIndex << "_scene\": {";
            const int linesHere = baseLines + (questIndex < extraLines ? 1 : 0);
            for (int li = 0; li < linesHere; ++li) {
                const std::string guid = nextGuid();
                if (questIndex == 0 && li == 0) result.firstGuid = guid;
                result.lastGuid = guid;
                const int speakerIdx = (guidCounter + questIndex) % 10;
                const int textIdx = pick(rng);
                if (li > 0) f << ",";
                f << "\n      \"" << guid << "\": {"
                  << "\"en\": \"" << kEn[textIdx] << "\", "
                  << "\"ru\": \"" << kRu[textIdx] << "\", "
                  << "\"speaker_name\": \"" << kSpeakers[speakerIdx] << "\", "
                  << "\"speaker_internal\": \"Character.Fake.S" << speakerIdx << "\", "
                  << "\"dur\": " << durDist(rng) << "}";
            }
            f << "\n    }";
        }
        f << "\n  }";
    }
    f << "\n}\n";
    f.close();
    REQUIRE(f.good());
    return result;
}
