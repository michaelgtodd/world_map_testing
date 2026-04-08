#include "FlightPlan.h"
#include "Geodesy.h"

#include <rocky/rocky.h>
#include <rocky/vsg/DisplayManager.h>

using namespace ROCKY_NAMESPACE;

FlightPlan::FlightPlan(FlightSettings& settings, QObject* parent)
    : QObject(parent), settings_(settings)
{
}

void FlightPlan::addWaypoint(const Waypoint& wp)
{
    waypoints_.push_back(wp);
    emit waypointAdded(wp);
    emit planChanged();
}

void FlightPlan::clear(rocky::Application& app)
{
    app.registry.write([&](entt::registry& r) {
        for (auto& wp : waypoints_)
            if (wp.pointEntity != entt::null && r.valid(wp.pointEntity))
                r.destroy(wp.pointEntity);
        if (routeLineEntity_ != entt::null && r.valid(routeLineEntity_))
            r.destroy(routeLineEntity_);
        routeLineEntity_ = entt::null;
        app.vsgcontext->requestFrame();
    });
    waypoints_.clear();
    settings_.nextIndex.store(1);
    settings_.prevAltFtMSL.store(50.0);
    emit planCleared();
    emit planChanged();
}

double FlightPlan::getWpAltMSL(const Waypoint& wp) const
{
    if (wp.type == WPType::ApproachToHover)
        return wp.flightAlt * geo::FT_TO_M; // already MSL
    return wp.isMSL ? wp.flightAlt * geo::FT_TO_M
                    : wp.terrainAlt + wp.flightAlt * geo::FT_TO_M;
}

void FlightPlan::updateRouteLine(rocky::Application& app)
{
    app.registry.write([&](entt::registry& r)
    {
        if (routeLineEntity_ != entt::null && r.valid(routeLineEntity_))
            r.destroy(routeLineEntity_);
        routeLineEntity_ = entt::null;

        if (waypoints_.size() < 2) return;

        routeLineEntity_ = r.create();
        auto& geom = r.emplace<LineGeometry>(routeLineEntity_);
        geom.topology = LineTopology::Strip;
        geom.srs = SRS::WGS84;

        constexpr double MAX_SEG_NM = 50.0;

        auto& wp0 = waypoints_[0];
        geom.points.emplace_back(wp0.lon, wp0.lat, getWpAltMSL(wp0));

        for (size_t i = 1; i < waypoints_.size(); i++)
        {
            auto& prev = waypoints_[i - 1];
            auto& cur = waypoints_[i];

            double prevAlt = getWpAltMSL(prev);
            double prevLat = prev.lat, prevLon = prev.lon;

            if (cur.type == WPType::ApproachToHover)
            {
                double fafAlt = cur.fafAltFt * geo::FT_TO_M;
                geo::interpolateGreatCircle(prevLat, prevLon, prevAlt,
                    cur.fafLat, cur.fafLon, fafAlt, MAX_SEG_NM, geom.points);

                double hoverAlt = getWpAltMSL(cur);
                geo::interpolateGreatCircle(cur.fafLat, cur.fafLon, fafAlt,
                    cur.lat, cur.lon, hoverAlt, 5.0, geom.points);
            }
            else
            {
                double curAlt = getWpAltMSL(cur);
                geo::interpolateGreatCircle(prevLat, prevLon, prevAlt,
                    cur.lat, cur.lon, curAlt, MAX_SEG_NM, geom.points);
            }
        }

        auto& style = r.emplace<LineStyle>(routeLineEntity_);
        style.color = Color(0.2f, 0.8f, 1.0f, 0.9f);
        style.width = 5.0f;
        style.depthOffset = 100000.0f;

        r.emplace<Line>(routeLineEntity_, geom, style);
        app.vsgcontext->requestFrame();
    });
}

