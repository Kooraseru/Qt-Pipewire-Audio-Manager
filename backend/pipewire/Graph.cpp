#include "Graph.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QSocketNotifier>
#include <algorithm>
#include <cstring>
#include <cerrno>
#include <QLoggingCategory>
#include <QVector>
#include <spa/param/props.h>
#include <spa/pod/builder.h>
#include <spa/pod/iter.h>
#include <cmath>
#include <stdexcept>

Q_LOGGING_CATEGORY(pamGraphLog, "pam.graph", QtWarningMsg)

namespace {
QJsonObject properties(const spa_dict *dict) {
    QJsonObject result;
    if (dict) {
        const spa_dict_item *item;
        spa_dict_for_each(item, dict)
            result.insert(QString::fromUtf8(item->key), QString::fromUtf8(item->value));
    }
    return result;
}
QString json(const QJsonObject &object) {
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}
}

struct Graph::NodeBinding {
    Graph *graph;
    uint32_t id;
    pw_node *proxy;
    spa_hook hook{};
    spa_hook proxyHook{};
    uint32_t localId = PW_ID_ANY;
    bool canWrite = false;
    bool propsWritable = false;
    bool subscribed = false;
    QVector<float> volumes;
    float volume = 1.0f;
    bool hasVolume = false;
    bool muted = false;
    bool hasMute = false;
    ~NodeBinding() {
        spa_hook_remove(&hook);
        spa_hook_remove(&proxyHook);
        pw_proxy_destroy(reinterpret_cast<pw_proxy *>(proxy));
    }
};

struct Graph::MetadataBinding {
    Graph *graph;
    uint32_t id, localId;
    pw_metadata *proxy;
    spa_hook hook{}, proxyHook{};
    bool writable = false;
    QMap<uint32_t, QString> targets;
    ~MetadataBinding() {
        spa_hook_remove(&hook); spa_hook_remove(&proxyHook);
        pw_proxy_destroy(reinterpret_cast<pw_proxy *>(proxy));
    }
};

Graph::Graph(QObject *parent) : QObject(parent) {}

void Graph::start() {
    if (loop_) throw std::runtime_error("Graph already started");
    loop_ = pw_loop_new(nullptr);
    if (!loop_) throw std::runtime_error("Cannot create PipeWire loop");
    pw_loop_enter(loop_);
    context_ = pw_context_new(loop_, nullptr, 0);
    if (!context_) throw std::runtime_error("Cannot create PipeWire context");
    core_ = pw_context_connect(context_, pw_properties_new(
        PW_KEY_APP_NAME, "Plasma Audio Manager", nullptr), 0);
    if (!core_) throw std::runtime_error("Cannot connect to PipeWire");
    static const pw_core_events coreEvents = [] {
        pw_core_events e{}; e.version = PW_VERSION_CORE_EVENTS;
        e.done = &Graph::done; e.error = &Graph::error;
        e.remove_id = &Graph::proxyRemoved; return e;
    }();
    pw_core_add_listener(core_, &coreHook_, &coreEvents, this);
    registry_ = pw_core_get_registry(core_, PW_VERSION_REGISTRY, 0);
    if (!registry_) throw std::runtime_error("Cannot obtain PipeWire registry");
    static const pw_registry_events registryEvents = {
        PW_VERSION_REGISTRY_EVENTS, &Graph::global, &Graph::removed
    };
    pw_registry_add_listener(registry_, &registryHook_, &registryEvents, this);
    initialSync_ = pw_core_sync(core_, PW_ID_CORE, 0);
    notifier_ = new QSocketNotifier(pw_loop_get_fd(loop_), QSocketNotifier::Read, this);
    connect(notifier_, &QSocketNotifier::activated, this, [this] {
        const int result = pw_loop_iterate(loop_, 0);
        if (result < 0) {
            notifier_->setEnabled(false);
            emit failed(QStringLiteral("PipeWire loop error: %1").arg(result));
        }
    });
}

