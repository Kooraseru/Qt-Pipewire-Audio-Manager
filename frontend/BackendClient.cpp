#include "BackendClient.h"
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusReply>
#include <QDBusServiceWatcher>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <algorithm>
#include <cmath>

namespace {
constexpr auto service = "org.plasmaaudiomanager.Prototype1";
constexpr auto path = "/org/plasmaaudiomanager/Prototype1";
QDBusMessage request(const QString &owner, const QString &method) {
    return QDBusMessage::createMethodCall(owner, path, service, method);
}
QString label(const QJsonObject &props) {
    for (const auto *key : {"node.description", "node.nick", "application.name", "node.name"}) {
        const auto value = props.value(key).toString();
        if (!value.isEmpty()) return value;
    }
    return QStringLiteral("Unnamed audio endpoint");
}
bool truth(const QJsonValue &value) {
    return value.toBool() || value.toString() == "true" || value.toString() == "1";
}
void sortRows(QVariantList &rows) {
    std::sort(rows.begin(), rows.end(), [](const QVariant &a, const QVariant &b) {
        return QString::localeAwareCompare(a.toMap().value("label").toString(),
                                           b.toMap().value("label").toString()) < 0;
    });
}
QVariantMap endpoint(const QJsonObject &node) {
    const auto props = node.value("properties").toObject();
    const auto mixer = node.value("mixer").toObject();
    const bool source = props.value("media.class").toString().startsWith("Audio/Source");
    auto state = node.value("state").toString("unknown");
    if (state == "suspended" || state == "idle") state = "Idle";
    else if (state == "running") state = "Active";
    else if (state == "error") state = "Error";
    return {{"key", props.value("object.serial").toString()}, {"label", label(props)},
        {"nodeName", props.value("node.name").toString()}, {"state", state},
        {"icon", source ? "audio-input-microphone" : "audio-speakers"},
        {"kind", source ? "source" : "sink"}, {"owned", false}, {"token", ""},
        {"detail", props.value("audio.position").toString()},
        {"volumeAvailable", mixer.value("volumeAvailable").toBool()},
        {"muteAvailable", mixer.value("muteAvailable").toBool()},
        {"volumePercent", qRound(std::cbrt(std::max(0.0, mixer.value("volume").toDouble())) * 100)},
        {"muted", mixer.value("muted").toBool()}};
}
}

BackendClient::BackendClient(QObject *parent) : QObject(parent) {
    debounce_.setSingleShot(true);
    debounce_.setInterval(75);
    connect(&debounce_, &QTimer::timeout, this, &BackendClient::refresh);
    auto bus = QDBusConnection::sessionBus();
    auto *watcher = new QDBusServiceWatcher(service, bus,
        QDBusServiceWatcher::WatchForOwnerChange, this);
    connect(watcher, &QDBusServiceWatcher::serviceOwnerChanged, this, &BackendClient::ownerChanged);
    bus.connect(service, path, service, "GraphChanged", this, SLOT(graphChanged(QString)));
    bus.connect(service, path, service, "OperationFailed", this, SLOT(backendOperationFailed(QString)));
    bus.connect(service, path, service, "RoutesChanged", this, SLOT(routesChanged()));
    QTimer::singleShot(0, this, [this] {
        auto *interface = QDBusConnection::sessionBus().interface();
        if (!interface) { ownerChanged({}, {}, {}); return; }
        // Resolve a unique owner so queued replies cannot cross backend restarts.
        const QDBusReply<QString> reply = interface->serviceOwner(service);
        ownerChanged({}, {}, reply.isValid() ? reply.value() : QString());
    });
}

void BackendClient::ownerChanged(const QString &, const QString &, const QString &owner) {
    ++generation_;
    owner_ = owner;
    connected_ = false;
    refreshing_ = false;
    busy_ = false;
    refreshAgain_ = false;
    objects_ = {}; owned_ = {};
    routing_ = {}; updateRoutingKeys();
    rebuildModels();
    debounce_.stop();
    clearError();
    status_ = owner.isEmpty() ? "Backend offline" : "Connecting to PipeWire";
    emit statusChanged();
    if (!owner.isEmpty()) refresh();
}

void BackendClient::graphChanged(const QString &) { scheduleRefresh(); }
void BackendClient::routesChanged() { scheduleRefresh(); }
void BackendClient::scheduleRefresh() {
    if (refreshing_) refreshAgain_ = true;
    else if (!debounce_.isActive()) debounce_.start();
}

