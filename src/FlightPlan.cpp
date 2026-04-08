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
        {
            auto& geom = r.get<LineGeometry>(routeLineEntity_);
            geom.points.clear();
            geom.dirty(r);
        }

        app.vsgcontext->requestFrame();
    });
    waypoints_.clear();
    settings_.nextIndex.store(1);
    settings_.prevAltFtMSL.store(50.0);
    emit planCleared();
    emit planChanged();
}

void FlightPlan::initRouteLine(rocky::Application& app)
{
    app_ = &app;
    app.registry.write([&](entt::registry& r) {
        routeLineEntity_ = r.create();

        auto& geom = r.emplace<LineGeometry>(routeLineEntity_);
        geom.topology = LineTopology::Strip;
        geom.srs = SRS::WGS84;

        auto& style = r.emplace<LineStyle>(routeLineEntity_);
        style.color = Color(0.2f, 0.8f, 1.0f, 0.9f);
        style.width = 5.0f;
        style.depthOffset = 0.0f;

        r.emplace<Line>(routeLineEntity_, geom, style);
    });
}

void FlightPlan::updateRouteLine(rocky::Application& app)
{
    if (routeLineEntity_ == entt::null) return;

    app.registry.write([&](entt::registry& r)
    {
        if (!r.valid(routeLineEntity_)) return;

        auto& geom = r.get<LineGeometry>(routeLineEntity_);
        // Use recycle to properly reset the geometry for reuse
        geom.recycle(r);

        if (waypoints_.size() < 2)
        {
            app.vsgcontext->requestFrame();
            return;
        }

        constexpr double MAX_SEG_NM = 50.0;

        for (size_t i = 0; i < waypoints_.size(); i++)
        {
            auto& cur = waypoints_[i];

            if (cur.type == WPType::ApproachToHover && i > 0)
            {
                auto& prev = waypoints_[i - 1];
                double prevAlt = getWpAltMSL(prev);
                double fafAlt = std::max(cur.fafAltFt * geo::FT_TO_M, 1.0);
                double hoverAlt = getWpAltMSL(cur);

                geo::interpolateGreatCircle(prev.lat, prev.lon, prevAlt,
                    cur.fafLat, cur.fafLon, fafAlt, MAX_SEG_NM, geom.points);
                geo::interpolateGreatCircle(cur.fafLat, cur.fafLon, fafAlt,
                    cur.lat, cur.lon, hoverAlt, 5.0, geom.points);
            }
            else if (i > 0)
            {
                auto& prev = waypoints_[i - 1];
                double prevAlt = getWpAltMSL(prev);
                double curAlt = getWpAltMSL(cur);

                geo::interpolateGreatCircle(prev.lat, prev.lon, prevAlt,
                    cur.lat, cur.lon, curAlt, MAX_SEG_NM, geom.points);
            }
            else
            {
                geom.points.emplace_back(cur.lon, cur.lat, getWpAltMSL(cur));
            }
        }

        geom.dirty(r);
        app.vsgcontext->requestFrame();
    });
}

double FlightPlan::getWpAltMSL(const Waypoint& wp) const
{
    double alt;
    if (wp.type == WPType::ApproachToHover)
        alt = wp.flightAlt * geo::FT_TO_M;
    else if (wp.isMSL)
        alt = wp.flightAlt * geo::FT_TO_M;
    else
        alt = wp.terrainAlt + wp.flightAlt * geo::FT_TO_M;

    return std::max(alt, 1.0);
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
