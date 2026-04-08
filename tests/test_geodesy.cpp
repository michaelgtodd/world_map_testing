#include <gtest/gtest.h>
#include "Geodesy.h"

using namespace geo;

// ── haversineNm ─────────────────────────────────────────────────────

TEST(Geodesy, HaversineSamePointIsZero)
{
    EXPECT_NEAR(haversineNm(40.0, -74.0, 40.0, -74.0), 0.0, 1e-10);
}

TEST(Geodesy, HaversineNewYorkToLondon)
{
    // JFK (40.6413, -73.7781) to Heathrow (51.4700, -0.4543)
    // Known distance ~2999 nm
    double dist = haversineNm(40.6413, -73.7781, 51.4700, -0.4543);
    EXPECT_NEAR(dist, 2999.0, 15.0); // within 15 nm
}

TEST(Geodesy, HaversineSymmetric)
{
    double d1 = haversineNm(10.0, 20.0, 30.0, 40.0);
    double d2 = haversineNm(30.0, 40.0, 10.0, 20.0);
    EXPECT_NEAR(d1, d2, 1e-10);
}

TEST(Geodesy, HaversineAntipodal)
{
    // Opposite sides of the earth ~10800 nm (half circumference)
    double dist = haversineNm(0.0, 0.0, 0.0, 180.0);
    EXPECT_NEAR(dist, 10800.0, 30.0);
}

TEST(Geodesy, HaversineShortDistance)
{
    // ~1 degree of latitude at equator = 60 nm
    double dist = haversineNm(0.0, 0.0, 1.0, 0.0);
    EXPECT_NEAR(dist, 60.0, 0.5);
}

// ── bearingDeg ──────────────────────────────────────────────────────

TEST(Geodesy, BearingDueNorth)
{
    double brg = bearingDeg(0.0, 0.0, 10.0, 0.0);
    EXPECT_NEAR(brg, 0.0, 0.1);
}

TEST(Geodesy, BearingDueEast)
{
    double brg = bearingDeg(0.0, 0.0, 0.0, 10.0);
    EXPECT_NEAR(brg, 90.0, 0.1);
}

TEST(Geodesy, BearingDueSouth)
{
    double brg = bearingDeg(10.0, 0.0, 0.0, 0.0);
    EXPECT_NEAR(brg, 180.0, 0.1);
}

TEST(Geodesy, BearingDueWest)
{
    double brg = bearingDeg(0.0, 10.0, 0.0, 0.0);
    EXPECT_NEAR(brg, 270.0, 0.1);
}

TEST(Geodesy, BearingResultRange)
{
    // Bearing should always be in [0, 360)
    double brg = bearingDeg(51.5, -0.1, 40.6, -73.8);
    EXPECT_GE(brg, 0.0);
    EXPECT_LT(brg, 360.0);
}

// ── destPoint ───────────────────────────────────────────────────────

TEST(Geodesy, DestPointZeroDistance)
{
    double lat, lon;
    destPoint(40.0, -74.0, 90.0, 0.0, lat, lon);
    EXPECT_NEAR(lat, 40.0, 1e-10);
    EXPECT_NEAR(lon, -74.0, 1e-10);
}

TEST(Geodesy, DestPointDueNorth)
{
    double lat, lon;
    destPoint(0.0, 0.0, 0.0, 60.0, lat, lon); // 60 nm north = ~1 degree
    EXPECT_NEAR(lat, 1.0, 0.02);
    EXPECT_NEAR(lon, 0.0, 0.02);
}

TEST(Geodesy, DestPointRoundTrip)
{
    // Go 100nm at bearing 45, then compute bearing and distance back
    double lat2, lon2;
    destPoint(35.0, -120.0, 45.0, 100.0, lat2, lon2);

    double dist = haversineNm(35.0, -120.0, lat2, lon2);
    EXPECT_NEAR(dist, 100.0, 0.1);

    double brg = bearingDeg(35.0, -120.0, lat2, lon2);
    EXPECT_NEAR(brg, 45.0, 0.5);
}

// ── interpolateGreatCircle ──────────────────────────────────────────

TEST(Geodesy, InterpolateEndpointMatchesDestination)
{
    std::vector<glm::dvec3> pts;
    interpolateGreatCircle(0.0, 0.0, 1000.0, 10.0, 10.0, 2000.0, 50.0, pts);

    ASSERT_FALSE(pts.empty());
    auto& last = pts.back();
    // Output format is (lon, lat, alt)
    EXPECT_NEAR(last.x, 10.0, 1e-6); // lon
    EXPECT_NEAR(last.y, 10.0, 1e-6); // lat
    EXPECT_NEAR(last.z, 2000.0, 1e-6); // alt
}

TEST(Geodesy, InterpolateSegmentCount)
{
    std::vector<glm::dvec3> pts;
    // 600 nm distance with 50 nm segments = 12 segments = 12 points (excludes start)
    interpolateGreatCircle(0.0, 0.0, 0.0, 10.0, 0.0, 0.0, 50.0, pts);

    double dist = haversineNm(0.0, 0.0, 10.0, 0.0);
    int expectedSegments = static_cast<int>(std::ceil(dist / 50.0));
    EXPECT_EQ(static_cast<int>(pts.size()), expectedSegments);
}

TEST(Geodesy, InterpolateAltitudeLinear)
{
    std::vector<glm::dvec3> pts;
    // 1 degree lat = ~60nm, with 100nm segments = 1 segment
    interpolateGreatCircle(0.0, 0.0, 0.0, 1.0, 0.0, 1000.0, 100.0, pts);

    ASSERT_EQ(pts.size(), 1u);
    EXPECT_NEAR(pts[0].z, 1000.0, 1e-6);
}

TEST(Geodesy, InterpolateSamePointProducesOnePoint)
{
    std::vector<glm::dvec3> pts;
    interpolateGreatCircle(45.0, 90.0, 500.0, 45.0, 90.0, 500.0, 50.0, pts);

    ASSERT_EQ(pts.size(), 1u);
    EXPECT_NEAR(pts[0].x, 90.0, 1e-6);
    EXPECT_NEAR(pts[0].y, 45.0, 1e-6);
}

// ── Constants ───────────────────────────────────────────────────────

TEST(Geodesy, ConstantsConsistency)
{
    EXPECT_NEAR(FT_TO_M * M_TO_FT, 1.0, 1e-4);
    EXPECT_NEAR(DEG2RAD * RAD2DEG, 1.0, 1e-15);
}
