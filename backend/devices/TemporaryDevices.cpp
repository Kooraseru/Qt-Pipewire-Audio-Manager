#include "TemporaryDevices.h"
#include "pipewire/Graph.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QUuid>
#include <pipewire/impl-module.h>
#include <stdexcept>

namespace {
QByteArray quote(const QString &text) {
    const auto array = QJsonDocument(QJsonArray{text}).toJson(QJsonDocument::Compact);
    return array.mid(1, array.size() - 2);
}
}

TemporaryDevices::~TemporaryDevices() {
    while (!owned_.empty()) destroy(owned_.begin()->first);
}

QString TemporaryDevices::create(const QString &kind, const QString &label, const QString &savedToken) {
    if (label.trimmed().isEmpty() || label.size() > 128 || label.contains(QChar::Null))
        throw std::runtime_error("Label must contain 1–128 characters and no NUL");
    if (kind != "sink" && kind != "cable")
        throw std::runtime_error("Supported kinds: sink, cable");
    const auto token = savedToken.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : savedToken;
    if (QUuid(token).isNull() || owned_.contains(token)) throw std::runtime_error("Invalid or duplicate device token");
    const auto name = ("pam.prototype." + token).toUtf8();
    auto device = std::make_unique<Device>();
    device->kind = kind;
    device->label = label;
    if (kind == "sink") {
        auto *props = pw_properties_new("factory.name", "support.null-audio-sink",
            "node.name", name.constData(), "node.description", label.toUtf8().constData(),
            "media.class", "Audio/Sink", "node.virtual", "true",
            "priority.session", "0", "node.autoconnect", "false",
            "object.linger", "false", "audio.position", "[ FL FR ]", nullptr);
        if (!props) throw std::runtime_error("Cannot allocate device properties");
        device->proxy = static_cast<pw_proxy *>(pw_core_create_object(graph_.core(),
            "adapter", PW_TYPE_INTERFACE_Node, PW_VERSION_NODE, &props->dict, 0));
        pw_properties_free(props);
        if (!device->proxy) throw std::runtime_error("Cannot request virtual sink");
    } else {
        // Both endpoints are devices, so neither side auto-connects to physical hardware.
        // This is a lifecycle experiment, not the selected per-route gain topology.
        const QByteArray shared = " node.virtual = true priority.session = 0 "
            "node.autoconnect = false object.linger = false audio.position = [ FL FR ] ";
        const QByteArray args = "{ audio.position = [ FL FR ] capture.props = { node.name = "
            + quote(QString::fromUtf8(name) + ".sink") + " node.description = "
            + quote(label + " Output") + " media.class = Audio/Sink " + shared
            + " } playback.props = { node.name = "
            + quote(QString::fromUtf8(name) + ".source") + " node.description = "
            + quote(label + " Input") + " media.class = Audio/Source " + shared + " } }";
        device->module = pw_context_load_module(graph_.context(),
            "libpipewire-module-loopback", args.constData(), nullptr);
        if (!device->module) throw std::runtime_error("Cannot load temporary cable module");
        static const pw_impl_module_events events = [] {
            pw_impl_module_events e{};
            e.version = PW_VERSION_IMPL_MODULE_EVENTS;
            e.destroy = [](void *data) {
                auto *owned = static_cast<Device *>(data);
                spa_hook_remove(&owned->moduleHook);
                owned->module = nullptr;
            };
            return e;
        }();
        pw_impl_module_add_listener(device->module, &device->moduleHook, &events, device.get());
    }
    owned_.emplace(token, std::move(device));
    return token;
}

bool TemporaryDevices::destroy(const QString &token) {
    const auto it = owned_.find(token);
    if (it == owned_.end()) return false;
    if (it->second->module) pw_impl_module_destroy(it->second->module);
    if (it->second->proxy) pw_proxy_destroy(it->second->proxy);
    owned_.erase(it);
    return true;
}

QString TemporaryDevices::snapshot() const {
    QJsonArray devices;
    for (const auto &[token, device] : owned_) {
        if (!device->proxy && !device->module) continue;
        devices.append(QJsonObject{{"token", token}, {"kind", device->kind},
            {"label", device->label}, {"nodePrefix", "pam.prototype." + token}});
    }
    return QString::fromUtf8(QJsonDocument(devices).toJson(QJsonDocument::Compact));
}
