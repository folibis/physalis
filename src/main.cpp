#include <QApplication>
#include <QCommandLineParser>
#include <QFileInfo>

#include "Icons.h"
#include "MainWindow.h"

// Generated per build; included here only, so stamping a new build number
// recompiles this one file instead of half the project.
#include "Version.h"

int main(int argc, char *argv[])
{
    // Qt6 scales for a high-DPI screen by itself and has deprecated both of
    // these; Qt5 does neither unless asked, and an unasked Qt5 build draws the
    // canvas and its SVG icons at the wrong size on a scaled display. They have
    // to be set before the QApplication exists, which is why they are here.
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif

    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("Physalis"));
    app.setWindowIcon(Icons::app());

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QCoreApplication::translate("main", "A 2D physics scene editor."));
    parser.addHelpOption();
    parser.addPositionalArgument(
        QCoreApplication::translate("main", "scene"),
        QCoreApplication::translate("main", "Scene file to open (*.phys)."));
    parser.process(app);

    MainWindow window;
    window.setVersion(QStringLiteral(PHYSALIS_VERSION_STRING));
    window.show();

    // Shown first, so a failure to open reports itself over a real window
    // rather than an empty screen. Double-clicking a .phys file in Explorer
    // arrives here as the first positional argument.
    const QStringList files = parser.positionalArguments();
    if (!files.isEmpty())
        window.openScene(QFileInfo(files.first()).absoluteFilePath());

    return app.exec();
}
