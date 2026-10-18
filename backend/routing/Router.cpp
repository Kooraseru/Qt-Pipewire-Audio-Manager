#include "Router.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QUuid>
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace {
QString keyFor(const QString &a, const QString &b) { return a + ":" + b; }
QJsonObject props(const QJsonObject &o) { return o.value("properties").toObject(); }
QString title(const QJsonObject &node) {
    const auto p = props(node);
    for (const auto *name : {"node.description", "application.name", "node.nick", "node.name"})
        if (!p.value(name).toString().isEmpty()) return p.value(name).toString();
    return "Unnamed endpoint";
}
bool sourceType(const QJsonObject &node) {
    const auto p = props(node);
    const auto media = p.value("media.class").toString();
    return media.startsWith("Audio/Source") || (media == "Stream/Output/Audio" && p.value("node.virtual").toString() != "true");
}
bool sinkType(const QJsonObject &node) { return props(node).value("media.class").toString().startsWith("Audio/Sink"); }
struct View {
    QMap<int, QJsonObject> nodes, ports;
    QList<QJsonObject> links;
    explicit View(const Graph &graph) {
        const auto objects = QJsonDocument::fromJson(graph.snapshot().toUtf8()).object().value("objects").toArray();
        for (const auto &entry : objects) {
            const auto o = entry.toObject();
            const int id = o.value("id").toInt();
            const auto type = o.value("type").toString();
            if (type.endsWith(":Node")) nodes[id] = o;
            else if (type.endsWith(":Port")) ports[id] = o;
            else if (type.endsWith(":Link")) links.append(o);
        }
    }
    int node(const QString &serial) const {
        if (!serial.isEmpty()) for (auto it = nodes.begin(); it != nodes.end(); ++it)
            if (props(it.value()).value("object.serial").toString() == serial) return it.key();
        return -1;
    }
    bool linked(int from, int to) const {
        for (const auto &link : links) {
            const auto p = props(link);
            if (p.value("link.output.node").toString().toInt() == from && p.value("link.input.node").toString().toInt() == to) return true;
        }
        return false;
    }
};
QString serial(const QJsonObject &o) { return props(o).value("object.serial").toString(); }
}

struct Router::Link {
    Router *router;
    QString key, token;
    pw_proxy *proxy = nullptr;
    spa_hook proxyHook{}, infoHook{};
    pw_link_state state = PW_LINK_STATE_INIT;
    ~Link() {
        spa_hook_remove(&proxyHook); spa_hook_remove(&infoHook);
        if (proxy) pw_proxy_destroy(proxy);
    }
};
struct Router::Route {
    QString source, destination, token, state = "Connecting", error;
    std::vector<std::unique_ptr<Link>> links;
};
struct Router::Plan {
    int source = -1, destination = -1;
    QList<QPair<int, int>> ports;
    QString reason;
    bool external = false;
};

Router::Router(Graph &graph) : graph_(graph) {
    debounce_.setInterval(60); debounce_.setSingleShot(true);
    connect(&graph_, &Graph::changed, this, [this] { if (!debounce_.isActive()) debounce_.start(); });
    connect(&debounce_, &QTimer::timeout, this, &Router::update);
}
Router::~Router() = default;

