#include "infrastructure/drivers/limitswitch.hpp"
#include "esp_log.h"
#include "esp_check.h"

static constexpr auto TAG = "limitswitch";

// CONTRACT: ISR handler runs in interrupt context. Only atomic store with
// relaxed ordering — no blocking, no heap, no logging. IRAM_ATTR ensures
// the handler remains mapped during flash cache misses.
static void IRAM_ATTR limitswitch_isr_handler(void* arg) {
    auto* flag = static_cast<std::atomic<bool>*>(arg);
    flag->store(true, std::memory_order_relaxed);
}

namespace ecotiter::infrastructure::drivers {

LimitSwitch::LimitSwitch(gpio_num_t pin, std::atomic<bool>& flag, bool pullDown)
    : pin_(pin)
    , flag_(flag) {

    puts("DBG: LS ctor A gpio_config"); fflush(stdout);
    gpio_config_t ioConf = {};
    ioConf.pin_bit_mask = (1ULL << pin_);
    ioConf.mode = GPIO_MODE_INPUT;
    ioConf.pull_up_en = GPIO_PULLUP_DISABLE;
    ioConf.pull_down_en = pullDown ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE;
    ioConf.intr_type = GPIO_INTR_POSEDGE;
    puts("DBG: LS ctor B calling gpio_config"); fflush(stdout);
    ESP_ERROR_CHECK(gpio_config(&ioConf));
    puts("DBG: LS ctor C gpio_config done"); fflush(stdout);

    static bool isrInstalled = false;
    if (!isrInstalled) {
        puts("DBG: LS ctor D calling gpio_install_isr_service"); fflush(stdout);
        esp_err_t err = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "Failed to install ISR service: %s", esp_err_to_name(err));
        }
        isrInstalled = true;
        puts("DBG: LS ctor E isr_service done"); fflush(stdout);
    }

    puts("DBG: LS ctor F calling gpio_isr_handler_add"); fflush(stdout);
    ESP_ERROR_CHECK(gpio_isr_handler_add(pin_, limitswitch_isr_handler,
                                         static_cast<void*>(&flag_)));
    puts("DBG: LS ctor G done"); fflush(stdout);
}

LimitSwitch::~LimitSwitch() {
    gpio_isr_handler_remove(pin_);
}

bool LimitSwitch::isTriggered() const {
    return flag_.load(std::memory_order_acquire);
}

void LimitSwitch::clear() {
    flag_.store(false, std::memory_order_release);
}

domain::Result<void, domain::StepperError> LimitSwitch::rearm() {
    esp_err_t err = gpio_intr_enable(pin_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to rearm interrupt: %s", esp_err_to_name(err));
        return std::unexpected(domain::StepperError::InitFailed);
    }
    return {};
}

bool LimitSwitch::level() const {
    return gpio_get_level(pin_) != 0;
}

} // namespace ecotiter::infrastructure::drivers
