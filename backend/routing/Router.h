#pragma once
#include "pipewire/Graph.h"
#include <QTimer>
#include <map>

class Router final : public QObject {
    Q_OBJECT
public:
    explicit Router(Graph &graph);
    ~Router() override;
    QString snapshot() const;
    QString routes() const;
    void setRoute(const QString &source, const QString &destination, bool enabled);
    void clear();
    QString rejection(const QString &source, const QString &destination) const;
signals:
    void changed();
private:
    struct Link;
    struct Route;
    struct Plan;
    Plan plan(const QString &source, const QString &destination) const;
    void update();
    void failed(const QString &key, const QString &token, const QString &message);
    void linkChanged(Link *link);
    Graph &graph_;
    QTimer debounce_;
    std::map<QString, std::unique_ptr<Route>> routes_;
};