Router::Plan Router::plan(const QString &source, const QString &destination) const {
    const View view(graph_);
    Plan result;
    result.source = view.node(source); result.destination = view.node(destination);
    const auto reject = [&](const QString &message) { result.reason = message; return result; };
    if (result.source < 0 || result.destination < 0) return reject("An endpoint disappeared");
    if (!sourceType(view.nodes.value(result.source)) || !sinkType(view.nodes.value(result.destination)))
        return reject("Choose an audio source or playback stream and an output sink");
    result.external = view.linked(result.source, result.destination) && !routes_.contains(keyFor(source, destination));
    if (result.external) return reject("Existing route managed externally; Audio Manager will not remove it");
    QMap<int, QSet<int>> edges;
    for (const auto &link : view.links) {
        const auto p = props(link);
        edges[p.value("link.output.node").toString().toInt()].insert(p.value("link.input.node").toString().toInt());
    }
    for (const auto &[key, route] : routes_) {
        Q_UNUSED(key);
        if (route->state != "Failed") edges[view.node(route->source)].insert(view.node(route->destination));
    }
    // Include internal loopback/cable paths, which aren't external PipeWire links.
    for (auto input = view.nodes.begin(); input != view.nodes.end(); ++input) {
        const auto p = props(input.value());
        const auto media = p.value("media.class").toString();
        if (media != "Stream/Input/Audio" && !sinkType(input.value())) continue;
        const auto group = p.value("node.link-group").toString();
        if (!group.isEmpty()) for (auto output = view.nodes.begin(); output != view.nodes.end(); ++output)
            if (input.key() != output.key() && props(output.value()).value("node.link-group").toString() == group &&
                (props(output.value()).value("media.class").toString().startsWith("Audio/Source") ||
                 props(output.value()).value("media.class").toString() == "Stream/Output/Audio"))
                edges[input.key()].insert(output.key());
        if (p.value("stream.capture.sink").toString() == "true") {
            const auto target = p.value("target.object").toString();
            for (auto sink = view.nodes.begin(); sink != view.nodes.end(); ++sink)
                if (!target.isEmpty() && sinkType(sink.value()) && (target == serial(sink.value()) || target == props(sink.value()).value("node.name").toString()))
                    edges[sink.key()].insert(input.key());
        }
    }
    QSet<int> seen;
    QList<int> pending{result.destination};
    while (!pending.isEmpty()) {
        const int current = pending.takeLast();
        if (current == result.source) return reject("This connection would create a feedback loop");
        if (seen.contains(current)) continue;
        seen.insert(current);
        for (int next : edges.value(current)) pending.append(next);
    }
    QMap<QString, int> outputs, inputs;
    for (auto port = view.ports.begin(); port != view.ports.end(); ++port) {
        const auto p = props(port.value());
        const int node = p.value("node.id").toString().toInt();
        const auto direction = p.value("port.direction").toString();
        if (!((node == result.source && direction == "out") || (node == result.destination && direction == "in"))) continue;
        if (p.value("format.dsp").toString() != "32 bit float mono audio") return reject("This endpoint does not expose supported audio DSP ports");
        const auto channel = p.value("audio.channel").toString();
        if (channel.isEmpty()) return reject("Channel positions are not available yet");
        auto &channels = node == result.source ? outputs : inputs;
        if (channels.contains(channel)) return reject("Ambiguous channel layout");
        channels[channel] = port.key();
    }
    if (outputs.isEmpty() || inputs.isEmpty()) return reject("Waiting for audio ports");
    const bool monoToStereo = outputs.size() == 1 && outputs.contains("MONO") && inputs.size() == 2 && inputs.contains("FL") && inputs.contains("FR");
    if (!monoToStereo && outputs.keys() != inputs.keys()) return reject("Channel layouts differ; an explicit remix/downmix stage is required");
    for (auto input = inputs.begin(); input != inputs.end(); ++input)
        result.ports.append({outputs.value(monoToStereo ? "MONO" : input.key()), input.value()});
    return result;
}

void Router::setRoute(const QString &source, const QString &destination, bool enabled) {
    const auto key = keyFor(source, destination);
    auto existing = routes_.find(key);
    if (!enabled) {
        if (existing == routes_.end()) throw std::runtime_error("Only Audio Manager routes can be removed here");
        routes_.erase(existing); emit changed(); return;
    }
    if (existing != routes_.end() && existing->second->state != "Failed") return;
    if (existing != routes_.end()) routes_.erase(existing);
    const auto mapping = plan(source, destination);
    if (!mapping.reason.isEmpty()) throw std::runtime_error(mapping.reason.toStdString());
    auto route = std::make_unique<Route>();
    route->source = source; route->destination = destination;
    route->token = QUuid::createUuid().toString(QUuid::WithoutBraces);
    auto *state = route.get();
    routes_[key] = std::move(route);
    for (const auto &[output, input] : mapping.ports) {
        auto link = std::make_unique<Link>();
        link->router = this; link->key = key; link->token = state->token;
        auto *p = pw_properties_new("link.output.port", QByteArray::number(output).constData(),
            "link.input.port", QByteArray::number(input).constData(), "object.linger", "false", nullptr);
        link->proxy = static_cast<pw_proxy *>(pw_core_create_object(graph_.core(), "link-factory", PW_TYPE_INTERFACE_Link, PW_VERSION_LINK, &p->dict, 0));
        pw_properties_free(p);
        if (!link->proxy) { failed(key, state->token, "Cannot request a PipeWire link"); break; }
        static const pw_proxy_events events = [] {
            pw_proxy_events e{}; e.version = PW_VERSION_PROXY_EVENTS;
            e.error = [](void *data, int, int, const char *message) {
                auto *link = static_cast<Link *>(data);
                link->router->failed(link->key, link->token, QString::fromUtf8(message));
            };
            e.removed = [](void *data) {
                auto *link = static_cast<Link *>(data);
                link->router->failed(link->key, link->token, "A route link disappeared");
            };
            return e;
        }();
        static const pw_link_events linkEvents = [] {
            pw_link_events e{}; e.version = PW_VERSION_LINK_EVENTS;
            e.info = [](void *data, const pw_link_info *info) {
                auto *link = static_cast<Link *>(data);
                link->state = info->state;
                if (info->state == PW_LINK_STATE_ERROR) link->router->failed(link->key, link->token, QString::fromUtf8(info->error));
                else link->router->linkChanged(link);
            };
            return e;
        }();
        pw_proxy_add_listener(link->proxy, &link->proxyHook, &events, link.get());
        pw_link_add_listener(reinterpret_cast<pw_link *>(link->proxy), &link->infoHook, &linkEvents, link.get());
        state->links.push_back(std::move(link));
    }
    emit changed();
}

