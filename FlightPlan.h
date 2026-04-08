#pragma once

#include <QObject>
#include <QString>
#include <vector>
#include <entt/entt.hpp>

#include "Waypoint.h"

namespace rocky { class Application; }

class FlightPlan : public QObject
{
    Q_OBJECT
public:
    explicit FlightPlan(FlightSettings& settings, QObject* parent = nullptr);

    void addWaypoint(const Waypoint& wp);
    void clear(rocky::Application& app);
    void updateRouteLine(rocky::Application& app);
    QString formatText() const;

    const std::vector<Waypoint>& waypoints() const { return waypoints_; }
    int waypointCount() const { return static_cast<int>(waypoints_.size()); }
    FlightSettings& settings() { return settings_; }

signals:
    void waypointAdded(const Waypoint& wp);
    void planChanged();
    void planCleared();

private:
    double getWpAltMSL(const Waypoint& wp) const;

    std::vector<Waypoint> waypoints_;
    entt::entity routeLineEntity_ = entt::null;
    FlightSettings& settings_;
};
