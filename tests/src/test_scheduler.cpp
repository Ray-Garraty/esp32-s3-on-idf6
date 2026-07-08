#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <limits>

#include "application/scheduler.hpp"

using namespace ecotiter::application;

TEST_CASE("TickScheduler: tick increments global tick", "[scheduler]") {
    gTick.store(0, std::memory_order_relaxed);
    TickScheduler sched;
    uint32_t before = gTick.load(std::memory_order_relaxed);
    sched.tick();
    uint32_t after = gTick.load(std::memory_order_relaxed);
    REQUIRE(after == before + 1);
}

TEST_CASE("TickScheduler: shouldBroadcast returns true every 200 ticks", "[scheduler]") {
    gTick.store(0, std::memory_order_relaxed);
    TickScheduler sched;
    // First broadcast fires at tick BROADCAST_INTERVAL (200) from 0
    for (int i = 0; i < 200; ++i) {
        REQUIRE(sched.shouldBroadcast() == false);
        sched.tick();
    }
    REQUIRE(sched.shouldBroadcast() == true);

    // Next broadcast at tick 400: 199 ticks false, then 200th tick true
    for (int i = 0; i < 199; ++i) {
        sched.tick();
        REQUIRE(sched.shouldBroadcast() == false);
    }
    sched.tick();
    REQUIRE(sched.shouldBroadcast() == true);
}

TEST_CASE("TickScheduler: shouldSample returns true every 10 ticks", "[scheduler]") {
    gTick.store(0, std::memory_order_relaxed);
    TickScheduler sched;
    for (int i = 0; i < 10; ++i) {
        REQUIRE(sched.shouldSample() == false);
        sched.tick();
    }
    REQUIRE(sched.shouldSample() == true);

    for (int i = 0; i < 9; ++i) {
        sched.tick();
        REQUIRE(sched.shouldSample() == false);
    }
    sched.tick();
    REQUIRE(sched.shouldSample() == true);
}

TEST_CASE("TickScheduler: shouldCheckWatermarks returns true every 100 ticks", "[scheduler]") {
    gTick.store(0, std::memory_order_relaxed);
    TickScheduler sched;
    for (int i = 0; i < 100; ++i) {
        REQUIRE(sched.shouldCheckWatermarks() == false);
        sched.tick();
    }
    REQUIRE(sched.shouldCheckWatermarks() == true);
}

TEST_CASE("TickScheduler: shouldMaintain returns true every 6000 ticks", "[scheduler]") {
    gTick.store(0, std::memory_order_relaxed);
    TickScheduler sched;
    for (int i = 0; i < 6000; ++i) {
        REQUIRE(sched.shouldMaintain() == false);
        sched.tick();
    }
    REQUIRE(sched.shouldMaintain() == true);
}

TEST_CASE("TickScheduler: handles 32-bit tick wrapping", "[scheduler]") {
    gTick.store(std::numeric_limits<uint32_t>::max() - 5,
                std::memory_order_relaxed);

    TickScheduler sched;
    // Advance past wrap (5 ticks to overflow, BROADCAST_INTERVAL to trigger)
    for (int i = 0; i < 205; ++i) {
        sched.tick();
    }
    // gTick ~= 199, shouldBroadcast needs BROADCAST_INTERVAL (200)
    REQUIRE(sched.shouldBroadcast() == false);

    sched.tick(); // gTick = 200
    REQUIRE(sched.shouldBroadcast() == true);
}
