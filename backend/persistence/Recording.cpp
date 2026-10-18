#include "Configuration.h"
#include <QJsonDocument>
#include <QUuid>
#include <algorithm>
#include <stdexcept>

namespace {
QJsonObject properties(const QJsonObject &n) { return n.value("properties").toObject(); }
QString serial(const QJsonObject &n) { return properties(n).value("object.serial").toString(); }
QString title(const QJsonObject &n) {
    const auto p = properties(n);
    for (auto key : {"node.description", "application.name", "node.nick", "node.name"})
        if (!p.value(key).toString().isEmpty()) return p.value(key).toString();
    return "Microphone";
}
bool matches(const QJsonObject &n, const QJsonObject &s) {
    const auto p = properties(n);
    for (auto it = s.begin(); it != s.end(); ++it) if (p.value(it.key()) != it.value()) return false;
    return !s.isEmpty();
}
[[noreturn]] void reject(const QString &message) { throw std::runtime_error(message.toStdString()); }
}

void Configuration::setRecordingSource(const QString &stream, const QString &source) {
    const auto app = node(stream); const auto p = properties(app);
    if (p.value("media.class") != "Stream/Input/Audio" || p.value("node.virtual") == "true") reject("Choose a recording application stream");
    if (p.value("node.dont-move") == "true") reject("This recording stream does not allow moving");
    if (!graph_.recordingMetadataAvailable()) reject("WirePlumber default metadata is unavailable");
    const auto appSelector = selector(app);
    int count = 0;
    for (const auto &n : nodes()) count += matches(n, appSelector);
    if (count != 1) reject("Several recording streams share this identity; refusing an ambiguous assignment");
    auto data = data_; QJsonArray assignments; QString id;
    for (const auto &v : data.value("recordingAssignments").toArray()) {
        if (v.toObject().value("application").toObject() == appSelector) id = v.toObject().value("id").toString();
        else assignments.append(v);
    }
    if (!source.isEmpty()) {
        const auto mic = node(source);
        if (!properties(mic).value("media.class").toString().startsWith("Audio/Source")) reject("Choose a microphone source endpoint, not an output sink");
        const auto sourceSelector = selector(mic); count = 0;
        for (const auto &n : nodes()) count += matches(n, sourceSelector);
        if (count != 1) reject("Several sources share this identity");
        if (id.isEmpty()) id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        assignments.append(QJsonObject{{"id", id}, {"application", appSelector}, {"source", sourceSelector},
            {"applicationLabel", title(app)}, {"sourceLabel", title(mic)}});
    }
    data["recordingAssignments"] = assignments; commit(data);
    inputAttempted_.remove(id); inputStatus_.remove(id);
    if (source.isEmpty()) graph_.setRecordingTarget(stream, {});
    else reconcileRecording();
    emit changed();
}
void Configuration::forgetRecording(const QString &id) {
    auto data = data_; QJsonArray assignments; QJsonObject removed;
    for (const auto &v : data.value("recordingAssignments").toArray()) {
        if (v.toObject().value("id") == id) removed = v.toObject(); else assignments.append(v);
    }
    if (removed.isEmpty()) reject("Saved recording assignment not found");
    data["recordingAssignments"] = assignments; commit(data);
    inputAttempted_.remove(id); inputStatus_.remove(id);
    // Do not overwrite a later move made by another audio controller.
    const auto all = nodes();
    QList<QJsonObject> apps, mics;
    for (const auto &n : all) {
        if (matches(n, removed.value("application").toObject())) apps.append(n);
        if (matches(n, removed.value("source").toObject())) mics.append(n);
    }
    if (apps.size() == 1 && mics.size() == 1 && graph_.recordingTarget(serial(apps.first())) == serial(mics.first()))
        graph_.setRecordingTarget(serial(apps.first()), {});
    emit changed();
}
void Configuration::reconcileRecording() {
    const auto all = nodes();
    const auto objects = QJsonDocument::fromJson(graph_.snapshot().toUtf8()).object().value("objects").toArray();
    for (const auto &v : data_.value("recordingAssignments").toArray()) {
        const auto rule = v.toObject(); const auto id = rule.value("id").toString();
        QList<QJsonObject> applications, sources;
        for (const auto &n : all) {
            if (matches(n, rule.value("application").toObject())) applications.append(n);
            if (matches(n, rule.value("source").toObject())) sources.append(n);
        }
        QString state = "Waiting", reason;
        if (applications.size() != 1 || sources.size() != 1) {
            reason = applications.size() > 1 || sources.size() > 1 ? "Ambiguous recording application or source; waiting for a unique match" :
                applications.isEmpty() ? "Waiting for recording application" : "Saved microphone unavailable; WirePlumber may use another source";
        } else {
            const auto app = applications.first(), mic = sources.first();
            const auto stream = serial(app), source = serial(mic);
            const auto generation = stream + ":" + source + ":" + graph_.recordingMetadataGeneration();
            bool desiredLinked = false, otherLinked = false;
            for (const auto &o : objects) {
                const auto link = o.toObject(); const auto p = properties(link);
                if (link.value("type").toString().endsWith(":Link") && p.value("link.input.node").toString().toInt() == app.value("id").toInt()) {
                    if (p.value("link.output.node").toString().toInt() == mic.value("id").toInt()) desiredLinked = true;
                    else otherLinked = true;
                }
            }
            if (!graph_.recordingMetadataAvailable()) reason = "Waiting for writable WirePlumber metadata";
            else if (properties(app).value("node.dont-move") == "true") { state = "Blocked"; reason = "This stream does not allow moving"; }
            else {
                if (inputAttempted_.value(id) != generation) {
                    inputAttempted_[id] = generation;
                    try { graph_.setRecordingTarget(stream, source); }
                    catch (const std::exception &e) { inputStatus_[id] = {{"state", "Failed"}, {"reason", QString::fromUtf8(e.what())}}; continue; }
                }
                const auto target = graph_.recordingTarget(stream);
                if (desiredLinked && !otherLinked) state = "Connected";
                else if (desiredLinked && otherLinked) { state = "Multiple sources"; reason = "Additional input links remain; external patchbay links are not removed"; }
                else if (!target.isEmpty() && target != source && target != properties(mic).value("node.name").toString()) {
                    state = "Changed externally"; reason = "Another controller selected an input; reselect to retry";
                } else {
                    state = "Requested";
                    reason = otherLinked ? "Waiting for WirePlumber to replace the previous source" : "Waiting for WirePlumber input links";
                }
            }
        }
        inputStatus_[id] = {{"state", state}, {"reason", reason}};
    }
}
QJsonObject Configuration::recordingSnapshot() const {
    const auto all = nodes(); QJsonArray sources, streams, assignments;
    for (const auto &n : all) if (properties(n).value("media.class").toString().startsWith("Audio/Source"))
        sources.append(QJsonObject{{"key", serial(n)}, {"label", title(n)}});
    for (const auto &v : data_.value("recordingAssignments").toArray()) {
        auto rule = v.toObject(); const auto status = inputStatus_.value(rule.value("id").toString());
        rule["state"] = status.value("state").toString("Waiting"); rule["reason"] = status.value("reason"); assignments.append(rule);
    }
    for (const auto &app : all) {
        const auto p = properties(app);
        if (p.value("media.class") != "Stream/Input/Audio" || p.value("node.virtual") == "true") continue;
        QString selected, state = "No saved input", reason, id;
        for (const auto &v : assignments) {
            const auto rule = v.toObject();
            if (!matches(app, rule.value("application").toObject())) continue;
            id = rule.value("id").toString(); state = rule.value("state").toString(); reason = rule.value("reason").toString();
            QList<QString> candidates;
            for (const auto &n : all) if (matches(n, rule.value("source").toObject())) candidates.append(serial(n));
            if (candidates.size() == 1) selected = candidates.first();
            break;
        }
        bool editable = graph_.recordingMetadataAvailable() && p.value("node.dont-move") != "true";
        if (!graph_.recordingMetadataAvailable()) reason = "WirePlumber input selection is unavailable";
        else if (p.value("node.dont-move") == "true") reason = "This recording stream does not allow moving";
        try {
            const auto identity = selector(app); int count = 0;
            for (const auto &n : all) count += matches(n, identity);
            if (count != 1) { editable = false; reason = "Several recording streams share this identity; input selection is ambiguous"; }
        } catch (const std::exception &e) { editable = false; reason = QString::fromUtf8(e.what()); }
        streams.append(QJsonObject{{"key", serial(app)}, {"selected", selected}, {"assignment", id}, {"state", state}, {"reason", reason}, {"editable", editable}});
    }
    return {{"sources", sources}, {"streams", streams}, {"assignments", assignments}, {"available", graph_.recordingMetadataAvailable()}};
}
