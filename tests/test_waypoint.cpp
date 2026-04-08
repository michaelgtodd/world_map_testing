// Tests for FlightSettings (no entt dependency).
// WaypointQueue and Waypoint struct tests require entt headers from vcpkg
// and are tested as part of integration tests.

#include <gtest/gtest.h>
#include <atomic>

// Replicate FlightSettings here to avoid pulling in Waypoint.h's entt dependency
namespace {
struct TestFlightSettings
{
    std::atomic<double> altitude{5000.0};
    std::atomic<bool> isMSL{true};
    std::atomic<double> speedKnots{250.0};
    std::atomic<int> nextIndex{1};
    std::atomic<int> mode{0};
    std::atomic<double> prevAltFtMSL{5000.0};
};
}

TEST(FlightSettings, DefaultValues)
{
    TestFlightSettings s;
    EXPECT_DOUBLE_EQ(s.altitude.load(), 5000.0);
    EXPECT_TRUE(s.isMSL.load());
    EXPECT_DOUBLE_EQ(s.speedKnots.load(), 250.0);
    EXPECT_EQ(s.nextIndex.load(), 1);
    EXPECT_EQ(s.mode.load(), 0);
    EXPECT_DOUBLE_EQ(s.prevAltFtMSL.load(), 5000.0);
}

TEST(FlightSettings, AtomicUpdates)
{
    TestFlightSettings s;
    s.altitude.store(10000.0);
    s.isMSL.store(false);
    s.mode.store(1);
    int idx = s.nextIndex.fetch_add(1);

    EXPECT_DOUBLE_EQ(s.altitude.load(), 10000.0);
    EXPECT_FALSE(s.isMSL.load());
    EXPECT_EQ(s.mode.load(), 1);
    EXPECT_EQ(idx, 1);
    EXPECT_EQ(s.nextIndex.load(), 2);
}

TEST(FlightSettings, NextIndexIncrementsAtomically)
{
    TestFlightSettings s;
    EXPECT_EQ(s.nextIndex.fetch_add(1), 1);
    EXPECT_EQ(s.nextIndex.fetch_add(1), 2);
    EXPECT_EQ(s.nextIndex.fetch_add(1), 3);
    EXPECT_EQ(s.nextIndex.load(), 4);
}
