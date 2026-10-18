#include "Service.h"
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QDBusConnection>
#include <QDBusError>
#include <QTextStream>
#include <QTimer>
#include <csignal>

namespace { volatile std::sig_atomic_t interrupted = 0;
void stop(int) { interrupted = 1; } }

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    app.setApplicationName("pam-backend");
    QCommandLineParser parser;
    parser.setApplicationDescription("Plasma Audio Manager backend");
    parser.addHelpOption();
    parser.addOption({"snapshot", "Print registry snapshot and exit (read only)"});
    parser.process(app);
    pw_init(&argc, &argv);
    int result = 1;
    try {
        Graph graph;
        TemporaryDevices devices(graph);
        Router router(graph);
        Configuration configuration(graph, devices, router);
        Service service(graph, devices, router, configuration);
        auto bus = QDBusConnection::sessionBus();
        QObject::connect(&graph, &Graph::failed, &app, [&](const QString &error) {
            QTextStream(stderr) << error << '\n'; app.exit(1);
        });
        QObject::connect(&graph, &Graph::ready, &app, [&] {
            if (parser.isSet("snapshot")) {
                QTextStream(stdout) << graph.snapshot() << '\n'; app.quit(); return;
            }
            if (!bus.registerService("org.plasmaaudiomanager.Prototype1") ||
                !bus.registerObject("/org/plasmaaudiomanager/Prototype1", &service,
                    QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals)) {
                QTextStream(stderr) << bus.lastError().message() << '\n'; app.exit(1); return;
            }
            try { configuration.start(); }
            catch (const std::exception &e) { QTextStream(stderr) << e.what() << '\n'; app.exit(1); return; }
            QTextStream(stdout) << "Ready: org.plasmaaudiomanager.Prototype1\n";
        });
        QTimer timeout;
        timeout.setSingleShot(true);
        QObject::connect(&timeout, &QTimer::timeout, &app, [&] {
            QTextStream(stderr) << "PipeWire registry timed out\n"; app.exit(1);
        });
        QObject::connect(&graph, &Graph::ready, &timeout, &QTimer::stop);
        timeout.start(5000);
        std::signal(SIGINT, stop); std::signal(SIGTERM, stop);
        QTimer signalTimer;
        QObject::connect(&signalTimer, &QTimer::timeout, &app, [&] {
            if (interrupted) app.quit();
        });
        signalTimer.start(50);
        graph.start();
        result = app.exec();
        bus.unregisterObject("/org/plasmaaudiomanager/Prototype1");
        // Never unregister another instance's service name after a failed registration.
    } catch (const std::exception &e) {
        QTextStream(stderr) << e.what() << '\n';
    }
    pw_deinit();
    return result;
}
