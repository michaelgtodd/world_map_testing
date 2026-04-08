#pragma once

#include <entt/entt.hpp>
#include <mutex>
#include <deque>
#include <atomic>

enum class WPType { FlyTo, ApproachToHover };

struct Waypoint
{
    int index;
    double lon, lat;
    double terrainAlt;     // ground elevation (meters)
    double flightAlt;      // altitude value (feet)
    bool isMSL;
    double speedKnots;
    WPType type = WPType::FlyTo;

    // Approach-to-hover specific
    double inboundBearing = 0;   // degrees true
    double fafLon = 0, fafLat = 0; // final approach fix position
    double fafAltFt = 0;        // FAF altitude (feet MSL)

    entt::entity pointEntity = entt::null;
};

class WaypointQueue
{
public:
    void push(const Waypoint& w)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push_back(w);
    }
    bool pop(Waypoint& w)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) return false;
        w = queue_.front();
        queue_.pop_front();
        return true;
    }
private:
    std::mutex mutex_;
    std::deque<Waypoint> queue_;
};

struct FlightSettings
{
    std::atomic<double> altitude{5000.0};
    std::atomic<bool> isMSL{true};
    std::atomic<double> speedKnots{250.0};
    std::atomic<int> nextIndex{1};
    std::atomic<int> mode{0}; // 0=FlyTo, 1=ApproachToHover
    // Previous waypoint altitude in feet MSL (for glideslope calc)
    std::atomic<double> prevAltFtMSL{5000.0};
};