void BackendClient::refresh() {
    if (owner_.isEmpty()) return;
    if (refreshing_) { refreshAgain_ = true; return; }
    refreshing_ = true;
    const auto generation = generation_;
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(
        request(owner_, "Snapshot"), 3000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, generation](auto *finished) {
        QDBusPendingReply<QString> reply = *finished;
        finished->deleteLater();
        if (generation != generation_) return;
        if (reply.isError()) {
            refreshing_ = false; connected_ = false;
            status_ = "Backend unavailable"; setError(reply.error().message());
            objects_ = {}; owned_ = {}; routing_ = {}; updateRoutingKeys(); rebuildModels(); emit modelsChanged(); emit statusChanged(); return;
        }
        QJsonParseError parseError;
        auto document = QJsonDocument::fromJson(reply.value().toUtf8(), &parseError);
        if (parseError.error != QJsonParseError::NoError ||
            document.object().value("schemaVersion").toInt() != 1 ||
            !document.object().value("objects").isArray()) {
            refreshing_ = false; connected_ = false;
            status_ = "Unsupported backend response";
            setError("The backend returned an invalid graph snapshot.");
            objects_ = {}; owned_ = {}; routing_ = {}; updateRoutingKeys(); rebuildModels(); emit modelsChanged(); emit statusChanged(); return;
        }
        objects_ = document.object().value("objects").toArray();
        auto *ownedWatcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(
            request(owner_, "OwnedDevices"), 3000), this);
        connect(ownedWatcher, &QDBusPendingCallWatcher::finished, this, [this, generation](auto *done) {
            QDBusPendingReply<QString> ownedReply = *done;
            done->deleteLater();
            if (generation != generation_) return;
            bool devicesValid = false;
            QString nextStatus;
            if (ownedReply.isError()) {
                owned_ = {}; nextStatus = "Backend update required";
                setError(ownedReply.error().message());
            } else {
                QJsonParseError parse;
                auto doc = QJsonDocument::fromJson(ownedReply.value().toUtf8(), &parse);
                devicesValid = parse.error == QJsonParseError::NoError && doc.isArray();
                owned_ = devicesValid ? doc.array() : QJsonArray{};
                nextStatus = devicesValid ? "Connected to PipeWire" : "Invalid device response";
                if (!devicesValid) setError("Cannot read backend device ownership.");
            }
            auto *routingWatcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(
                request(owner_, "RoutingSnapshot"), 3000), this);
            connect(routingWatcher, &QDBusPendingCallWatcher::finished, this, [this, generation, devicesValid, nextStatus](auto *finished) {
                QDBusPendingReply<QString> reply = *finished; finished->deleteLater();
                if (generation != generation_) return;
                refreshing_ = false;
                const auto routing = reply.isError() ? QVariantMap{} : QJsonDocument::fromJson(reply.value().toUtf8()).object().toVariantMap();
                const bool routingChanged = routing != routing_;
                routing_ = routing;
                updateRoutingKeys();
                rebuildModels();
                connected_ = devicesValid;
                status_ = nextStatus;
                if (routingChanged) emit modelsChanged();
                emit statusChanged();
                if (refreshAgain_) { refreshAgain_ = false; scheduleRefresh(); }
            });
        });
    });
}

void BackendClient::createDevice(const QString &kind, const QString &text) {
    if (!connected_ || busy_) return;
    const QString name = text.trimmed();
    if (name.isEmpty() || name.size() > 128) { setError("Enter a name between 1 and 128 characters."); return; }
    busy_ = true; clearError(); emit statusChanged();
    auto message = request(owner_, "CreatePersistentDevice");
    message.setArguments({kind, name});
    const auto generation = generation_;
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message, 5000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, generation](auto *finished) {
        QDBusPendingReply<QString> reply = *finished;
        finished->deleteLater();
        if (generation != generation_) return;
        busy_ = false; emit statusChanged();
        if (reply.isError()) setError(reply.error().message());
        else emit deviceCreated();
        scheduleRefresh();
    });
}