Graph::~Graph() {
    delete notifier_;
    metadata_.clear(); retiredMetadata_.clear();
    nodes_.clear();
    retiredNodes_.clear();
    if (registry_) {
        spa_hook_remove(&registryHook_);
        pw_proxy_destroy(reinterpret_cast<pw_proxy *>(registry_));
    }
    if (core_) {
        spa_hook_remove(&coreHook_);
        pw_core_disconnect(core_);
    }
    if (context_) pw_context_destroy(context_);
    if (loop_) { pw_loop_leave(loop_); pw_loop_destroy(loop_); }
}

void Graph::global(void *data, uint32_t id, uint32_t permissions, const char *type,
                   uint32_t version, const spa_dict *props) {
    auto *self = static_cast<Graph *>(data);
    self->objects_[id] = {{"id", qint64(id)}, {"type", QString::fromUtf8(type)},
                         {"properties", properties(props)}};
    self->publish(id, "added");
    if (std::strcmp(type, PW_TYPE_INTERFACE_Metadata) == 0 &&
        self->objects_[id].value("properties").toObject().value("metadata.name") == "default") {
        auto *proxy = static_cast<pw_metadata *>(pw_registry_bind(self->registry_, id, type,
            std::min(version, uint32_t(PW_VERSION_METADATA)), 0));
        if (proxy) {
            auto binding = std::make_unique<MetadataBinding>();
            binding->graph = self; binding->id = id; binding->proxy = proxy;
            binding->localId = pw_proxy_get_id(reinterpret_cast<pw_proxy *>(proxy));
            binding->writable = (permissions & PW_PERM_W) && (permissions & PW_PERM_X);
            self->observationProxyIds_.insert(binding->localId);
            static const pw_proxy_events proxyEvents = [] { pw_proxy_events e{}; e.version = PW_VERSION_PROXY_EVENTS; e.removed = &Graph::metadataRemoved; return e; }();
            static const pw_metadata_events events = {PW_VERSION_METADATA_EVENTS, &Graph::metadataProperty};
            pw_proxy_add_listener(reinterpret_cast<pw_proxy *>(proxy), &binding->proxyHook, &proxyEvents, binding.get());
            pw_metadata_add_listener(proxy, &binding->hook, &events, binding.get());
            self->metadata_[id] = std::move(binding);
        }
    }
    if (std::strcmp(type, PW_TYPE_INTERFACE_Node) == 0) {
        auto *proxy = static_cast<pw_node *>(pw_registry_bind(self->registry_, id,
            type, std::min(version, uint32_t(PW_VERSION_NODE)), 0));
        if (!proxy) return;
        auto binding = std::make_unique<NodeBinding>();
        binding->graph = self; binding->id = id; binding->proxy = proxy;
        binding->canWrite = (permissions & PW_PERM_W) && (permissions & PW_PERM_X);
        binding->localId = pw_proxy_get_id(reinterpret_cast<pw_proxy *>(proxy));
        self->observationProxyIds_.insert(binding->localId);
        static const pw_proxy_events proxyEvents = [] {
            pw_proxy_events e{}; e.version = PW_VERSION_PROXY_EVENTS;
            e.removed = &Graph::nodeProxyRemoved; return e;
        }();
        pw_proxy_add_listener(reinterpret_cast<pw_proxy *>(proxy), &binding->proxyHook, &proxyEvents, binding.get());
        static const pw_node_events events = [] {
            pw_node_events e{}; e.version = PW_VERSION_NODE_EVENTS;
            e.info = &Graph::nodeInfo; e.param = &Graph::nodeParam; return e;
        }();
        pw_node_add_listener(proxy, &binding->hook, &events, binding.get());
        self->nodes_.emplace(id, std::move(binding));
    }
}