void Router::failed(const QString &key, const QString &token, const QString &message) {
    QTimer::singleShot(0, this, [this, key, token, message] {
        auto it = routes_.find(key);
        if (it == routes_.end() || it->second->token != token || it->second->state == "Failed") return;
        it->second->state = "Failed"; it->second->error = message;
        it->second->links.clear(); emit changed();
    });
}
void Router::linkChanged(Link *link) {
    auto it = routes_.find(link->key);
    if (it == routes_.end() || it->second->token != link->token || it->second->state == "Failed") return;
    bool connected = !it->second->links.empty();
    for (const auto &part : it->second->links)
        connected &= part->state == PW_LINK_STATE_PAUSED || part->state == PW_LINK_STATE_ACTIVE;
    const QString state = connected ? "Connected" : "Connecting";
    if (state != it->second->state) { it->second->state = state; emit changed(); }
}
void Router::clear() { routes_.clear(); emit changed(); }
void Router::update() {
    const View view(graph_);
    for (auto it = routes_.begin(); it != routes_.end();) {
        if (view.node(it->second->source) < 0 || view.node(it->second->destination) < 0) it = routes_.erase(it);
        else ++it;
    }
    emit changed();
}
QString Router::routes() const {
    QJsonArray array;
    for (const auto &[key, route] : routes_) array.append(QJsonObject{{"key", key}, {"source", route->source},
        {"destination", route->destination}, {"state", route->state}, {"error", route->error}});
    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}
QString Router::snapshot() const {
    const View view(graph_);
    QList<QJsonObject> sources, destinations;
    for (const auto &node : view.nodes) {
        const auto media = props(node).value("media.class").toString();
        QJsonObject entry{{"key", serial(node)}, {"label", title(node)},
            {"icon", media.startsWith("Audio/Source") ? "audio-input-microphone" : "audio-volume-high"}};
        if (sourceType(node)) sources.append(entry);
        else if (sinkType(node)) destinations.append(entry);
    }
    auto compare = [](const QJsonObject &a, const QJsonObject &b) { return QString::localeAwareCompare(a.value("label").toString(), b.value("label").toString()) < 0; };
    std::sort(sources.begin(), sources.end(), compare); std::sort(destinations.begin(), destinations.end(), compare);
    QJsonArray rows, columns;
    for (const auto &destination : destinations) columns.append(destination);
    for (auto source : sources) {
        QJsonArray cells;
        for (const auto &destination : destinations) {
            const auto a = source.value("key").toString(), b = destination.value("key").toString();
            const auto mapping = plan(a, b);
            const auto own = routes_.find(keyFor(a, b));
            const bool owned = own != routes_.end();
            const bool checked = owned ? own->second->state != "Failed" : mapping.external;
            cells.append(QJsonObject{{"source", a}, {"destination", b}, {"destinationLabel", destination.value("label")}, {"owned", owned},
                {"checked", checked}, {"editable", owned || mapping.reason.isEmpty()},
                {"reason", owned && !own->second->error.isEmpty() ? own->second->error : mapping.reason},
                {"state", owned ? own->second->state : mapping.external ? "External" : "Off"}});
        }
        source["cells"] = cells; rows.append(source);
    }
    return QString::fromUtf8(QJsonDocument(QJsonObject{{"sources", rows}, {"destinations", columns},
        {"ownedCount", int(routes_.size())}}).toJson(QJsonDocument::Compact));
}

QString Router::rejection(const QString &source, const QString &destination) const { return plan(source, destination).reason; }
