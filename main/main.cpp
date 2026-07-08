#include <cstdio>
#include <cstring>
#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "diag/black_box.hpp"
#include "diag/stack_monitor.hpp"
#include "diag/tick_watchdog.hpp"
#include "diag/ffi_guard.hpp"
#include "interface/serial.hpp"
#include "interface/broadcast.hpp"
#include "application/command.hpp"
#include "application/dispatch.hpp"
#include "application/scheduler.hpp"
#include "infrastructure/drivers/led.hpp"
#include "infrastructure/drivers/adc.hpp"
#include "infrastructure/motor_task.hpp"
#include "infrastructure/temp_thread.hpp"

static constexpr auto TAG = "main";
static constexpr auto BOOT_OK_MARKER = "BOOT_OK_MARKER";

extern "C" void app_main(void) {
    puts(BOOT_OK_MARKER);
    fflush(stdout);

    nvs_flash_init();

    auto& blackbox = ecotiter::diag::BlackBox::instance();
    blackbox.init();

    auto& stackmon = ecotiter::diag::StackMonitor::instance();
    stackmon.registerMainTask();

    ecotiter::interface::SerialReader serial;
    auto initResult = serial.init();
    if (!initResult) {
        ESP_LOGE(TAG, "Serial init failed: %d", static_cast<int>(initResult.error()));
    }

    ecotiter::infrastructure::drivers::Led led(GPIO_NUM_2);
    (void)led;

    xTaskCreate(
        motorTaskEntry,
        "motor",
        16384 / sizeof(configSTACK_DEPTH_TYPE),
        nullptr,
        5,
        nullptr);

    xTaskCreate(
        tempTaskEntry,
        "temp",
        16384 / sizeof(configSTACK_DEPTH_TYPE),
        nullptr,
        4,
        nullptr);

    ecotiter::infrastructure::drivers::AdcDriver adc(
        ADC_UNIT_1, ADC_CHANNEL_3);

    ecotiter::application::TickScheduler scheduler;
    TickType_t lastWake = xTaskGetTickCount();
    constexpr TickType_t PACING_TICK = pdMS_TO_TICKS(10);

    auto sendResponse = [&](const ecotiter::application::CommandResponse& rsp) {
        ecotiter::domain::memory::ResponseBuffer buf{};
        auto written = ecotiter::application::serializeToBuffer(rsp, buf);
        if (written && *written > 0) {
            size_t len = *written;
            if (len < buf.size()) {
                buf[len++] = '\n';
            }
            serial.write({buf.data(), len});
        }
    };

    while (true) {
        ecotiter::diag::TickWatchdog watchdog;
        (void)watchdog;

        scheduler.tick();

        if (scheduler.shouldSample()) {
            ecotiter::diag::FfiGuard guard(50);
            int16_t mv = adc.calibratedMv();
            if (mv < 0) { mv = 0; }
            ecotiter::domain::gLastMv.store(
                static_cast<uint16_t>(mv), std::memory_order_release);
        }

        if (scheduler.shouldBroadcast()) {
            ecotiter::interface::BroadcastEvent evt{
                .tick = ecotiter::application::gTick.load(std::memory_order_acquire),
                .tempCX100 = ecotiter::domain::gTempCX100.load(std::memory_order_acquire),
                .mv = ecotiter::domain::gLastMv.load(std::memory_order_acquire),
                .vlv = ecotiter::domain::gValvePosition.load(std::memory_order_acquire),
                .brt = ecotiter::domain::gBuretteState.load(std::memory_order_acquire),
                .dir = ecotiter::domain::gDirection.load(std::memory_order_acquire),
                .speed = ecotiter::domain::gSpeed.load(std::memory_order_acquire),
                .accel = ecotiter::domain::gAccel.load(std::memory_order_acquire),
                .volumeMl = ecotiter::domain::gVolumeMl.load(std::memory_order_acquire),
                .dispensedSteps = ecotiter::domain::gDispensedSteps.load(std::memory_order_acquire)
            };

            ecotiter::domain::memory::ResponseBuffer buf{};
            auto sv = ecotiter::interface::serializeBroadcast(evt, buf);
            if (!sv.empty()) {
                serial.write(sv);
                serial.write({"\n", 1});
            }
        }

        auto line = serial.process();
        if (line.has_value()) {
            ESP_LOGI(TAG, "RX: %.*s", static_cast<int>(line->size()), line->data());

            auto cmd = ecotiter::application::parseCommand(*line);
            if (cmd) {
                auto rsp = ecotiter::application::dispatch(*cmd);
                if (rsp) {
                    sendResponse(*rsp);
                } else {
                    sendResponse(ecotiter::application::makeErrorResponse("dispatch failed"));
                }
            } else {
                sendResponse(ecotiter::application::makeErrorResponse("parse failed"));
            }
        }

        vTaskDelayUntil(&lastWake, PACING_TICK);
    }
}