void Graph::nodeInfo(void *data, const pw_node_info *info) {
    auto *binding = static_cast<NodeBinding *>(data);
    const auto active = binding->graph->nodes_.find(binding->id);
    if (active == binding->graph->nodes_.end() || active->second.get() != binding) return;
    auto &object = binding->graph->objects_[binding->id];
    if (info->change_mask & PW_NODE_CHANGE_MASK_PROPS) {
        auto props = object.value("properties").toObject();
        const auto updates = properties(info->props);
        for (auto it = updates.begin(); it != updates.end(); ++it) props[it.key()] = it.value();
        object["properties"] = props;
    }
    object["state"] = QString::fromUtf8(pw_node_state_as_string(info->state));
    if (info->change_mask & PW_NODE_CHANGE_MASK_PARAMS) {
        for (uint32_t i = 0; i < info->n_params; ++i) {
            if (info->params[i].id != SPA_PARAM_Props) continue;
            binding->propsWritable = info->params[i].flags & SPA_PARAM_INFO_WRITE;
            if (!binding->subscribed && (info->params[i].flags & SPA_PARAM_INFO_READ)) {
                const auto media = object.value("properties").toObject().value("media.class").toString();
                if (media.contains("Audio")) {
                    uint32_t param = SPA_PARAM_Props;
                    pw_node_subscribe_params(binding->proxy, &param, 1);
                    binding->subscribed = true;
                }
            }
        }
    }
    binding->graph->publish(binding->id, "updated");
}

void Graph::nodeParam(void *data, int, uint32_t id, uint32_t, uint32_t, const spa_pod *pod) {
    if (id != SPA_PARAM_Props || !pod || !spa_pod_is_object(pod)) return;
    auto *binding = static_cast<NodeBinding *>(data);
    const auto active = binding->graph->nodes_.find(binding->id);
    if (active == binding->graph->nodes_.end() || active->second.get() != binding) return;
    if (const auto *prop = spa_pod_find_prop(pod, nullptr, SPA_PROP_channelVolumes)) {
        float values[64];
        const auto count = spa_pod_copy_array(&prop->value, SPA_TYPE_Float, values, 64);
        if (count > 0) {
            binding->volumes = QVector<float>(values, values + count);
            binding->volume = *std::max_element(values, values + count);
            binding->hasVolume = std::isfinite(binding->volume) && binding->volume >= 0;
        }
    }
    if (binding->volumes.isEmpty()) {
        if (const auto *prop = spa_pod_find_prop(pod, nullptr, SPA_PROP_volume)) {
            float value;
            if (spa_pod_get_float(&prop->value, &value) == 0 && std::isfinite(value) && value >= 0) {
                binding->volume = value; binding->hasVolume = true;
            }
        }
    }
    if (const auto *prop = spa_pod_find_prop(pod, nullptr, SPA_PROP_mute)) {
        bool value;
        if (spa_pod_get_bool(&prop->value, &value) == 0) {
            binding->muted = value; binding->hasMute = true;
        }
    }
    QJsonArray channels;
    for (float value : binding->volumes) channels.append(value);
    const QJsonObject mixer{{"volume", binding->volume}, {"muted", binding->muted},
        {"volumeAvailable", binding->hasVolume && binding->propsWritable && binding->canWrite},
        {"muteAvailable", binding->hasMute && binding->propsWritable && binding->canWrite},
        {"channels", channels}};
    auto &object = binding->graph->objects_[binding->id];
    if (object.value("mixer").toObject() == mixer) return;
    object["mixer"] = mixer;
    binding->graph->publish(binding->id, "updated");
}

Graph::NodeBinding &Graph::controlNode(const QString &serial) {
    if (serial.isEmpty()) throw std::runtime_error("Missing endpoint identity");
    for (auto &[id, binding] : nodes_) {
        if (objects_.value(id).value("properties").toObject().value("object.serial").toString() == serial) {
            if (!binding->canWrite || !binding->propsWritable)
                throw std::runtime_error("This audio endpoint is read only");
            return *binding;
        }
    }
    throw std::runtime_error("The audio endpoint disappeared");
}

