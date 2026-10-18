#include "Configuration.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>
#include <cmath>
#include <stdexcept>

namespace {
QJsonObject props(const QJsonObject &n) { return n.value("properties").toObject(); }
QString serial(const QJsonObject &n) { return props(n).value("object.serial").toString(); }
QString label(const QJsonObject &n) {
    const auto p = props(n);
    for (auto key : {"node.description", "application.name", "node.nick", "node.name"})
        if (!p.value(key).toString().isEmpty()) return p.value(key).toString();
    return "Audio endpoint";
}
bool matches(const QJsonObject &n, const QJsonObject &selector) {
    const auto p = props(n);
    for (auto it = selector.begin(); it != selector.end(); ++it)
        if (p.value(it.key()) != it.value()) return false;
    return !selector.isEmpty();
}
void invalid(const QString &message) { throw std::runtime_error(message.toStdString()); }
bool validSelector(const QJsonValue &v) {
    if (!v.isObject()) return false;
    const auto s = v.toObject();
    const auto media = s.value("media.class").toString();
    if (media.isEmpty()) return false;
    if (media == "Stream/Output/Audio" || media == "Stream/Input/Audio") {
        if (s.value("application.id").toString().isEmpty() && s.value("application.process.binary").toString().isEmpty()) return false;
    } else if (s.value("node.name").toString().isEmpty()) return false;
    const QSet<QString> allowed{"media.class", "node.name", "application.id", "application.process.binary", "application.name", "media.role", "stream.restore.id", "device.serial", "api.bluez5.address"};
    for (auto it = s.begin(); it != s.end(); ++it)
        if (!allowed.contains(it.key()) || !it.value().isString() || it.value().toString().isEmpty()) return false;
    return true;
}
}

