#include "diag/black_box.hpp"
#include <cstdio>
#include <cstring>
#include "esp_timer.h"

namespace ecotiter::diag {

void BlackBox::init() noexcept {
    head_.store(0, std::memory_order_release);
}

void BlackBox::record(Event e) noexcept {
    e.timestampUs = static_cast<uint64_t>(esp_timer_get_time());
    size_t idx = head_.fetch_add(1, std::memory_order_acq_rel) % RING_SIZE;
    // CONTRACT: volatile ring buffer — use memcpy for volatile access
    std::memcpy(const_cast<Event*>(&ring_[idx]), &e, sizeof(Event));
}

void BlackBox::dump() const noexcept {
    size_t head = head_.load(std::memory_order_acquire);
    size_t count = head < RING_SIZE ? head : RING_SIZE;

    printf("=== BLACK BOX (%zu events, newest first) ===\n", count);
    for (size_t i = 0; i < count; ++i) {
        size_t idx = (head - 1 - i) % RING_SIZE;
        const auto& e = ring_[idx];
        printf("[%llu] t%hhu type=%hhu id=%hu val=%lu\n",
               static_cast<unsigned long long>(e.timestampUs),
               e.threadId,
               static_cast<uint8_t>(e.type),
               e.payloadId,
               static_cast<unsigned long>(e.payloadValue));
    }
}

} // namespace ecotiter::diag
