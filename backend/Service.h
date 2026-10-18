#pragma once
#include "devices/TemporaryDevices.h"
#include "pipewire/Graph.h"
#include "routing/Router.h"
#include "persistence/Configuration.h"
#include <QDBusContext>
#include <stdexcept>

class Service final : public QObject, protected QDBusContext {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.plasmaaudiomanager.Prototype1")
public:
    Service(Graph &graph, TemporaryDevices &devices, Router &router, Configuration &configuration) : graph_(graph), devices_(devices), router_(router), configuration_(configuration) {
        connect(&graph_, &Graph::changed, this, &Service::GraphChanged);
        connect(&graph_, &Graph::operationFailed, this, &Service::OperationFailed);
        connect(&configuration_, &Configuration::changed, this, &Service::RoutesChanged);
        connect(&configuration_, &Configuration::operationFailed, this, &Service::OperationFailed);
    }
public slots:
    QString Snapshot() const { return graph_.snapshot(); }
    QString OwnedDevices() const { return configuration_.ownedDevices(); }
    QString Routes() const { return router_.routes(); }
    QString RoutingSnapshot() const { return configuration_.routingSnapshot(); }
    bool SetRoute(const QString &source, const QString &destination, bool enabled) {
        try { configuration_.setRoute(source, destination, enabled); return true; }
        catch (const std::exception &e) {
            sendErrorReply("org.plasmaaudiomanager.Prototype1.RouteError", e.what()); return false;
        }
    }
    bool ClearRoutes() {
        try { configuration_.clearRoutes(); return true; }
        catch (const std::exception &e) { sendErrorReply("org.plasmaaudiomanager.Prototype1.ConfigurationError", e.what()); return false; }
    }
    bool SetRecordingSource(const QString &stream, const QString &source) {
        try { configuration_.setRecordingSource(stream, source); return true; }
        catch (const std::exception &e) { sendErrorReply("org.plasmaaudiomanager.Prototype1.RecordingError", e.what()); return false; }
    }
    bool ForgetRecordingAssignment(const QString &id) {
        try { configuration_.forgetRecording(id); return true; }
        catch (const std::exception &e) { sendErrorReply("org.plasmaaudiomanager.Prototype1.RecordingError", e.what()); return false; }
    }
    QString SavedRoutes() const { return configuration_.savedRoutes(); }
    QString ConfigurationPath() const { return configuration_.path(); }
    QString SavedConfiguration() const { return configuration_.document(); }
    bool ForgetRoute(const QString &id) {
        try { configuration_.forgetRoute(id); return true; }
        catch (const std::exception &e) { sendErrorReply("org.plasmaaudiomanager.Prototype1.ConfigurationError", e.what()); return false; }
    }
    QString CreatePersistentDevice(const QString &kind, const QString &label) {
        try { return configuration_.create(kind, label); }
        catch (const std::exception &e) { sendErrorReply("org.plasmaaudiomanager.Prototype1.ConfigurationError", e.what()); return {}; }
    }
    QString CreateTemporaryDevice(const QString &kind, const QString &label) {
        try { return devices_.create(kind, label); }
        catch (const std::exception &e) {
            sendErrorReply("org.plasmaaudiomanager.Prototype1.InvalidRequest", e.what());
            return {};
        }
    }
    bool DestroyTemporaryDevice(const QString &token) {
        try { return configuration_.destroy(token); }
        catch (const std::exception &e) { sendErrorReply("org.plasmaaudiomanager.Prototype1.ConfigurationError", e.what()); return false; }
    }
    bool SetVolume(const QString &serial, double gain) {
        try { configuration_.setVolume(serial, gain); return true; }
        catch (const std::exception &e) {
            sendErrorReply("org.plasmaaudiomanager.Prototype1.ControlError", e.what()); return false;
        }
    }
    bool SetMute(const QString &serial, bool muted) {
        try { configuration_.setMute(serial, muted); return true; }
        catch (const std::exception &e) {
            sendErrorReply("org.plasmaaudiomanager.Prototype1.ControlError", e.what()); return false;
        }
    }
signals:
    void GraphChanged(QString event);
    void OperationFailed(QString message);
    void RoutesChanged();
private:
    Graph &graph_;
    TemporaryDevices &devices_;
    Router &router_;
    Configuration &configuration_;
};
