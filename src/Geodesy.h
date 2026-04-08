#pragma once

#include <cmath>
#include <vector>
#include <glm/glm.hpp>

namespace geo {

inline constexpr double DEG2RAD    = M_PI / 180.0;
inline constexpr double RAD2DEG    = 180.0 / M_PI;
inline constexpr double EARTH_R_NM = 3440.065;
inline constexpr double FT_TO_M    = 0.3048;
inline constexpr double M_TO_FT    = 3.28084;
inline constexpr double NM_TO_FT   = 6076.12;
inline constexpr double GLIDE_ANGLE_DEG = 3.0;

inline double haversineNm(double lat1, double lon1, double lat2, double lon2)
{
    double dLat = (lat2 - lat1) * DEG2RAD;
    double dLon = (lon2 - lon1) * DEG2RAD;
    double a = std::sin(dLat / 2) * std::sin(dLat / 2) +
               std::cos(lat1 * DEG2RAD) * std::cos(lat2 * DEG2RAD) *
               std::sin(dLon / 2) * std::sin(dLon / 2);
    return EARTH_R_NM * 2.0 * std::atan2(std::sqrt(a), std::sqrt(1.0 - a));
}

inline double bearingDeg(double lat1, double lon1, double lat2, double lon2)
{
    double dLon = (lon2 - lon1) * DEG2RAD;
    double la1 = lat1 * DEG2RAD, la2 = lat2 * DEG2RAD;
    double y = std::sin(dLon) * std::cos(la2);
    double x = std::cos(la1) * std::sin(la2) - std::sin(la1) * std::cos(la2) * std::cos(dLon);
    return std::fmod(std::atan2(y, x) * RAD2DEG + 360.0, 360.0);
}

inline void destPoint(double lat1, double lon1, double bearingDeg_, double distNm,
                      double& latOut, double& lonOut)
{
    double d = distNm / EARTH_R_NM;
    double brg = bearingDeg_ * DEG2RAD;
    double phi1 = lat1 * DEG2RAD, lam1 = lon1 * DEG2RAD;

    double phi2 = std::asin(std::sin(phi1) * std::cos(d) +
                            std::cos(phi1) * std::sin(d) * std::cos(brg));
    double lam2 = lam1 + std::atan2(std::sin(brg) * std::sin(d) * std::cos(phi1),
                                     std::cos(d) - std::sin(phi1) * std::sin(phi2));

    latOut = phi2 * RAD2DEG;
    lonOut = lam2 * RAD2DEG;
}

inline void interpolateGreatCircle(
    double lat1, double lon1, double alt1,
    double lat2, double lon2, double alt2,
    double maxSegmentNm,
    std::vector<glm::dvec3>& out)
{
    double distNm = haversineNm(lat1, lon1, lat2, lon2);
    int segments = std::max(1, static_cast<int>(std::ceil(distNm / maxSegmentNm)));

    double phi1 = lat1 * DEG2RAD, lam1 = lon1 * DEG2RAD;
    double phi2 = lat2 * DEG2RAD, lam2 = lon2 * DEG2RAD;

    double d = 2.0 * std::asin(std::sqrt(
        std::pow(std::sin((phi2 - phi1) / 2), 2) +
        std::cos(phi1) * std::cos(phi2) * std::pow(std::sin((lam2 - lam1) / 2), 2)));

    if (d < 1e-12)
    {
        out.emplace_back(lon2, lat2, alt2);
        return;
    }

    for (int i = 1; i <= segments; i++)
    {
        double f = static_cast<double>(i) / segments;
        double A = std::sin((1.0 - f) * d) / std::sin(d);
        double B = std::sin(f * d) / std::sin(d);

        double x = A * std::cos(phi1) * std::cos(lam1) + B * std::cos(phi2) * std::cos(lam2);
        double y = A * std::cos(phi1) * std::sin(lam1) + B * std::cos(phi2) * std::sin(lam2);
        double z = A * std::sin(phi1) + B * std::sin(phi2);

        double lat = std::atan2(z, std::sqrt(x * x + y * y)) * RAD2DEG;
        double lon = std::atan2(y, x) * RAD2DEG;
        double alt = alt1 + f * (alt2 - alt1);

        out.emplace_back(lon, lat, alt);
    }
}

} // namespace geo
