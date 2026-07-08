#include <cstdio>
#include <cstring>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "application/command.hpp"
#include "application/dispatch.hpp"
#include "application/scheduler.hpp"
#include "diag/black_box.hpp"
#include "diag/ffi_guard.hpp"
#include "diag/rtc_watchdog.hpp"
#include "diag/stack_monitor.hpp"
#include "diag/tick_watchdog.hpp"
#include "infrastructure/config.hpp"
#include "infrastructure/drivers/adc.hpp"
#include "infrastructure/drivers/rgb_led.hpp"
#include "infrastructure/motor_task.hpp"
#include "infrastructure/network/ble.hpp"
#include "infrastructure/temp_thread.hpp"
#include "interface/broadcast.hpp"
#include "interface/serial.hpp"

static constexpr auto TAG = "main";

static constexpr auto BOOT_OK_MARKER = "BOOT_OK_MARKER";

// LL-031: PHY RF calibration holds gpio_spinlock for 10-200ms asynchronously
// after BT init. This constant controls how long we wait for it to complete
// before any GPIO operations (motor task, LED, etc.).
static constexpr TickType_t PHY_CALIBRATION_WAIT_TICKS = pdMS_TO_TICKS(1000);

// Fix 2: DRAM pre-check — must be called before every xTaskCreate
static void logDramBeforeTask(const char* name, size_t stackSize)
{
    auto largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    ESP_LOGI(TAG, "Creating %s, stack=%lu, largest_free=%lu", name, (unsigned long)stackSize,
             (unsigned long)largest);
    if (largest < stackSize + 4096)
    {
        ESP_LOGW(TAG, "DRAM pressure! Largest free block %lu < needed %lu", (unsigned long)largest,
                 (unsigned long)(stackSize + 4096));
    }
}

// Fix 1: GPIO spinlock deadlock prevention
// Wait for asynchronous PHY calibration to release gpio_spinlock before any
// gpio_config/gpio_set_direction/gpio_set_level call (LL-031).
// GR-1: blocking delay OK at boot (not in main loop).
static void ensureGpioReady()
{
    ESP_LOGI(TAG, "PHY wait: sleeping %lu ms for gpio_spinlock release...",
             (unsigned long)(PHY_CALIBRATION_WAIT_TICKS * portTICK_PERIOD_MS));
    puts("DBG: PHY wait start");
    fflush(stdout);
    vTaskDelay(PHY_CALIBRATION_WAIT_TICKS);
    puts("DBG: PHY wait done");
    fflush(stdout);
    ESP_LOGI(TAG, "PHY wait complete — GPIO spinlock should be released");
}

