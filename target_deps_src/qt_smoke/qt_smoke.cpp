// Minimal Qt5 smoke test for the OHOS (musl) board build.
// Runs with -platform offscreen (no display on the board).
#include <QApplication>
#include <QLabel>
#include <QTimer>
#include <QDebug>
#include <QFileInfo>
#include <QPluginLoader>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    qInfo() << "Qt version:" << qVersion();
    qInfo() << "Platform:" << app.platformName();
    qInfo() << "Library paths:" << QApplication::libraryPaths();

    QLabel label(QStringLiteral("qt smoke ok"));
    label.resize(200, 50);
    label.show();
    qInfo() << "Widget created, size:" << label.size();

    // Exit after the event loop spins once so we can verify it runs.
    QTimer::singleShot(0, &app, [&]() {
        qInfo() << "Event loop ran, quitting";
        app.quit();
    });
    return app.exec();
}
