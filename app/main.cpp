// RuDub Studio — скелет GUI (Фаза 0).
// Тёмная тема по умолчанию, интерфейс только на русском (AGENTS.md).
// Дальнейшее развитие: дерево файл->сцена, QTableView реплик, FTS-поиск
// (PLAN.md, разделы 13, 15.4).

#include <QApplication>
#include <QMainWindow>
#include <QMenuBar>
#include <QLabel>
#include <QStyleFactory>
#include <QPalette>
#include <QColor>
#include <QString>

int main(int argc, char* argv[]) {
  QApplication app(argc, argv);
  QApplication::setApplicationName(QStringLiteral("RuDub Studio"));
  QApplication::setOrganizationName(QStringLiteral("RuDub"));
  QApplication::setApplicationVersion(QStringLiteral("0.1.0"));

  // Тёмная тема: Fusion + явная палитра (PLAN.md 1.1)
  QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
  QPalette dark;
  dark.setColor(QPalette::Window, QColor(37, 37, 38));
  dark.setColor(QPalette::WindowText, QColor(208, 208, 208));
  dark.setColor(QPalette::Base, QColor(30, 30, 30));
  dark.setColor(QPalette::AlternateBase, QColor(45, 45, 45));
  dark.setColor(QPalette::ToolTipBase, QColor(60, 60, 61));
  dark.setColor(QPalette::ToolTipText, QColor(208, 208, 208));
  dark.setColor(QPalette::Text, QColor(208, 208, 208));
  dark.setColor(QPalette::Button, QColor(45, 45, 48));
  dark.setColor(QPalette::ButtonText, QColor(208, 208, 208));
  dark.setColor(QPalette::Highlight, QColor(0, 120, 215));
  dark.setColor(QPalette::HighlightedText, QColor(240, 240, 240));
  dark.setColor(QPalette::Disabled, QPalette::Text, QColor(120, 120, 120));
  dark.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(120, 120, 120));
  QApplication::setPalette(dark);

  QMainWindow window;
  window.setWindowTitle(QApplication::translate("MainWindow",
      "RuDub Studio — скелет (Фаза 0)"));
  window.resize(1280, 800);

  auto* hello = new QLabel(QApplication::translate("MainWindow",
      "Фаза 0: скелет проекта.\n"
      "Далее: schema.sql -> lines.db, импорт combined.json, дерево файл->сцена->реплики."),
      &window);
  hello->setAlignment(Qt::AlignCenter);
  window.setCentralWidget(hello);

  // Меню (русский интерфейс обязателен)
  QMenu* fileMenu = window.menuBar()->addMenu(
      QApplication::translate("MainWindow", "&Файл"));
  fileMenu->addAction(QApplication::translate("MainWindow", "Выход"),
                      &window, &QWidget::close);

  window.show();
  return QApplication::exec();
}
