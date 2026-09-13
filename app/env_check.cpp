// Консольная проверка среды разработки RuDub Studio.
// Запуск: build\<config>\app\rudub_env_check.exe (после scripts\env.cmd).
// Коды возврата: 0 — среда готова, 1 — есть критические проблемы.

#include <QCoreApplication>
#include <QSqlDatabase>
#include <QStringList>
#include <QTextStream>
#include <cstdio>

int main(int argc, char* argv[]) {
  QCoreApplication app(argc, argv);
  QTextStream out(stdout);

  const QString qtVer = QStringLiteral(QT_VERSION_STR);
  const QString buildQt = QString::fromUtf8(qVersion());

  out << "=== RuDub Studio: проверка среды ===\n";
  out << "Qt (сборка):    " << qtVer << "\n";
  out << "Qt (runtime):   " << buildQt << "\n";

  const int ok = qtVer == buildQt;

  const QStringList drivers = QSqlDatabase::drivers();
  out << "SQL-драйверы:   " << drivers.join(QStringLiteral(", ")) << "\n";
  const bool hasSqlite = drivers.contains(QStringLiteral("QSQLITE"),
                                          Qt::CaseInsensitive);
  if (!hasSqlite) {
    out << "ОШИБКА: драйвер QSQLITE не найден — lines.db не будет работать.\n";
  }

  out << "Плагины:        " << QCoreApplication::libraryPaths().join(QStringLiteral("; "))
      << "\n";

  if (!ok) {
    out << "ОШИБКА: версия runtime Qt отличается от версии сборки "
           "(проверить PATH к D:\\Qt\\6.5.3\\msvc2019_64\\bin).\n";
  }
  out << (ok && hasSqlite ? "СРЕДА ГОТОВА\n" : "СРЕДА НЕ ГОТОВА\n");
  return (ok && hasSqlite) ? 0 : 1;
}
