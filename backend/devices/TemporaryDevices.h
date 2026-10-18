#pragma once
#include <QString>
#include <map>
#include <memory>
#include <pipewire/pipewire.h>

struct pw_impl_module;
class Graph;

// Tokens refer only to objects allocated by this instance. No global-ID deletion API.
class TemporaryDevices final {
public:
    explicit TemporaryDevices(Graph &graph) : graph_(graph) {}
    ~TemporaryDevices();
    QString create(const QString &kind, const QString &label, const QString &token = {});
    bool destroy(const QString &token);
    QString snapshot() const;
private:
    struct Device {
        pw_proxy *proxy = nullptr;
        pw_impl_module *module = nullptr;
        spa_hook moduleHook{};
        QString kind;
        QString label;
    };
    Graph &graph_;
    std::map<QString, std::unique_ptr<Device>> owned_;
};
