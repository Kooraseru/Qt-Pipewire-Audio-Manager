#include <QGuiApplication>
#include <QCommandLineParser>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QQuickStyle>
#include <QQuickItem>
#include <QTimer>
#include <QImage>
#include <QTextStream>
#include <QIcon>

int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    app.setApplicationName("Plasma Audio Manager");
    app.setOrganizationName("PlasmaAudioManager");
    app.setDesktopFileName("org.kooraseru.plasma.audiomanager");
    app.setWindowIcon(QIcon::fromTheme("audio-card"));
    if (qEnvironmentVariableIsEmpty("QT_QUICK_CONTROLS_STYLE"))
        QQuickStyle::setStyle("org.kde.desktop");
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({"capture", "Save a GUI screenshot and exit", "file"});
    parser.addOption({"tab", "Initial tab: devices, cables, applications or routing", "tab", "devices"});
    parser.process(app);
    const QStringList tabs{"devices", "cables", "applications", "routing"};
    const int tab = tabs.indexOf(parser.value("tab"));
    if (tab < 0) { QTextStream(stderr) << "Invalid --tab value\n"; return 1; }
    QQmlApplicationEngine engine;
    if (QCoreApplication::applicationDirPath() == QStringLiteral(PAM_GUI_BUILD_DIR))
        engine.addImportPath(QStringLiteral(PAM_QML_BUILD_DIR));
    engine.load(QUrl("qrc:/Preview.qml"));
    if (engine.rootObjects().isEmpty()) return 1;
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    auto *popup = window->findChild<QObject *>("audioPopup");
    if (popup) popup->setProperty("currentTab", tab);
    if (parser.isSet("capture")) {
        QTimer::singleShot(1200, &app, [&] {
            const bool saved = window->grabWindow().save(parser.value("capture"));
            app.exit(saved ? 0 : 1);
        });
    }
    return app.exec();
}