extern "C" void app_main(void)
{
    puts(BOOT_OK_MARKER);
    fflush(stdout);

    using namespace ecotiter;

    // Warmup: flush any stale serial data, ensure stdout works
    fflush(stdout);

    // ====== Step 1: NVS ======
    puts("DBG: step 1 - nvs_flash_init");
    fflush(stdout);
    domain::gBootProgress.store(domain::BootProgress::Nvs, std::memory_order_release);
    nvs_flash_init();

    // ====== Step 2: BlackBox ======
    puts("DBG: step 2 - blackbox");
    fflush(stdout);
    domain::gBootProgress.store(domain::BootProgress::BlackBox, std::memory_order_release);
    auto& blackbox = diag::BlackBox::instance();
    blackbox.init();

    // ====== Step 3: StackMonitor ======
    puts("DBG: step 3 - stack_monitor");
    fflush(stdout);
    domain::gBootProgress.store(domain::BootProgress::StackMonitor, std::memory_order_release);
    auto& stackmon = diag::StackMonitor::instance();
    stackmon.registerMainTask();

    // ====== Step 4: Serial ======
    puts("DBG: step 4 - serial");
    fflush(stdout);
    domain::gBootProgress.store(domain::BootProgress::Serial, std::memory_order_release);
    interface::SerialReader serial;
    auto initResult = serial.init();
    if (!initResult)
    {
        ESP_LOGE(TAG, "Serial init failed: %d", static_cast<int>(initResult.error()));
    }

    // ====== Step 5: RWDT (RTC Watchdog) — ultimate hang protection ======
    // RWDT runs on RTC slow clock, independent of CPU interrupts.
    // If ANY task holds a spinlock > 6s (or the system freezes entirely),
    // RWDT fires → RESET_SYSTEM → reboot with RTCWDT_SYS_RESET in boot log.
    // Configured BEFORE BLE init so it covers the PHY calibration window too.
    puts("DBG: step 5 - RWDT init");
    fflush(stdout);
    domain::gBootProgress.store(domain::BootProgress::RtcWdt, std::memory_order_release);
    diag::RtcWatchdog rtcWdt;
    (void)rtcWdt;

    // DRAM snapshot before BLE
    {
        auto freeHeap = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
        ESP_LOGI(TAG, "DRAM before BLE: %zu bytes", freeHeap);
    }

    // ====== Step 6: BLE init (triggers async PHY calibration) ======
    puts("DBG: step 6 - BLE init");
    fflush(stdout);
    domain::gBootProgress.store(domain::BootProgress::BleInit, std::memory_order_release);
    infrastructure::network::BleManager bleManager;
    auto bleInitResult = bleManager.init();
    if (bleInitResult)
    {
        ESP_LOGI(TAG, "BLE initialized successfully");
    }
    else
    {
        ESP_LOGW(TAG, "BLE init skipped (insufficient heap or HW error)");
    }

    // ====== Step 7: Wait for PHY calibration to release gpio_spinlock ======
    // LL-031: PHY calibration holds gpio_spinlock for 10-200ms after BT init.
    // Any gpio_config/gpio_set_direction/gpio_set_level during this window
    // deadlocks. All GPIO work (motor task, LED, sensors) must wait.
    puts("DBG: step 7 - PHY wait");
    fflush(stdout);
    domain::gBootProgress.store(domain::BootProgress::PhyWait, std::memory_order_release);
    ensureGpioReady();

    // DRAM snapshot after BLE + PHY wait (before motor task)
    {
        auto freeHeap = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
        ESP_LOGI(TAG, "DRAM after BLE, before motor: %zu bytes", freeHeap);
    }

    // ====== Step 7b: RGB LED ======
    infrastructure::drivers::RgbLed rgbLed(config::PIN_LED_RGB);
    domain::TransportMode currentLedMode = domain::TransportMode::UsbActive;
    bool currentLedError = false;

    // If BLE init succeeded, show advertising (blue)
    if (bleInitResult)
    {
        currentLedMode = domain::TransportMode::BleAdvertising;
        currentLedError = false;
        rgbLed.setTransportMode(currentLedMode, currentLedError);
    }
    else
    {
        // BLE failed — show red for 2 seconds, then off
        rgbLed.setTransportMode(domain::TransportMode::BleAdvertising, true);
    }

    // ====== Step 8: Motor task ======
    puts("DBG: step 8 - xTaskCreate motor");
    fflush(stdout);
    domain::gBootProgress.store(domain::BootProgress::MotorTask, std::memory_order_release);
    logDramBeforeTask("motor", domain::MOTOR_THREAD_STACK);
    BaseType_t mt = xTaskCreate(motorTaskEntry, "motor",
                                domain::MOTOR_THREAD_STACK / sizeof(configSTACK_DEPTH_TYPE),
                                nullptr, 1, nullptr);
    {
        char dbg[64];
        std::snprintf(dbg, sizeof(dbg), "DBG: xTaskCreate motor returned=%d\n", (int)mt);
        puts(dbg);
        fflush(stdout);
    }

    // temp task skipped for now (domain::gTempCX100 populated by main loop ADC only)

    domain::gBootProgress.store(domain::BootProgress::Running, std::memory_order_release);
    puts("DBG: step 9 - RUNNING");
    fflush(stdout);

    diag::FfiGuard guard(50);
    infrastructure::drivers::AdcDriver adc(ADC_UNIT_1, ADC_CHANNEL_3);

    application::TickScheduler scheduler;
    TickType_t lastWake = xTaskGetTickCount();
    constexpr TickType_t PACING_TICK = pdMS_TO_TICKS(10);

    auto sendResponse = [&](const ecotiter::application::CommandResponse& rsp) {
        ecotiter::domain::memory::ResponseBuffer buf{};
        auto written = ecotiter::application::serializeToBuffer(rsp, buf);
        if (written && *written > 0)
        {
            size_t len = *written;
            if (len < buf.size())
            {
                buf[len++] = '\n';
            }
            serial.write({buf.data(), len});
            // Also send over BLE if connected
            if (bleManager.isConnected())
            {
                std::ignore = bleManager.sendNotification({buf.data(), len});
            }
        }
    };

    while (true)
    {
        ecotiter::diag::TickWatchdog watchdog;
        (void)watchdog;

        // Feed RWDT every main loop iteration (10ms).
        // If ANY task holds a spinlock > 6s, RWDT fires → RESET_SYSTEM.
        rtcWdt.feed();

        scheduler.tick();

        if (scheduler.shouldSample())
        {
            ecotiter::diag::FfiGuard guard(50);
            int16_t mv = adc.calibratedMv();
            if (mv < 0)
            {
                mv = 0;
            }
            ecotiter::domain::gLastMv.store(static_cast<uint16_t>(mv), std::memory_order_release);
        }

        if (scheduler.shouldBroadcast())
        {
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
                .dispensedSteps =
                    ecotiter::domain::gDispensedSteps.load(std::memory_order_acquire)};

            ecotiter::domain::memory::ResponseBuffer buf{};
            auto sv = ecotiter::interface::serializeBroadcast(evt, buf);
            if (!sv.empty())
            {
                serial.write(sv);
                serial.write({"\n", 1});
                if (bleManager.isConnected())
                {
                    std::string_view bleSv(sv.data(), sv.size());
                    std::ignore = bleManager.sendNotification(bleSv);
                    std::ignore = bleManager.sendNotification({"\n", 1});
                }
            }
        }

        // Process BLE zombie detection
        bleManager.process();

        // Update RGB LED based on BLE/USB state
        {
            domain::TransportMode newLedMode;
            bool newLedError;

            if (domain::gUsbHandshakeReceived.load(std::memory_order_acquire))
            {
                // USB active takes priority — LED off
                newLedMode = domain::TransportMode::UsbActive;
                newLedError = false;
            }
            else if (domain::gBleError.load(std::memory_order_acquire))
            {
                // BLE error — red
                newLedMode = domain::TransportMode::BleAdvertising;
                newLedError = true;
            }
            else if (bleManager.isConnected())
            {
                // BLE connected — green
                newLedMode = domain::TransportMode::BleConnected;
                newLedError = false;
            }
            else
            {
                // Advertising (not connected, no error) — blue
                newLedMode = domain::TransportMode::BleAdvertising;
                newLedError = false;
            }

            if (newLedMode != currentLedMode || newLedError != currentLedError)
            {
                char dbg[80];
                std::snprintf(dbg, sizeof(dbg), "DBG: LED %d->%d err=%d isConnected=%d gBleErr=%d",
                              static_cast<int>(currentLedMode),
                              static_cast<int>(newLedMode),
                              static_cast<int>(newLedError),
                              static_cast<int>(bleManager.isConnected()),
                              static_cast<int>(domain::gBleError.load(std::memory_order_acquire)));
                puts(dbg); fflush(stdout);
                currentLedMode = newLedMode;
                currentLedError = newLedError;
                rgbLed.setTransportMode(currentLedMode, currentLedError);
            }
        }

        // Drain BLE command queue
        {
            ecotiter::infrastructure::network::BleCmdItem bleItem;
            while (xQueueReceive(bleManager.commandQueue(), &bleItem, 0) == pdTRUE)
            {
                ESP_LOGI(TAG, "BLE RX: %s", bleItem.data);

                std::string_view line(bleItem.data);
                auto cmd = ecotiter::application::parseCommand(line);
                if (cmd)
                {
                    auto rsp = ecotiter::application::dispatch(*cmd);
                    if (rsp)
                    {
                        sendResponse(*rsp);
                    }
                    else
                    {
                        sendResponse(ecotiter::application::makeErrorResponse("dispatch failed"));
                    }
                }
                else
                {
                    sendResponse(ecotiter::application::makeErrorResponse("parse failed"));
                }
            }
        }

        // Process serial commands
        auto line = serial.process();
        if (line.has_value())
        {
            ESP_LOGI(TAG, "RX: %.*s", static_cast<int>(line->size()), line->data());

            ecotiter::domain::gUsbHandshakeReceived.store(true, std::memory_order_release);

            auto cmd = ecotiter::application::parseCommand(*line);
            if (cmd)
            {
                auto rsp = ecotiter::application::dispatch(*cmd);
                if (rsp)
                {
                    sendResponse(*rsp);
                }
                else
                {
                    sendResponse(ecotiter::application::makeErrorResponse("dispatch failed"));
                }
            }
            else
            {
                sendResponse(ecotiter::application::makeErrorResponse("parse failed"));
            }
        }

        vTaskDelayUntil(&lastWake, PACING_TICK);
    }
}
