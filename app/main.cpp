#include "mainwindow.h"

#include "dubstudio/database.h"

#include <QApplication>
#include <QDir>
#include <QMessageBox>
#include <QString>

#include <exception>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("DubStudio"));
    QApplication::setApplicationName(QStringLiteral("DubStudio"));

    dubstudio::applyDarkTheme(); // тёмная тема по умолчанию (AGENTS.md, правило 1)

    // Рабочая директория проекта по умолчанию (PLAN.md, раздел 12).
    const QString workDir = QStringLiteral("MyDub");
    QDir().mkpath(workDir);
    const QString dbPath = workDir + QStringLiteral("/lines.db");

    try {
        dubstudio::MainWindow window(dbPath);
        window.show();
        return QApplication::exec();
    } catch (const std::exception& e) {
        QMessageBox::critical(nullptr, QStringLiteral("Запуск невозможен"),
                              QString::fromUtf8(e.what()));
        return 1;
    }
}