QString FlightPlan::formatText() const
{
    if (waypoints_.empty()) return "(empty flight plan)";

    QString text;
    text += QString("FLIGHT PLAN  (%1 waypoint%2)\n")
        .arg(waypoints_.size()).arg(waypoints_.size() > 1 ? "s" : "");
    text += QString("").fill('=', 72) + "\n\n";

    double totalDist = 0, totalTime = 0;
    QString deg = QString::fromUtf8("\xC2\xB0");

    for (size_t i = 0; i < waypoints_.size(); i++)
    {
        auto& wp = waypoints_[i];
        auto latDir = wp.lat >= 0 ? "N" : "S";
        auto lonDir = wp.lon >= 0 ? "E" : "W";

        if (wp.type == WPType::FlyTo)
        {
            text += QString("  WP%1  FLY TO  %2%3%4  %5%6%7\n")
                .arg(wp.index, 2, 10, QChar('0'))
                .arg(QString::number(std::abs(wp.lat), 'f', 5)).arg(latDir).arg(deg)
                .arg(QString::number(std::abs(wp.lon), 'f', 5)).arg(lonDir).arg(deg);
            text += QString("        ALT %1 ft %2   SPD %3 kts\n")
                .arg(QString::number(wp.flightAlt, 'f', 0))
                .arg(wp.isMSL ? "MSL" : "AGL")
                .arg(QString::number(wp.speedKnots, 'f', 0));
        }
        else
        {
            text += QString("  WP%1  APPROACH TO HOVER  %2%3%4  %5%6%7\n")
                .arg(wp.index, 2, 10, QChar('0'))
                .arg(QString::number(std::abs(wp.lat), 'f', 5)).arg(latDir).arg(deg)
                .arg(QString::number(std::abs(wp.lon), 'f', 5)).arg(lonDir).arg(deg);
            text += QString("        HOVER 20 ft AGL (%1 ft MSL)   SPD %2 kts\n")
                .arg(QString::number(wp.flightAlt, 'f', 0))
                .arg(QString::number(wp.speedKnots, 'f', 0));
            text += QString("        INBOUND %1%2   FAF %3 ft MSL   3%4 GS\n")
                .arg(QString::number(wp.inboundBearing, 'f', 0)).arg(deg)
                .arg(QString::number(wp.fafAltFt, 'f', 0)).arg(deg);

            double approachDist = geo::haversineNm(wp.fafLat, wp.fafLon, wp.lat, wp.lon);
            text += QString("        APPROACH LEG %1 nm\n")
                .arg(QString::number(approachDist, 'f', 1));
        }

        if (i > 0)
        {
            auto& prev = waypoints_[i - 1];
            double fromLat = prev.lat, fromLon = prev.lon;
            double toLat = wp.lat, toLon = wp.lon;

            if (wp.type == WPType::ApproachToHover)
            {
                toLat = wp.fafLat;
                toLon = wp.fafLon;
            }

            double dist = geo::haversineNm(fromLat, fromLon, toLat, toLon);
            double brg = geo::bearingDeg(fromLat, fromLon, toLat, toLon);
            if (wp.type == WPType::ApproachToHover)
                dist += geo::haversineNm(wp.fafLat, wp.fafLon, wp.lat, wp.lon);

            double legTime = (wp.speedKnots > 0) ? dist / wp.speedKnots : 0;
            totalDist += dist;
            totalTime += legTime;

            int mins = static_cast<int>(legTime * 60.0 + 0.5);
            text += QString("        LEG  %1 nm  BRG %2%3  ETE %4 min\n")
                .arg(QString::number(dist, 'f', 1))
                .arg(QString::number(brg, 'f', 0)).arg(deg)
                .arg(mins);
        }
        text += "\n";
    }

    text += QString("").fill('-', 72) + "\n";
    int totalMins = static_cast<int>(totalTime * 60.0 + 0.5);
    text += QString("  TOTAL DISTANCE: %1 nm\n").arg(QString::number(totalDist, 'f', 1));
    text += QString("  TOTAL ETE:      %1h %2m\n")
        .arg(totalMins / 60).arg(totalMins % 60, 2, 10, QChar('0'));
    text += QString("  WAYPOINTS:      %1\n").arg(waypoints_.size());

    return text;
}