void BackendClient::destroyDevice(const QString &token) {
    if (!connected_ || busy_) return;
    // UI defense as well as the backend's authoritative ownership check.
    bool owned = false;
    for (const auto &entry : owned_) if (entry.toObject().value("token").toString() == token) owned = true;
    if (!owned) { setError("This device is managed outside Audio Manager."); return; }
    busy_ = true; clearError(); emit statusChanged();
    auto message = request(owner_, "DestroyTemporaryDevice"); message.setArguments({token});
    const auto generation = generation_;
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message, 5000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, generation](auto *finished) {
        QDBusPendingReply<bool> reply = *finished; finished->deleteLater();
        if (generation != generation_) return;
        busy_ = false; emit statusChanged();
        if (reply.isError()) setError(reply.error().message());
        else if (!reply.value()) setError("The device is no longer owned by this backend.");
        scheduleRefresh();
    });
}

void BackendClient::clearError() { setError({}); }
void BackendClient::backendOperationFailed(const QString &message) { setError(message); scheduleRefresh(); }
void BackendClient::control(const QString &method, const QVariantList &arguments) {
    if (!connected_) return;
    clearError();
    auto message = request(owner_, method); message.setArguments(arguments);
    const auto generation = generation_;
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message, 3000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, generation](auto *done) {
        QDBusPendingReply<bool> reply = *done; done->deleteLater();
        if (generation != generation_) return;
        if (reply.isError()) setError(reply.error().message());
        scheduleRefresh();
    });
}
void BackendClient::setVolume(const QString &serial, double percent) {
    if (!std::isfinite(percent) || percent < 0 || percent > 100) {
        setError("Volume must be between 0 and 100 percent."); return;
    }
    control("SetVolume", {serial, std::pow(percent / 100.0, 3)});
}
void BackendClient::setMute(const QString &serial, bool muted) { control("SetMute", {serial, muted}); }
void BackendClient::setRoute(const QString &source, const QString &destination, bool enabled) { control("SetRoute", {source, destination, enabled}); }
void BackendClient::clearRoutes() { control("ClearRoutes", {}); }
void BackendClient::forgetRoute(const QString &id) { control("ForgetRoute", {id}); }
void BackendClient::setError(const QString &message) {
    if (error_ == message) return;
    error_ = message; emit errorChanged();
}