void Graph::setVolume(const QString &serial, double gain) {
    if (!std::isfinite(gain) || gain < 0 || gain > 1)
        throw std::runtime_error("Volume gain must be between zero and one");
    auto &node = controlNode(serial);
    if (!node.hasVolume) throw std::runtime_error("Volume is not available on this endpoint");
    uint8_t buffer[1024];
    spa_pod_builder builder = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    spa_pod *pod;
    if (!node.volumes.isEmpty()) {
        auto channels = node.volumes;
        const float peak = *std::max_element(channels.begin(), channels.end());
        for (auto &channel : channels) channel = peak > 0 ? channel * gain / peak : gain;
        pod = static_cast<spa_pod *>(spa_pod_builder_add_object(&builder,
            SPA_TYPE_OBJECT_Props, SPA_PARAM_Props, SPA_PROP_channelVolumes,
            SPA_POD_Array(sizeof(float), SPA_TYPE_Float, channels.size(), channels.constData())));
    } else {
        pod = static_cast<spa_pod *>(spa_pod_builder_add_object(&builder,
            SPA_TYPE_OBJECT_Props, SPA_PARAM_Props, SPA_PROP_volume, SPA_POD_Float(gain)));
    }
    const int result = pw_node_set_param(node.proxy, SPA_PARAM_Props, 0, pod);
    if (result < 0) throw std::runtime_error(std::strerror(-result));
}

void Graph::setMute(const QString &serial, bool muted) {
    auto &node = controlNode(serial);
    if (!node.hasMute) throw std::runtime_error("Mute is not available on this endpoint");
    uint8_t buffer[128];
    spa_pod_builder builder = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    const auto *pod = static_cast<spa_pod *>(spa_pod_builder_add_object(&builder,
        SPA_TYPE_OBJECT_Props, SPA_PARAM_Props, SPA_PROP_mute, SPA_POD_Bool(muted)));
    const int result = pw_node_set_param(node.proxy, SPA_PARAM_Props, 0, pod);
    if (result < 0) throw std::runtime_error(std::strerror(-result));
}

void Graph::proxyRemoved(void *data, uint32_t id) {
    static_cast<Graph *>(data)->observationProxyIds_.remove(id);
}

void Graph::removed(void *data, uint32_t id) {
    auto *self = static_cast<Graph *>(data);
    if (self->objects_.contains(id)) self->publish(id, "removed");
    const auto node = self->nodes_.find(id);
    if (node != self->nodes_.end()) {
        // Global disappearance precedes the proxy's remove_id acknowledgement.
        // Destroying an unbound proxy here sends a destroy for a resource that
        // never existed. Retain it until PipeWire marks that proxy removed.
        const auto localId = node->second->localId;
        self->retiredNodes_.emplace(localId, std::move(node->second));
        self->nodes_.erase(node);
    }
    const auto meta = self->metadata_.find(id);
    if (meta != self->metadata_.end()) {
        self->retiredMetadata_.emplace(meta->second->localId, std::move(meta->second));
        self->metadata_.erase(meta);
    }
    for (auto &[unused, binding] : self->metadata_) { Q_UNUSED(unused); binding->targets.remove(id); }
    self->objects_.remove(id);
}

void Graph::nodeProxyRemoved(void *data) {
    auto *binding = static_cast<NodeBinding *>(data);
    auto *graph = binding->graph;
    const auto localId = binding->localId;
    const auto active = graph->nodes_.find(binding->id);
    if (active != graph->nodes_.end() && active->second.get() == binding)
        removed(graph, binding->id);
    graph->retiredNodes_.erase(localId);
}

void Graph::publish(uint32_t id, const QString &kind) {
    emit changed(json({{"event", kind}, {"object", objects_.value(id)}}));
}

QString Graph::snapshot() const {
    QJsonArray objects;
    for (const auto &object : objects_) objects.append(object);
    return json({{"schemaVersion", 1}, {"objects", objects}});
}

void Graph::done(void *data, uint32_t id, int seq) {
    auto *self = static_cast<Graph *>(data);
    if (id != PW_ID_CORE || seq != self->initialSync_) return;
    if (self->bootstrapBarriers_++ < 2) {
        // Registry callbacks issue binds, whose info callbacks subscribe to Props.
        // Include both dependent rounds before the initial snapshot is ready.
        self->initialSync_ = pw_core_sync(self->core_, PW_ID_CORE, seq);
    } else {
        emit self->ready();
    }
}

