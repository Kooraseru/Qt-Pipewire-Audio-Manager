#pragma once

#include <QObject>
#include <QJsonObject>
#include <QMap>
#include <QSet>
#include <memory>
#include <map>
#include <pipewire/pipewire.h>
#include <pipewire/extensions/metadata.h>

class QSocketNotifier;

// All control callbacks run on the Qt thread; PipeWire owns any audio threads.
class Graph final : public QObject {
    Q_OBJECT
public:
    explicit Graph(QObject *parent = nullptr);
    ~Graph() override;
    void start();
    QString snapshot() const;
    void setVolume(const QString &serial, double gain);
    void setMute(const QString &serial, bool muted);
    bool recordingMetadataAvailable() const;
    QString recordingMetadataGeneration() const;
    QString recordingTarget(const QString &stream) const;
    void setRecordingTarget(const QString &stream, const QString &source);
    pw_context *context() const { return context_; }
    pw_core *core() const { return core_; }
signals:
    void changed(QString event);
    void ready();
    void failed(QString message);
    void operationFailed(QString message);
private:
    struct NodeBinding;
    struct MetadataBinding;
    static void global(void *, uint32_t, uint32_t, const char *, uint32_t, const spa_dict *);
    static void removed(void *, uint32_t);
    static void nodeInfo(void *, const pw_node_info *);
    static void nodeParam(void *, int, uint32_t, uint32_t, uint32_t, const spa_pod *);
    static void proxyRemoved(void *, uint32_t);
    static void nodeProxyRemoved(void *);
    static void done(void *, uint32_t, int);
    static void error(void *, uint32_t, int, int, const char *);
    void publish(uint32_t id, const QString &kind);
    pw_loop *loop_ = nullptr;
    pw_context *context_ = nullptr;
    pw_core *core_ = nullptr;
    pw_registry *registry_ = nullptr;
    spa_hook registryHook_{};
    spa_hook coreHook_{};
    QSocketNotifier *notifier_ = nullptr;
    int initialSync_ = -1;
    int bootstrapBarriers_ = 0;
    QMap<uint32_t, QJsonObject> objects_;
    std::map<uint32_t, std::unique_ptr<NodeBinding>> nodes_;
    std::map<uint32_t, std::unique_ptr<NodeBinding>> retiredNodes_;
    QSet<uint32_t> observationProxyIds_;
    std::map<uint32_t, std::unique_ptr<MetadataBinding>> metadata_, retiredMetadata_;
    static void metadataRemoved(void *);
    static int metadataProperty(void *, uint32_t, const char *, const char *, const char *);
    NodeBinding &controlNode(const QString &serial);
};