void BackendClient::rebuildModels() {
    QVariantList outputs, inputs, virtuals, applications;
    QMap<int, QJsonObject> nodes;
    QSet<QString> ownedNames;
    for (const auto &value : objects_) {
        const auto object = value.toObject();
        if (object.value("type").toString().endsWith(":Node"))
            nodes.insert(object.value("id").toInt(), object);
    }
    for (const auto &value : owned_) {
        const auto object = value.toObject();
        const auto prefix = object.value("nodePrefix").toString();
        const auto kind = object.value("kind").toString();
        QVariantList endpoints;
        int present = 0;
        for (const auto &node : nodes) {
            const auto props = node.value("properties").toObject();
            const auto name = props.value("node.name").toString();
            if (name == prefix || name == prefix + ".sink" || name == prefix + ".source") {
                ownedNames.insert(name); ++present;
                endpoints.append(endpoint(node));
            }
        }
        virtuals.append(QVariantMap{{"label", object.value("label").toString()},
            {"key", object.value("token").toString()}, {"token", object.value("token").toString()},
            {"owned", true}, {"kind", kind}, {"icon", kind == "cable" ? "insert-link" : "audio-card"},
            {"detail", kind == "cable" ? "Output sink and microphone input" : "Virtual output sink"},
            {"persistent", object.value("persistent").toBool()},
            {"state", present == (kind == "cable" ? 2 : 1) ? (object.value("persistent").toBool() ? "Saved" : "Temporary") : "Waiting for endpoints"},
            {"endpoints", endpoints}});
    }
    // Pair remap/monitor cables by actual filter-group and target metadata.
    // Names are display labels, never evidence of a cable relationship.
    QMap<int, QSet<int>> sourceSinks, sinkSources;
    for (auto source = nodes.begin(); source != nodes.end(); ++source) {
        const auto props = source.value().value("properties").toObject();
        const auto group = props.value("node.link-group").toString();
        if (!props.value("media.class").toString().startsWith("Audio/Source") || group.isEmpty() ||
            (!truth(props.value("node.virtual")) && props.value("device.class").toString() != "filter") ||
            ownedNames.contains(props.value("node.name").toString())) continue;
        for (const auto &helper : nodes) {
            const auto hp = helper.value("properties").toObject();
            if (hp.value("node.link-group").toString() != group || !truth(hp.value("stream.capture.sink")) ||
                hp.value("media.class").toString() != "Stream/Input/Audio") continue;
            const auto target = hp.value("target.object").toString();
            if (target.isEmpty()) continue;
            for (auto sink = nodes.begin(); sink != nodes.end(); ++sink) {
                const auto sp = sink.value().value("properties").toObject();
                if (!sp.value("media.class").toString().startsWith("Audio/Sink") ||
                    ownedNames.contains(sp.value("node.name").toString())) continue;
                if (target == sp.value("node.name").toString() || target == sp.value("object.serial").toString()) {
                    sourceSinks[source.key()].insert(sink.key());
                    sinkSources[sink.key()].insert(source.key());
                }
            }
        }
    }
    QSet<int> paired;
    for (auto it = sourceSinks.begin(); it != sourceSinks.end(); ++it) {
        if (it.value().size() != 1) continue;
        const auto sink = *it.value().begin();
        if (sinkSources.value(sink).size() != 1) continue;
        auto output = endpoint(nodes.value(sink));
        auto input = endpoint(nodes.value(it.key()));
        paired.insert(sink); paired.insert(it.key());
        virtuals.append(QVariantMap{{"key", "external-cable:" + output.value("key").toString() + ":" + input.value("key").toString()},
            {"label", output.value("label")}, {"owned", false}, {"token", ""}, {"kind", "cable"},
            {"icon", "insert-link"}, {"detail", "Output sink and microphone input"},
            {"state", "Managed externally"}, {"endpoints", QVariantList{output, input}}});
    }
    for (auto it = nodes.begin(); it != nodes.end(); ++it) {
        const auto object = it.value();
        const auto props = object.value("properties").toObject();
        const auto media = props.value("media.class").toString();
        const auto name = props.value("node.name").toString();
        const bool source = media.startsWith("Audio/Source");
        const bool sink = media.startsWith("Audio/Sink");
        const bool isVirtual = truth(props.value("node.virtual")) ||
            props.value("factory.name").toString() == "support.null-audio-sink" ||
            props.value("device.class").toString() == "filter";
        QVariantMap row = endpoint(object);
        if (sink || source) {
            if (ownedNames.contains(name) || paired.contains(it.key())) continue;
            if (isVirtual) {
                row["state"] = "Managed externally";
                row["detail"] = source ? "Microphone input endpoint" : "Output sink endpoint";
                row["endpoints"] = QVariantList{endpoint(object)};
                virtuals.append(row);
            } else if (source) inputs.append(row);
            else outputs.append(row);
        } else if ((media == "Stream/Output/Audio" || media == "Stream/Input/Audio") && !isVirtual) {
            const bool playback = media == "Stream/Output/Audio";
            row["icon"] = playback ? "audio-volume-high" : "audio-input-microphone";
            row["direction"] = playback ? "Playback" : "Recording";
            row["detail"] = props.value("application.name").toString(label(props));
            QStringList routes;
            for (const auto &entry : objects_) {
                const auto link = entry.toObject();
                if (!link.value("type").toString().endsWith(":Link")) continue;
                const auto lp = link.value("properties").toObject();
                const int from = lp.value("link.output.node").toString().toInt();
                const int to = lp.value("link.input.node").toString().toInt();
                if ((playback && from == it.key()) || (!playback && to == it.key())) {
                    const auto other = nodes.value(playback ? to : from).value("properties").toObject();
                    const auto destination = label(other);
                    if (!routes.contains(destination)) routes.append(destination);
                }
            }
            row["routes"] = routes;
            applications.append(row);
        }
    }
    sortRows(outputs); sortRows(inputs); sortRows(virtuals); sortRows(applications);
    if (outputs_ == outputs && inputs_ == inputs && virtualDevices_ == virtuals && applications_ == applications) return;
    outputs_ = outputs; inputs_ = inputs; virtualDevices_ = virtuals; applications_ = applications;
    emit modelsChanged();
}

void BackendClient::updateRoutingKeys() {
    QVariantList sources, destinations;
    for (const auto &row : routing_.value("sources").toList()) sources.append(row.toMap().value("key"));
    for (const auto &row : routing_.value("destinations").toList()) destinations.append(row.toMap().value("key"));
    if (sources == routingSourceKeys_ && destinations == routingDestinationKeys_) return;
    routingSourceKeys_ = sources; routingDestinationKeys_ = destinations;
    emit routingEndpointsChanged();
}

void BackendClient::setRecordingSource(const QString &stream, const QString &source) { control("SetRecordingSource", {stream, source}); }
void BackendClient::forgetRecordingAssignment(const QString &id) { control("ForgetRecordingAssignment", {id}); }
