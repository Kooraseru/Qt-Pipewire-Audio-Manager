#pragma once
#include "devices/TemporaryDevices.h"
#include "routing/Router.h"
#include <QJsonArray>
#include <QLockFile>
#include <QSet>

// Desired configuration is separate from the client-owned live graph.
class Configuration final : public QObject {
    Q_OBJECT
public:
    Configuration(Graph &graph, TemporaryDevices &devices, Router &router);
    void start();
    QString create(const QString &kind, const QString &label);
    bool destroy(const QString &token);
    void setRoute(const QString &source, const QString &destination, bool enabled);
    void forgetRoute(const QString &id);
    void clearRoutes();
    void setVolume(const QString &serial, double gain);
    void setMute(const QString &serial, bool muted);
    void setRecordingSource(const QString &stream, const QString &source);
    void forgetRecording(const QString &id);
    QString routingSnapshot() const;
    QString ownedDevices() const;
    QString savedRoutes() const;
    QString path() const { return path_; }
    QString document() const;
signals:
    void changed();
    void operationFailed(QString message);
private:
    Graph &graph_;
    TemporaryDevices &devices_;
    Router &router_;
    QString path_;
    std::unique_ptr<QLockFile> lock_;
    QJsonObject data_;
    QTimer debounce_;
    QMap<QString, QJsonObject> status_;
    QMap<QString, QString> attempted_;
    QMap<QString, QPair<QString, QString>> live_;
    QSet<QString> mixerApplied_;
    QMap<QString, QString> inputAttempted_;
    QMap<QString, QJsonObject> inputStatus_;
    void reconcileRecording();
    QJsonObject recordingSnapshot() const;
    QList<QJsonObject> nodes() const;
    QJsonObject node(const QString &serial) const;
    QJsonObject selector(const QJsonObject &node) const;
    void commit(const QJsonObject &data);
    void schedule();
    void reconcile();
    void removeLive(const QString &id);
    void mixer(const QString &serial, const QString &property, const QJsonValue &value);
};