void Graph::error(void *data, uint32_t id, int, int result, const char *message) {
    auto *self = static_cast<Graph *>(data);
    // Registry globals can vanish after announcement and before bind reaches the
    // server. That request fails on the registry proxy, not the disappearing node.
    // Removal events still reconcile our snapshot; this is not a lost connection.
    auto *proxy = pw_core_find_proxy(self->core_, id);
    if ((result == -ESTALE || result == -ENOENT) &&
        (self->observationProxyIds_.contains(id) || (proxy &&
        std::strcmp(pw_proxy_get_type(proxy, nullptr), PW_TYPE_INTERFACE_Registry) == 0))) {
        qCDebug(pamGraphLog)
            << "PipeWire endpoint disappeared before registry bind:" << message;
        return;
    }
    const auto error = QStringLiteral("PipeWire object %1: %2 (%3)")
        .arg(id).arg(QString::fromUtf8(message)).arg(result);
    if (id == PW_ID_CORE && result != -ENOENT && result != -ESTALE) emit self->failed(error);
    else emit self->operationFailed(error);
}

void Graph::metadataRemoved(void *data) {
    auto *binding = static_cast<MetadataBinding *>(data);
    auto *graph = binding->graph; const auto local = binding->localId;
    const auto active = graph->metadata_.find(binding->id);
    if (active != graph->metadata_.end() && active->second.get() == binding) removed(graph, binding->id);
    graph->retiredMetadata_.erase(local);
}
int Graph::metadataProperty(void *data, uint32_t subject, const char *key, const char *, const char *value) {
    auto *binding = static_cast<MetadataBinding *>(data);
    const auto active = binding->graph->metadata_.find(binding->id);
    if (active == binding->graph->metadata_.end() || active->second.get() != binding) return 0;
    if (!key) { if (subject == SPA_ID_INVALID) binding->targets.clear(); else binding->targets.remove(subject); }
    else if (std::strcmp(key, "target.object") != 0) return 0;
    else if (value) binding->targets[subject] = QString::fromUtf8(value);
    else binding->targets.remove(subject);
    binding->graph->publish(binding->id, "metadata");
    return 0;
}
bool Graph::recordingMetadataAvailable() const { return metadata_.size() == 1 && metadata_.begin()->second->writable; }
QString Graph::recordingMetadataGeneration() const {
    return metadata_.size() == 1 ? objects_.value(metadata_.begin()->first).value("properties").toObject().value("object.serial").toString() : QString();
}
QString Graph::recordingTarget(const QString &stream) const {
    if (metadata_.size() != 1) return {};
    for (const auto &[id, binding] : nodes_) {
        Q_UNUSED(binding);
        if (objects_.value(id).value("properties").toObject().value("object.serial") == stream)
            return metadata_.begin()->second->targets.value(id);
    }
    return {};
}
void Graph::setRecordingTarget(const QString &stream, const QString &source) {
    if (!recordingMetadataAvailable()) throw std::runtime_error("Writable default metadata is unavailable; WirePlumber is required to move recording streams");
    uint32_t id = SPA_ID_INVALID; bool found = source.isEmpty();
    for (const auto &[nodeId, unused] : nodes_) {
        Q_UNUSED(unused);
        const auto p = objects_.value(nodeId).value("properties").toObject();
        if (p.value("object.serial") == stream && p.value("media.class") == "Stream/Input/Audio" && p.value("node.virtual") != "true") {
            if (p.value("node.dont-move") == "true") throw std::runtime_error("This recording stream does not allow moving");
            id = nodeId;
        }
        if (!source.isEmpty() && p.value("object.serial") == source && p.value("media.class").toString().startsWith("Audio/Source")) found = true;
    }
    if (id == SPA_ID_INVALID || !found) throw std::runtime_error("Recording stream or microphone disappeared");
    const auto value = source.isEmpty() ? QByteArray("-1") : source.toUtf8();
    const int result = pw_metadata_set_property(metadata_.begin()->second->proxy, id, "target.object", "Spa:Id", value.constData());
    if (result < 0) throw std::runtime_error(std::strerror(-result));
}