Configuration::Configuration(Graph &graph, TemporaryDevices &devices, Router &router)
    : graph_(graph), devices_(devices), router_(router),
      path_(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/plasma-audio-manager/config.json"),
      data_{{"schemaVersion", 1}, {"devices", QJsonArray{}}, {"routes", QJsonArray{}}} {
    debounce_.setSingleShot(true); debounce_.setInterval(120);
    connect(&graph_, &Graph::changed, this, [this] { schedule(); });
    connect(&router_, &Router::changed, this, [this] { schedule(); emit changed(); });
    connect(&debounce_, &QTimer::timeout, this, &Configuration::reconcile);
}

void Configuration::start() {
    if (!QDir().mkpath(QFileInfo(path_).absolutePath())) invalid("Cannot create Audio Manager configuration directory");
    lock_ = std::make_unique<QLockFile>(path_ + ".lock");
    if (!lock_->tryLock(0)) invalid("Audio Manager configuration is in use by another backend");
    QFile file(path_);
    if (file.exists()) {
        if (!file.open(QIODevice::ReadOnly) || file.size() > 2 * 1024 * 1024) invalid("Cannot read Audio Manager configuration");
        QJsonParseError error;
        const auto doc = QJsonDocument::fromJson(file.readAll(), &error);
        auto data = doc.object();
        if (error.error != QJsonParseError::NoError || !doc.isObject() || data.value("schemaVersion") != QJsonValue(1) ||
            !data.value("devices").isArray() || !data.value("routes").isArray())
            invalid("Invalid or unsupported Audio Manager configuration; original file preserved");
        QSet<QString> ids;
        for (const auto &entry : data.value("devices").toArray()) {
            const auto d = entry.toObject(); const auto id = d.value("token").toString();
            if (QUuid(id).isNull() || ids.contains(id) || (d.value("kind") != "sink" && d.value("kind") != "cable") ||
                d.value("label").toString().trimmed().isEmpty() || d.value("label").toString().size() > 128 || d.value("label").toString().contains(QChar::Null))
                invalid("Invalid saved device; original configuration preserved");
            ids.insert(id);
            const auto controls = d.value("controls");
            if (!controls.isUndefined() && !controls.isObject()) invalid("Invalid saved mixer controls");
            const auto controlMap = controls.toObject();
            for (auto c = controlMap.begin(); c != controlMap.end(); ++c) {
                if ((c.key() != "sink" && c.key() != "source") || !c.value().isObject()) invalid("Invalid saved mixer endpoint");
                const auto values = c.value().toObject();
                for (auto v = values.begin(); v != values.end(); ++v) {
                    if (v.key() == "volume" && v.value().isDouble() && v.value().toDouble() >= 0 && v.value().toDouble() <= 1) continue;
                    if (v.key() == "muted" && v.value().isBool()) continue;
                    invalid("Invalid saved mixer value");
                }
            }
        }
        ids.clear();
        for (const auto &entry : data.value("routes").toArray()) {
            const auto r = entry.toObject(); const auto id = r.value("id").toString();
            if (QUuid(id).isNull() || ids.contains(id) || !validSelector(r.value("source")) || !validSelector(r.value("destination")))
                invalid("Invalid saved route; original configuration preserved");
            ids.insert(id);
        }
        if (data.contains("recordingAssignments") && !data.value("recordingAssignments").isArray()) invalid("Invalid recording assignments");
        ids.clear();
        for (const auto &entry : data.value("recordingAssignments").toArray()) {
            const auto r = entry.toObject(); const auto id = r.value("id").toString();
            if (QUuid(id).isNull() || ids.contains(id) || !validSelector(r.value("application")) || !validSelector(r.value("source")) ||
                r.value("application").toObject().value("media.class") != "Stream/Input/Audio" ||
                !r.value("source").toObject().value("media.class").toString().startsWith("Audio/Source")) invalid("Invalid saved recording assignment; original configuration preserved");
            ids.insert(id);
        }
        data_ = data;
    }
    if (!file.exists()) commit(data_);
    // Recreate only this application's saved UUIDs. External devices are never adopted.
    for (const auto &entry : data_.value("devices").toArray()) {
        const auto d = entry.toObject();
        devices_.create(d.value("kind").toString(), d.value("label").toString(), d.value("token").toString());
    }
    schedule();
}

void Configuration::commit(const QJsonObject &data) {
    if (!lock_ || !lock_->isLocked()) invalid("Audio Manager configuration has not been opened");
    QSaveFile file(path_);
    if (!file.open(QIODevice::WriteOnly)) invalid("Cannot save Audio Manager configuration: " + file.errorString());
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    const auto bytes = QJsonDocument(data).toJson(QJsonDocument::Indented);
    if (file.write(bytes) != bytes.size() || !file.commit()) invalid("Cannot commit Audio Manager configuration: " + file.errorString());
    data_ = data;
    emit changed();
}
QList<QJsonObject> Configuration::nodes() const {
    QList<QJsonObject> result;
    for (const auto &v : QJsonDocument::fromJson(graph_.snapshot().toUtf8()).object().value("objects").toArray()) {
        const auto o = v.toObject();
        if (o.value("type").toString().endsWith(":Node")) result.append(o);
    }
    return result;
}
QJsonObject Configuration::node(const QString &key) const {
    for (const auto &n : nodes()) if (serial(n) == key) return n;
    invalid("The audio endpoint disappeared"); return {};
}
QJsonObject Configuration::selector(const QJsonObject &n) const {
    const auto p = props(n); QJsonObject s{{"media.class", p.value("media.class")}};
    if (p.value("media.class") == "Stream/Output/Audio" || p.value("media.class") == "Stream/Input/Audio") {
        const auto identity = !p.value("application.id").toString().isEmpty() ? "application.id" : "application.process.binary";
        if (p.value(identity).toString().isEmpty()) invalid("This stream does not advertise a stable application identity");
        s[identity] = p.value(identity);
        for (auto key : {"application.name", "media.role", "stream.restore.id"})
            if (!p.value(key).toString().isEmpty()) s[key] = p.value(key);
    } else {
        if (p.value("node.name").toString().isEmpty()) invalid("This endpoint does not advertise a stable name");
        s["node.name"] = p.value("node.name");
        for (auto key : {"device.serial", "api.bluez5.address"}) if (!p.value(key).toString().isEmpty()) s[key] = p.value(key);
    }
    return s;
}
QString Configuration::create(const QString &kind, const QString &label) {
    if ((kind != "sink" && kind != "cable") || label.trimmed().isEmpty() || label.size() > 128 || label.contains(QChar::Null)) invalid("Invalid virtual device kind or label");
    auto data = data_; auto devices = data.value("devices").toArray();
    const auto token = QUuid::createUuid().toString(QUuid::WithoutBraces);
    devices.append(QJsonObject{{"token", token}, {"kind", kind}, {"label", label}}); data["devices"] = devices;
    commit(data);
    try { devices_.create(kind, label, token); }
    catch (...) {
        // Keep the saved definition visible for diagnosis/restart rather than silently losing intent.
        emit operationFailed("Virtual device saved but creation failed; restart the backend to retry"); throw;
    }
    emit changed(); return token;
}
bool Configuration::destroy(const QString &token) {
    auto data = data_; QJsonArray devices; bool found = false;
    for (const auto &entry : data.value("devices").toArray()) {
        if (entry.toObject().value("token") == token) found = true; else devices.append(entry);
    }
    if (!found) return devices_.destroy(token);
    QJsonArray routes; QStringList removed;
    for (const auto &entry : data.value("routes").toArray()) {
        const auto r = entry.toObject(); const auto prefix = "pam.prototype." + token;
        const auto refers = [&](const QJsonObject &s) { const auto name = s.value("node.name").toString(); return name == prefix || name == prefix + ".sink" || name == prefix + ".source"; };
        if (!refers(r.value("source").toObject()) && !refers(r.value("destination").toObject())) routes.append(entry);
        else removed.append(r.value("id").toString());
    }
    QJsonArray inputs;
    for (const auto &entry : data.value("recordingAssignments").toArray()) {
        const auto name = entry.toObject().value("source").toObject().value("node.name").toString();
        const auto prefix = "pam.prototype." + token;
        if (name != prefix && name != prefix + ".source") inputs.append(entry);
    }
    data["recordingAssignments"] = inputs;
    data["devices"] = devices; data["routes"] = routes; commit(data);
    for (const auto &id : removed) removeLive(id);
    devices_.destroy(token); schedule(); return true;
}
void Configuration::removeLive(const QString &id) {
    if (live_.contains(id)) {
        const auto pair = live_.take(id);
        try { router_.setRoute(pair.first, pair.second, false); } catch (const std::exception &) {}
    }
    attempted_.remove(id); status_.remove(id);
}
void Configuration::setRoute(const QString &source, const QString &destination, bool enabled) {
    const auto a = node(source), b = node(destination);
    const auto from = selector(a), to = selector(b);
    auto data = data_; auto routes = data.value("routes").toArray();
    QString existing;
    for (const auto &entry : routes) {
        const auto r = entry.toObject();
        if (r.value("source").toObject() == from && r.value("destination").toObject() == to) existing = r.value("id").toString();
    }
    if (!enabled) {
        if (existing.isEmpty()) invalid("Only saved Audio Manager routes can be removed here");
        forgetRoute(existing); return;
    }
    const auto reason = router_.rejection(source, destination);
    if (!reason.isEmpty()) invalid(reason);
    // Do not create a rule that already ambiguously matches another live stream.
    int sources = 0, destinations = 0;
    for (const auto &n : nodes()) { sources += matches(n, from); destinations += matches(n, to); }
    if (sources != 1 || destinations != 1) invalid("Several endpoints share this identity; refusing an ambiguous saved route");
    if (existing.isEmpty()) {
        existing = QUuid::createUuid().toString(QUuid::WithoutBraces);
        routes.append(QJsonObject{{"id", existing}, {"source", from}, {"destination", to},
            {"sourceLabel", label(a)}, {"destinationLabel", label(b)}});
        data["routes"] = routes; commit(data);
    }
    attempted_.remove(existing);
    reconcile();
}
void Configuration::forgetRoute(const QString &id) {
    auto data = data_; QJsonArray routes; bool found = false;
    for (const auto &entry : data.value("routes").toArray()) {
        if (entry.toObject().value("id") == id) found = true; else routes.append(entry);
    }
    if (!found) invalid("Saved route not found");
    data["routes"] = routes; commit(data); removeLive(id); emit changed();
}
void Configuration::clearRoutes() {
    auto data = data_; data["routes"] = QJsonArray{}; commit(data);
    router_.clear(); live_.clear(); attempted_.clear(); status_.clear(); emit changed();
}
void Configuration::schedule() { if (lock_ && !debounce_.isActive()) debounce_.start(); }
void Configuration::reconcile() {
    const auto all = nodes();
    QMap<QString, QStringList> portGenerations;
    QMap<QString, QString> nodeSerials;
    for (const auto &n : all) nodeSerials[QString::number(n.value("id").toInt())] = serial(n);
    for (const auto &v : QJsonDocument::fromJson(graph_.snapshot().toUtf8()).object().value("objects").toArray()) {
        const auto o = v.toObject(); const auto p = props(o);
        if (o.value("type").toString().endsWith(":Port"))
            portGenerations[nodeSerials.value(p.value("node.id").toString())].append(p.value("object.serial").toString());
    }
    for (auto &generation : portGenerations) generation.sort();
    const auto runtime = QJsonDocument::fromJson(router_.routes().toUtf8()).array();
    for (const auto &entry : data_.value("routes").toArray()) {
        const auto r = entry.toObject(); const auto id = r.value("id").toString();
        QList<QString> sources, destinations;
        for (const auto &n : all) {
            if (matches(n, r.value("source").toObject())) sources.append(serial(n));
            if (matches(n, r.value("destination").toObject())) destinations.append(serial(n));
        }
        QString state, reason;
        if (sources.size() != 1 || destinations.size() != 1) {
            removeLive(id);
            state = "Waiting";
            reason = sources.size() > 1 ? "Several application/source streams match; waiting for a unique match" :
                destinations.size() > 1 ? "Several destinations match; waiting for a unique match" :
                sources.isEmpty() ? "Waiting for source/application" : "Waiting for destination";
        } else {
            const auto a = sources.first(), b = destinations.first(); const auto pair = a + ":" + b + ":" + portGenerations.value(a).join(",") + ":" + portGenerations.value(b).join(",");
            if (live_.contains(id) && live_[id] != qMakePair(a, b)) removeLive(id);
            QJsonObject owned;
            for (const auto &v : runtime) if (v.toObject().value("source") == a && v.toObject().value("destination") == b) owned = v.toObject();
            if (!owned.isEmpty() && (owned.value("state") != "Failed" || attempted_.value(id) == pair)) {
                state = owned.value("state").toString(); reason = owned.value("error").toString();
                live_[id] = {a, b}; attempted_[id] = pair;
            } else if (router_.rejection(a, b).startsWith("Existing route managed externally")) {
                attempted_[id] = pair;
                state = "External"; reason = "Existing external route satisfies this rule; it will not be managed here";
            } else if (attempted_.value(id) == pair) {
                state = "Blocked"; reason = "Route removed externally; toggle it to retry, or wait for a new stream";
            } else {
                const auto why = router_.rejection(a, b);
                if (!why.isEmpty()) { state = "Waiting"; reason = why; }
                else {
                    attempted_[id] = pair;
                    try { router_.setRoute(a, b, true); live_[id] = {a, b}; state = "Connecting"; }
                    catch (const std::exception &e) { state = "Failed"; reason = QString::fromUtf8(e.what()); }
                }
            }
        }
        status_[id] = QJsonObject{{"state", state}, {"reason", reason}};
    }
    // Restore explicit controls only for saved project devices, once per new serial.
    for (const auto &entry : data_.value("devices").toArray()) {
        const auto d = entry.toObject(); const auto prefix = "pam.prototype." + d.value("token").toString();
        for (const auto &n : all) {
            const auto name = props(n).value("node.name").toString();
            const auto endpoint = name == prefix || name == prefix + ".sink" ? "sink" : name == prefix + ".source" ? "source" : "";
            if (!*endpoint) continue;
            const auto controls = d.value("controls").toObject().value(endpoint).toObject();
            for (auto c = controls.begin(); c != controls.end(); ++c) {
                const auto applied = serial(n) + ":" + c.key();
                if (mixerApplied_.contains(applied)) continue;
                const auto available = c.key() == "volume" ? "volumeAvailable" : "muteAvailable";
                if (!n.value("mixer").toObject().value(available).toBool()) continue;
                try {
                    if (c.key() == "volume") graph_.setVolume(serial(n), c.value().toDouble());
                    else graph_.setMute(serial(n), c.value().toBool());
                    mixerApplied_.insert(applied);
                } catch (const std::exception &e) { emit operationFailed(QString::fromUtf8(e.what())); }
            }
        }
    }
    reconcileRecording();
    emit changed();
}
void Configuration::mixer(const QString &key, const QString &property, const QJsonValue &value) {
    const auto n = node(key); const auto name = props(n).value("node.name").toString();
    if (property == "volume" && (!value.isDouble() || !std::isfinite(value.toDouble()) || value.toDouble() < 0 || value.toDouble() > 1)) invalid("Invalid volume");
    const auto capability = property == "volume" ? "volumeAvailable" : "muteAvailable";
    if (!n.value("mixer").toObject().value(capability).toBool()) invalid("Endpoint mixer control is unavailable");
    auto data = data_; auto devices = data.value("devices").toArray();
    for (int i = 0; i < devices.size(); ++i) {
        auto d = devices[i].toObject(); const auto prefix = "pam.prototype." + d.value("token").toString();
        const QString endpoint = name == prefix || name == prefix + ".sink" ? "sink" : name == prefix + ".source" ? "source" : "";
        if (endpoint.isEmpty()) continue;
        auto controls = d.value("controls").toObject(); auto values = controls.value(endpoint).toObject();
        values[property] = value; controls[endpoint] = values; d["controls"] = controls; devices[i] = d;
        data["devices"] = devices; commit(data); mixerApplied_.insert(key + ":" + property); break;
    }
    if (property == "volume") graph_.setVolume(key, value.toDouble()); else graph_.setMute(key, value.toBool());
}
void Configuration::setVolume(const QString &key, double gain) { mixer(key, "volume", gain); }
void Configuration::setMute(const QString &key, bool muted) { mixer(key, "muted", muted); }
QString Configuration::savedRoutes() const {
    QJsonArray routes;
    for (const auto &entry : data_.value("routes").toArray()) {
        auto r = entry.toObject(); const auto status = status_.value(r.value("id").toString());
        r["state"] = status.value("state").toString("Waiting"); r["reason"] = status.value("reason"); routes.append(r);
    }
    return QString::fromUtf8(QJsonDocument(routes).toJson(QJsonDocument::Compact));
}
QString Configuration::routingSnapshot() const {
    auto snapshot = QJsonDocument::fromJson(router_.snapshot().toUtf8()).object();
    snapshot["recording"] = recordingSnapshot();
    snapshot["savedRoutes"] = QJsonDocument::fromJson(savedRoutes().toUtf8()).array();
    snapshot["savedCount"] = data_.value("routes").toArray().size();
    snapshot["configurationPath"] = path_;
    return QString::fromUtf8(QJsonDocument(snapshot).toJson(QJsonDocument::Compact));
}
QString Configuration::ownedDevices() const {
    auto devices = QJsonDocument::fromJson(devices_.snapshot().toUtf8()).array();
    QSet<QString> present;
    for (int i = 0; i < devices.size(); ++i) {
        auto d = devices[i].toObject(); present.insert(d.value("token").toString()); d["persistent"] = false;
        for (const auto &saved : data_.value("devices").toArray()) if (saved.toObject().value("token") == d.value("token")) d["persistent"] = true;
        devices[i] = d;
    }
    for (const auto &saved : data_.value("devices").toArray()) {
        auto d = saved.toObject(); if (present.contains(d.value("token").toString())) continue;
        d["persistent"] = true; d["nodePrefix"] = "pam.prototype." + d.value("token").toString(); devices.append(d);
    }
    return QString::fromUtf8(QJsonDocument(devices).toJson(QJsonDocument::Compact));
}
QString Configuration::document() const { return QString::fromUtf8(QJsonDocument(data_).toJson(QJsonDocument::Compact)); }
