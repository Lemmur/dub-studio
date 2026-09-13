// Дымовой тест: окружение и CMake-скелет живы.
// По мере фаз сюда добавляются тесты импортёра, БД, DSP.

#include <catch2/catch_test_macros.hpp>
#include <QString>
#include <QtGlobal>

TEST_CASE("Версия Qt не ниже 6.5", "[env]") {
  REQUIRE(QT_VERSION_MAJOR == 6);
  REQUIRE(QT_VERSION_MINOR >= 5);
}

TEST_CASE("Строка Qt округляет кириллицу без потерь", "[smoke]") {
  const QString s = QStringLiteral("Реплика");
  REQUIRE(s.size() == 7);
  REQUIRE(s.toUtf8().size() == 14);
}
