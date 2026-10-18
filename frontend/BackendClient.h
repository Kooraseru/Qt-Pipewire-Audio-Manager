#pragma once

#include <QObject>
#include <QJsonArray>
#include <QTimer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

// A D-Bus client only: the UI neither starts nor owns the audio backend.
class BackendClient final : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(AudioBackend)
    QML_SINGLETON
    Q_PROPERTY(bool connected READ connected NOTIFY statusChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY statusChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(QVariantList outputs READ outputs NOTIFY modelsChanged)
    Q_PROPERTY(QVariantList inputs READ inputs NOTIFY modelsChanged)
    Q_PROPERTY(QVariantList virtualDevices READ virtualDevices NOTIFY modelsChanged)
    Q_PROPERTY(QVariantList applications READ applications NOTIFY modelsChanged)
    Q_PROPERTY(QVariantMap routing READ routing NOTIFY modelsChanged)
    Q_PROPERTY(QVariantList routingSourceKeys READ routingSourceKeys NOTIFY routingEndpointsChanged)
    Q_PROPERTY(QVariantList routingDestinationKeys READ routingDestinationKeys NOTIFY routingEndpointsChanged)
public:
    explicit BackendClient(QObject *parent = nullptr);
    bool connected() const { return connected_; }
    bool busy() const { return busy_; }
    QString status() const { return status_; }
    QString error() const { return error_; }
    QVariantList outputs() const { return outputs_; }
    QVariantList inputs() const { return inputs_; }
    QVariantList virtualDevices() const { return virtualDevices_; }
    QVariantList applications() const { return applications_; }
    QVariantMap routing() const { return routing_; }
    QVariantList routingSourceKeys() const { return routingSourceKeys_; }
    QVariantList routingDestinationKeys() const { return routingDestinationKeys_; }
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void createDevice(const QString &kind, const QString &label);
    Q_INVOKABLE void destroyDevice(const QString &token);
    Q_INVOKABLE void clearError();
    Q_INVOKABLE void setVolume(const QString &serial, double percent);
    Q_INVOKABLE void setMute(const QString &serial, bool muted);
    Q_INVOKABLE void setRoute(const QString &source, const QString &destination, bool enabled);
    Q_INVOKABLE void clearRoutes();
    Q_INVOKABLE void forgetRoute(const QString &id);
    Q_INVOKABLE void setRecordingSource(const QString &stream, const QString &source);
    Q_INVOKABLE void forgetRecordingAssignment(const QString &id);
signals:
    void statusChanged();
    void errorChanged();
    void modelsChanged();
    void routingEndpointsChanged();
    void deviceCreated();
private slots:
    void graphChanged(const QString &event);
    void ownerChanged(const QString &, const QString &, const QString &owner);
    void backendOperationFailed(const QString &message);
    void routesChanged();
private:
    void rebuildModels();
    void setError(const QString &message);
    void scheduleRefresh();
    void control(const QString &method, const QVariantList &arguments);
    bool connected_ = false;
    bool busy_ = false;
    bool refreshing_ = false;
    bool refreshAgain_ = false;
    int generation_ = 0;
    QString owner_;
    QString status_ = QStringLiteral("Looking for backend");
    QString error_;
    QTimer debounce_;
    QJsonArray objects_;
    QJsonArray owned_;
    QVariantList outputs_, inputs_, virtualDevices_, applications_;
    QVariantMap routing_;
    QVariantList routingSourceKeys_, routingDestinationKeys_;
    void updateRoutingKeys();
};
