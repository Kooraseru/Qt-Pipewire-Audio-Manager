#include "devices/TemporaryDevices.h"
#include "pipewire/Graph.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <QTimer>
#include <cerrno>

// Graph publishes an announcement before issuing its bind. Delete the announced
// test node there: server request ordering deterministically makes the bind stale.
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    pw_init(&argc, &argv);
    int result;
    {
        Graph graph;
        TemporaryDevices devices(graph);
        pw_proxy *created = nullptr;
        spa_hook errorsHook{};
        bool staleSeen = false;
        bool failure = false;
        QObject::connect(&graph, &Graph::failed, &app, [&](const QString &error) {
            QTextStream(stderr) << error << '\n'; failure = true; app.exit(1);
        });
        QObject::connect(&graph, &Graph::operationFailed, &app, [&](const QString &error) {
            QTextStream(stderr) << "Unexpected operation error: " << error << '\n';
            failure = true; app.exit(1);
        });
        QObject::connect(&graph, &Graph::changed, &app, [&](const QString &json) {
            const auto event = QJsonDocument::fromJson(json.toUtf8()).object();
            const auto props = event.value("object").toObject().value("properties").toObject();
            if (event.value("event") == "added" && props.value("node.name") == "pam-race-deleted" && created) {
                pw_proxy_destroy(created);
                created = nullptr;
            }
        });
        struct ErrorState { bool *seen; } errors{&staleSeen};
        QObject::connect(&graph, &Graph::ready, &app, [&] {
            static const pw_core_events events = [] {
                pw_core_events e{}; e.version = PW_VERSION_CORE_EVENTS;
                e.error = [](void *data, uint32_t id, int, int code, const char *) {
                    if (id != PW_ID_CORE && (code == -ESTALE || code == -ENOENT))
                        *static_cast<ErrorState *>(data)->seen = true;
                };
                return e;
            }();
            pw_core_add_listener(graph.core(), &errorsHook, &events, &errors);
            auto *props = pw_properties_new("factory.name", "support.null-audio-sink",
                "node.name", "pam-race-deleted", "media.class", "Audio/Sink",
                "object.linger", "false", "audio.position", "[ FL FR ]", nullptr);
            created = static_cast<pw_proxy *>(pw_core_create_object(graph.core(), "adapter",
                PW_TYPE_INTERFACE_Node, PW_VERSION_NODE, &props->dict, 0));
            pw_properties_free(props);
            if (!created) app.exit(1);
        });
        QString healthyToken;
        QTimer progress;
        QObject::connect(&progress, &QTimer::timeout, &app, [&] {
            if (!staleSeen) return;
            if (healthyToken.isEmpty()) {
                healthyToken = devices.create("sink", "Healthy after stale bind"); return;
            }
            const auto snapshot = graph.snapshot();
            if (!snapshot.contains("Healthy after stale bind")) return;
            if (snapshot.contains("pam-race-deleted") || failure) { app.exit(1); return; }
            devices.destroy(healthyToken);
            QTextStream(stdout) << "PASS: vanished-node bind is nonfatal; discovery and creation continue\n";
            app.exit(0);
        });
        progress.start(20);
        QTimer::singleShot(5000, &app, [&] {
            QTextStream(stderr) << "Stale bind regression timed out\n"; app.exit(1);
        });
        graph.start();
        result = app.exec();
        if (created) pw_proxy_destroy(created);
        spa_hook_remove(&errorsHook);
    }
    pw_deinit();
    return result;
}
