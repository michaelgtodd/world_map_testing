#pragma once

#include <rocky/rocky.h>
#include <rocky/vsg/DisplayManager.h>

#include "Waypoint.h"
#include "Geodesy.h"

using namespace ROCKY_NAMESPACE;

class RightClickHandler : public vsg::Inherit<vsg::Visitor, RightClickHandler>
{
public:
    rocky::Application* app = nullptr;
    FlightSettings* settings = nullptr;
    WaypointQueue* wpQueue = nullptr;

    void apply(vsg::ButtonPressEvent& event) override
    {
        if (event.button != 3 || !app)
            return;

        // Only capture press in Approach mode (need press point for hover location)
        if (settings->mode.load() != 1)
            return;

        pressX_ = event.x;
        pressY_ = event.y;
        pressed_ = true;

        auto result = rocky::pointAtWindowCoords(
            vsg::ref_ptr<vsg::Viewer>(app->viewer.get()),
            event.x, event.y);

        if (result.ok())
        {
            auto wgs84 = result.value().point.transform(SRS::WGS84);
            pressLon_ = wgs84.x;
            pressLat_ = wgs84.y;
            pressTerrainAlt_ = wgs84.z;
            pressValid_ = true;
        }
        else
        {
            pressValid_ = false;
        }
    }

    void apply(vsg::ButtonReleaseEvent& event) override
    {
        if (event.button != 3 || !app)
            return;

        int mode = settings->mode.load();

        if (mode == 0)
        {
            handleFlyTo(event);
        }
        else if (pressed_)
        {
            pressed_ = false;
            handleApproach(event);
        }
    }

private:
    bool pressed_ = false;
    int pressX_ = 0, pressY_ = 0;
    double pressLon_ = 0, pressLat_ = 0, pressTerrainAlt_ = 0;
    bool pressValid_ = false;

    entt::entity createPointMarker(entt::registry& r, double lon, double lat, double altM,
                                   Color color, float width)
    {
        auto entity = r.create();
        auto& geom = r.emplace<PointGeometry>(entity);
        geom.srs = SRS::WGS84;
        geom.points.emplace_back(lon, lat, altM);
        auto& style = r.emplace<PointStyle>(entity);
        style.color = color;
        style.width = width;
        style.antialias = 0.5f;
        style.depthOffset = 0.0f;
        r.emplace<Point>(entity, geom, style);
        return entity;
    }

    void handleFlyTo(vsg::ButtonReleaseEvent& event)
    {
        auto result = rocky::pointAtWindowCoords(
            vsg::ref_ptr<vsg::Viewer>(app->viewer.get()),
            event.x, event.y);

        if (!result.ok()) return;

        auto wgs84 = result.value().point.transform(SRS::WGS84);
        double lon = wgs84.x, lat = wgs84.y, terrainAlt = wgs84.z;
        double flightAlt = settings->altitude.load();
        bool isMSL = settings->isMSL.load();
        double speed = settings->speedKnots.load();
        int idx = settings->nextIndex.fetch_add(1);

        double markerAltMSL = isMSL ? flightAlt * geo::FT_TO_M : terrainAlt + flightAlt * geo::FT_TO_M;
        double altFtMSL = markerAltMSL * geo::M_TO_FT;
        entt::entity entity = entt::null;
        app->registry.write([&](entt::registry& r)
        {
            entity = createPointMarker(r, lon, lat, markerAltMSL,
                Color(0.2f, 0.6f, 1.0f, 1.0f), 10.0f);
            app->vsgcontext->requestFrame();
        });

        Waypoint wp{};
        wp.index = idx;
        wp.lon = lon; wp.lat = lat;
        wp.terrainAlt = terrainAlt;
        wp.flightAlt = flightAlt;
        wp.isMSL = isMSL;
        wp.speedKnots = speed;
        wp.type = WPType::FlyTo;
        wp.pointEntity = entity;

        wpQueue->push(wp);
        settings->prevAltFtMSL.store(altFtMSL);
    }

    void handleApproach(vsg::ButtonReleaseEvent& event)
    {
        if (!pressValid_) return;

        double hoverLon = pressLon_;
        double hoverLat = pressLat_;
        double hoverTerrainAlt = pressTerrainAlt_;

        double hoverAltFtMSL = hoverTerrainAlt * geo::M_TO_FT + 20.0;
        double hoverAltM = hoverAltFtMSL * geo::FT_TO_M;

        double dx = event.x - pressX_;
        double dy = event.y - pressY_;
        double dragDist = std::sqrt(dx * dx + dy * dy);

        double inboundBearing;
        if (dragDist > 15.0)
        {
            auto releaseResult = rocky::pointAtWindowCoords(
                vsg::ref_ptr<vsg::Viewer>(app->viewer.get()),
                event.x, event.y);

            if (releaseResult.ok())
            {
                auto releaseWgs84 = releaseResult.value().point.transform(SRS::WGS84);
                double dragBearing = geo::bearingDeg(hoverLat, hoverLon,
                                                releaseWgs84.y, releaseWgs84.x);
                inboundBearing = dragBearing;
            }
            else
            {
                double screenAngle = std::atan2(-dy, dx) * geo::RAD2DEG;
                inboundBearing = std::fmod(90.0 - screenAngle + 360.0, 360.0);
            }
        }
        else
        {
            inboundBearing = 0.0;
        }

        double prevAltFtMSL = settings->prevAltFtMSL.load();
        double altToLoseFt = prevAltFtMSL - hoverAltFtMSL;
        if (altToLoseFt < 100.0) altToLoseFt = 100.0;

        double approachDistFt = altToLoseFt / std::tan(geo::GLIDE_ANGLE_DEG * geo::DEG2RAD);
        double approachDistNm = approachDistFt / geo::NM_TO_FT;

        double recipBearing = std::fmod(inboundBearing + 180.0, 360.0);
        double fafLat, fafLon;
        geo::destPoint(hoverLat, hoverLon, recipBearing, approachDistNm, fafLat, fafLon);

        double speed = settings->speedKnots.load();
        int idx = settings->nextIndex.fetch_add(1);

        entt::entity entity = entt::null;
        app->registry.write([&](entt::registry& r)
        {
            // FAF marker (yellow)
            createPointMarker(r, fafLon, fafLat, prevAltFtMSL * geo::FT_TO_M,
                Color(1.0f, 0.8f, 0.0f, 1.0f), 8.0f);

            // Hover point marker (green)
            entity = createPointMarker(r, hoverLon, hoverLat, hoverAltM,
                Color(0.0f, 1.0f, 0.3f, 1.0f), 14.0f);

            app->vsgcontext->requestFrame();
        });

        Waypoint wp{};
        wp.index = idx;
        wp.lon = hoverLon; wp.lat = hoverLat;
        wp.terrainAlt = hoverTerrainAlt;
        wp.flightAlt = hoverAltFtMSL;
        wp.isMSL = true;
        wp.speedKnots = speed;
        wp.type = WPType::ApproachToHover;
        wp.inboundBearing = inboundBearing;
        wp.fafLon = fafLon; wp.fafLat = fafLat;
        wp.fafAltFt = prevAltFtMSL;
        wp.pointEntity = entity;

        wpQueue->push(wp);
        settings->prevAltFtMSL.store(hoverAltFtMSL);
    }
};
