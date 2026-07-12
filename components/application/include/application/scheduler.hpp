#pragma once

#include <atomic>
#include <cstdint>

namespace ecotiter::application {

// Global monotonic tick counter — incremented by main loop pacing tick (10ms)
// At FREERTOS_HZ=1000, 1 tick = 1ms, but main loop advances by 10 per iteration.
inline std::atomic<uint32_t> gTick{0};

class TickScheduler {
public:
    // Tick intervals (at 10ms per increment):
    static constexpr uint32_t BROADCAST_INTERVAL = 30;    // 300ms (30 ticks × 10ms)
    static constexpr uint32_t SAMPLE_INTERVAL    = 10;    // 100ms
    static constexpr uint32_t WATERMARK_INTERVAL = 100;   // 1s
    static constexpr uint32_t MAINT_INTERVAL     = 6000;  // 60s

    void tick() noexcept;

    [[nodiscard]] bool shouldBroadcast() const noexcept;
    [[nodiscard]] bool shouldSample() const noexcept;
    [[nodiscard]] bool shouldCheckWatermarks() const noexcept;
    [[nodiscard]] bool shouldMaintain() const noexcept;

private:
    uint32_t lastBroadcastTick_{0};
    uint32_t lastSampleTick_{0};
    uint32_t lastWatermarkCheckTick_{0};
    uint32_t lastMaintainTick_{0};
};

} // namespace ecotiter::application
