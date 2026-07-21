#include "infrastructure/drivers/valve.hpp"
#include "infrastructure/config.hpp"

#include <atomic>
#include <cstdio>
#include <cstring>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "domain/memory.hpp"

// Declared in main/net_owner.hpp — valve timer callback pushes WS event without
// calling broadcastWsEvent() directly (net_owner drain loop owns the HTTP server)
extern QueueHandle_t gWsBroadcastQueue;

namespace ecotiter::infrastructure::drivers
{

Valve gValve(config::PIN_VALVE);

Valve::Valve(gpio_num_t pin)
    : pin_(pin)
{
    gpio_set_direction(pin_, GPIO_MODE_OUTPUT);
    gpio_set_level(pin_, 0); // Input position (LOW)
}

void Valve::setPosition(domain::ValvePosition position)
{
    switch (position)
    {
    case domain::ValvePosition::Input:
        gpio_set_level(pin_, 0);
        break;
    case domain::ValvePosition::Output:
        gpio_set_level(pin_, 1);
        break;
    }
    position_ = position;
}

domain::ValvePosition Valve::getPosition() const noexcept
{
    return position_;
}

// ── Valve settle timer ──────────────────────────────────────────
static constexpr auto VALVE_TAG = "valve";

namespace
{
// RAII wrapper for esp_timer_handle_t (Constitution Art. VI)
class ValveSettleTimer
{
    esp_timer_handle_t handle_ = nullptr;

public:
    ValveSettleTimer() = default;
    ValveSettleTimer(const ValveSettleTimer&) = delete;
    ValveSettleTimer& operator=(const ValveSettleTimer&) = delete;
    ~ValveSettleTimer()
    {
        if (handle_)
        {
            esp_timer_delete(handle_);
        }
    }

    esp_timer_handle_t get() const { return handle_; }
    bool isCreated() const { return handle_ != nullptr; }

    void create(const esp_timer_create_args_t* args)
    {
        ESP_ERROR_CHECK(esp_timer_create(args, &handle_));
    }
};

ValveSettleTimer s_valveTimer;
std::atomic<bool> s_valveTimerArmed{false};

// CONTRACT:
//   Invariant: gValveTimerArmed must be true when this fires.
//   Context: esp_timer callback — runs in esp_timer task, NOT ISR.
//            No blocking allowed (xQueueSend with 0 timeout is safe).
//   Risk: If gValveIsSettling is cleared while a burette op is in
//         progress, subsequent valve.setPosition checks would pass
//         but gBuretteState CAS guards prevent actual misuse.
void valveSettleCallback(void* arg)
{
    auto pos = static_cast<domain::ValvePosition>(reinterpret_cast<uintptr_t>(arg));
    const char* posStr = (pos == domain::ValvePosition::Input) ? "input" : "output";
    ESP_LOGI(VALVE_TAG, "Valve settled: position=%s", posStr);

    // Push valve_settled WS event — non-blocking
    // (Article I Constitution: no blocking operations in timer context)
    if (gWsBroadcastQueue)
    {
        static struct
        {
            char data[domain::memory::MAX_RSP_SIZE];
            size_t len;
        } entry;
        int n = std::snprintf(entry.data, sizeof(entry.data),
                              R"({"event":"valve_settled","position":"%s"})", posStr);
        if (n > 0 && static_cast<size_t>(n) < sizeof(entry.data))
        {
            entry.len = static_cast<size_t>(n);
            if (xQueueSend(gWsBroadcastQueue, &entry, 0) != pdPASS)
            {
                ESP_LOGW(VALVE_TAG, "WS queue full, dropping valve_settled event");
            }
        }
    }

    domain::gValveIsSettling.store(false, std::memory_order_release);
    s_valveTimerArmed.store(false, std::memory_order_release);
}
} // anonymous namespace

void armValveSettleTimer(domain::ValvePosition pos)
{
    if (!s_valveTimer.isCreated())
    {
        esp_timer_create_args_t args = {};
        args.callback = valveSettleCallback;
        args.arg = reinterpret_cast<void*>(
            static_cast<uintptr_t>(pos == domain::ValvePosition::Input ? 0 : 1));
        args.name = "valve_settle";
        s_valveTimer.create(&args);
    }
    else
    {
        // Cancel previous if re-arming (rapid toggle)
        // TOCTOU race: timer may fire between exchange and stop.
        // ESP_ERR_INVALID_STATE from esp_timer_stop is benign.
        if (s_valveTimerArmed.exchange(true, std::memory_order_acq_rel))
        {
            esp_err_t err = esp_timer_stop(s_valveTimer.get());
            if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
            {
                ESP_ERROR_CHECK(err);
            }
        }
    }
    // exchange() already set s_valveTimerArmed=true — no redundant store needed
    ESP_ERROR_CHECK(esp_timer_start_once(s_valveTimer.get(), config::VALVE_SETTLE_MS * 1000));
}

void cancelValveSettleTimer()
{
    if (s_valveTimerArmed.exchange(false, std::memory_order_acq_rel))
    {
        if (s_valveTimer.isCreated())
        {
            esp_err_t err = esp_timer_stop(s_valveTimer.get());
            if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
            {
                ESP_ERROR_CHECK(err);
            }
        }
    }
}

} // namespace ecotiter::infrastructure::drivers
