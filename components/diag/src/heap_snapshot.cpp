#include "diag/heap_snapshot.hpp"
#include "esp_heap_caps.h"
#include "esp_log.h"

static constexpr auto TAG = "heap";

namespace ecotiter::diag {

bool HeapSnapshot::canAllocate(size_t size) noexcept {
    int largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    return largest >= 0 && static_cast<size_t>(largest) >= size;
}

size_t HeapSnapshot::largestFreeBlock() noexcept {
    int largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    return static_cast<size_t>(largest < 0 ? 0 : largest);
}

void HeapSnapshot::log() noexcept {
    int largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    auto free8 = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    auto total8 = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
    ESP_LOGI(TAG, "DRAM: total=%u free=%u largest=%d",
             static_cast<unsigned>(total8),
             static_cast<unsigned>(free8),
             largest);
}

bool HeapSnapshot::assertCanAllocate(size_t size) noexcept {
    int largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    if (largest < 0 || static_cast<size_t>(largest) < size) {
        ESP_LOGW(TAG, "Cannot alloc %u B (largest=%d B)",
                 static_cast<unsigned>(size),
                 largest);
        return false;
    }
    return true;
}

} // namespace ecotiter::diag
