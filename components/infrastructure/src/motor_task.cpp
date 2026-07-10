#include "infrastructure/motor_task.hpp"

#include <cstdint>
#include <cstdio>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "infrastructure/config.hpp"
#include "infrastructure/drivers/stepper.hpp"
#include "infrastructure/drivers/limitswitch.hpp"
#include "domain/types.hpp"
#include "domain/errors.hpp"
#include "diag/ffi_guard.hpp"
#include "diag/stack_monitor.hpp"
#include "diag/state_tracer.hpp"

static constexpr auto TAG = "motor_task";

namespace ecotiter::infrastructure {

QueueHandle_t gMotorCmdQueue = nullptr;

namespace {

using drivers::StepperMotor;
using drivers::LimitSwitch;
using domain::Direction;
using domain::BuretteState;

static constexpr size_t INTERVAL_CHUNK = 128;
static constexpr uint32_t HOME_SPEED_HZ = 1500;
static constexpr uint32_t HOME_INTERVAL_US = 1'000'000 / HOME_SPEED_HZ;
static constexpr uint32_t HOMING_TIMEOUT_MS = 120000;

void assert_rmt_preconditions() {
    // Check that RMT driver is initialized and GPIO pins are valid
    // In ESP-IDF v6, RMT is initialized on first use
    // CONTRACT: call before any RMT transmit operation
}

const char* buretteStateName(BuretteState s) {
    switch (s) {
        case BuretteState::Idle:      return "idle";
        case BuretteState::Homing:    return "homing";
        case BuretteState::Filling:   return "filling";
        case BuretteState::Emptying:  return "emptying";
        case BuretteState::Dosing:    return "dosing";
        case BuretteState::Rinsing:   return "rinsing";
        case BuretteState::Stopping:  return "stopping";
        case BuretteState::Error:     return "error";
    }
    return "unknown";
}

void run_homing(StepperMotor& stepper, LimitSwitch& fullSwitch) {
    puts("DBG: run_homing START"); fflush(stdout);

    diag::FfiGuard guard(30);

    ESP_LOGI(TAG, "Starting homing sequence");

    auto oldState = domain::gBuretteState.load(std::memory_order_acquire);
    domain::gBuretteState.store(BuretteState::Homing, std::memory_order_release);
    diag::StateTracer::logBuretteTransition(
        buretteStateName(oldState), "homing");

    // Move toward FULL limit switch (CW = LiqIn)
    gpio_set_level(config::PIN_DIR, 1);

    // Clear any stale FULL limit flag
    fullSwitch.clear();
    domain::gStopFull.store(false, std::memory_order_release);

    uint32_t intervals[INTERVAL_CHUNK];
    for (size_t i = 0; i < INTERVAL_CHUNK; ++i) {
        intervals[i] = HOME_INTERVAL_US;
    }

    uint32_t totalSent = 0;
    bool homed = false;
    auto startTime = xTaskGetTickCount();
    bool timedOut = false;

    while (!homed && !timedOut) {
        assert_rmt_preconditions();

        if (domain::gStopFull.load(std::memory_order_acquire)) {
            homed = true;
            break;
        }

        if ((xTaskGetTickCount() - startTime) >= pdMS_TO_TICKS(HOMING_TIMEOUT_MS)) {
            timedOut = true;
            break;
        }

        size_t chunk = INTERVAL_CHUNK;

        auto result = stepper.moveStepsIntervals(
            {intervals, chunk},
            &domain::gStopFull);

        if (!result) {
            if (result.error() == domain::StepperError::LimitSwitchTriggered) {
                homed = true;
            } else {
                ESP_LOGE(TAG, "Homing RMT error: %d",
                         static_cast<int>(result.error()));
                domain::gBuretteState.store(BuretteState::Error,
                                            std::memory_order_release);
                diag::StateTracer::logBuretteTransition(
                    "homing", "error");
                return;
            }
        }

        totalSent += static_cast<uint32_t>(chunk);

        if (domain::gStopFull.load(std::memory_order_acquire)) {
            homed = true;
        }

        if ((xTaskGetTickCount() - startTime) >= pdMS_TO_TICKS(HOMING_TIMEOUT_MS)) {
            timedOut = true;
        }
    }

    std::ignore = stepper.emergencyStop();
    std::ignore = stepper.enable();

    if (homed) {
        stepper.setCurrentPosition(domain::Steps{0});
        domain::gStopFull.store(false, std::memory_order_release);
        ESP_LOGI(TAG, "Homing complete at step %lu (FULL limit)",
                 static_cast<unsigned long>(totalSent));
    } else if (timedOut) {
        ESP_LOGW(TAG, "Homing timed out after %lu ms (%lu steps)",
                 static_cast<unsigned long>(HOMING_TIMEOUT_MS),
                 static_cast<unsigned long>(totalSent));
    }

    domain::gBuretteState.store(BuretteState::Idle, std::memory_order_release);
    diag::StateTracer::logBuretteTransition(
        "homing", "idle");
}

void execute_move_steps(StepperMotor& stepper, int32_t steps) {
    if (steps <= 0) return;

    auto dir = domain::gDirection.load(std::memory_order_acquire);
    gpio_set_level(config::PIN_DIR, (dir == Direction::Cw) ? 1 : 0);
    vTaskDelay(pdMS_TO_TICKS(1));

    auto speed = domain::gSpeed.load(std::memory_order_acquire);
    if (speed < config::STEP_FREQ_MIN_HZ) speed = config::STEP_FREQ_MIN_HZ;
    if (speed > config::STEP_FREQ_MAX_HZ) speed = config::STEP_FREQ_MAX_HZ;

    uint32_t intervalUs = 1'000'000 / speed;

    uint32_t intervals[INTERVAL_CHUNK];
    for (size_t i = 0; i < INTERVAL_CHUNK; ++i) {
        intervals[i] = intervalUs;
    }

    int32_t remaining = steps;
    while (remaining > 0) {
        assert_rmt_preconditions();

        if (domain::gStopFull.load(std::memory_order_acquire)) {
            ESP_LOGI(TAG, "Move interrupted by stop flag");
            break;
        }

        size_t chunk = static_cast<size_t>(remaining);
        if (chunk > INTERVAL_CHUNK) chunk = INTERVAL_CHUNK;

        auto result = stepper.moveStepsIntervals(
            {intervals, chunk},
            &domain::gStopFull);

        if (!result) {
            if (result.error() == domain::StepperError::LimitSwitchTriggered) {
                ESP_LOGI(TAG, "Limit switch triggered during move");
                domain::gBuretteState.store(BuretteState::Error,
                                            std::memory_order_release);
                diag::StateTracer::logBuretteTransition(
                    buretteStateName(
                        domain::gBuretteState.load(std::memory_order_acquire)),
                    "error");
            } else {
                ESP_LOGE(TAG, "Move RMT error: %d",
                         static_cast<int>(result.error()));
            }
            break;
        }

        remaining -= static_cast<int32_t>(chunk);
    }
}

} // anonymous namespace

extern "C" void motorTaskEntry(void* pvParameters) {
    (void)pvParameters;

    puts("DBG: motorEntry START"); fflush(stdout);

    diag::StackMonitor::instance().registerThread("motor", domain::MOTOR_THREAD_STACK);

    // Fix 2: DRAM pre-check before queue allocation
    {
        auto largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
        ESP_LOGI(TAG, "motor task DRAM before queue: largest_free=%lu",
                 (unsigned long)largest);
        if (largest < 8192) {
            ESP_LOGW(TAG, "Low DRAM for motor queue! largest_free=%lu",
                     (unsigned long)largest);
        }
    }

    gMotorCmdQueue = xQueueCreate(4, sizeof(MotorCommand));
    if (gMotorCmdQueue == nullptr) {
        ESP_LOGE(TAG, "Failed to create motor command queue");
        vTaskDelete(nullptr);
        return;
    }
    puts("DBG: motor queue created"); fflush(stdout);

    // Fix 1: GPIO spinlock deadlock prevention
    // LL-031: PHY calibration holds gpio_spinlock for 10-200ms after BT init.
    // The main loop waited 1000ms before creating this task, but there's still
    // a small chance the motor task starts running before PHY completes.
    // Additionally, the FIRST gpio_config (in StepperMotor ctor) may retrigger
    // a short PHY window. We insert a safety delay before the next GPIO.
    puts("DBG: before StepperMotor ctor (gpio 21,26,27)"); fflush(stdout);
    StepperMotor stepper(config::PIN_STEP, config::PIN_DIR, config::PIN_EN);
    puts("DBG: after StepperMotor ctor"); fflush(stdout);

    puts("DBG: before LimitSwitch homeSwitch ctor (gpio 35)"); fflush(stdout);
    LimitSwitch homeSwitch(config::PIN_LIMIT_HOME,
                           domain::gStopHome, false);
    puts("DBG: after LimitSwitch homeSwitch ctor"); fflush(stdout);

    // Safety delay between GPIO configs. The StepperMotor ctor's gpio_config
    // may briefly re-trigger PHY calibration activity. This delay gives
    // gpio_spinlock time to settle before the next gpio_config (LL-031 belt).
    puts("DBG: PHY inter-GPIO safety delay start"); fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(500));
    puts("DBG: PHY inter-GPIO safety delay done"); fflush(stdout);

    puts("DBG: before LimitSwitch fullSwitch ctor (gpio 34)"); fflush(stdout);
    LimitSwitch fullSwitch(config::PIN_LIMIT_FULL,
                           domain::gStopFull, false);
    puts("DBG: after LimitSwitch fullSwitch ctor"); fflush(stdout);

    puts("DBG: before run_homing"); fflush(stdout);
    run_homing(stepper, fullSwitch);

    MotorCommand cmd;
    while (true) {
        if (xQueueReceive(gMotorCmdQueue, &cmd, pdMS_TO_TICKS(100))) {
            switch (cmd.type) {

            case MotorCommandType::MoveSteps:
                execute_move_steps(stepper, cmd.steps);
                break;

            case MotorCommandType::Stop: {
                ESP_LOGI(TAG, "Stop requested");
                domain::gBuretteState.store(BuretteState::Stopping,
                                            std::memory_order_release);
                domain::gStopFull.store(true, std::memory_order_release);
                vTaskDelay(pdMS_TO_TICKS(50));
                domain::gStopFull.store(false, std::memory_order_release);
                domain::gBuretteState.store(BuretteState::Idle,
                                            std::memory_order_release);
                break;
            }

            case MotorCommandType::EmergencyStop: {
                ESP_LOGW(TAG, "EMERGENCY STOP");
                domain::gStopFull.store(true, std::memory_order_release);
                domain::gStopHome.store(true, std::memory_order_release);
                domain::gBuretteState.store(BuretteState::Error,
                                            std::memory_order_release);
                diag::StateTracer::logBuretteTransition(
                    buretteStateName(
                        domain::gBuretteState.load(std::memory_order_acquire)),
                    "error");
                break;
            }

            case MotorCommandType::Home:
                run_homing(stepper, homeSwitch);
                break;

            case MotorCommandType::SetDirection: {
                auto dir = cmd.direction;
                domain::gDirection.store(dir, std::memory_order_release);
                gpio_set_level(config::PIN_DIR,
                               (dir == Direction::Cw) ? 1 : 0);
                break;
            }

            case MotorCommandType::SetSpeed:
                if (cmd.speedHz >= config::STEP_FREQ_MIN_HZ &&
                    cmd.speedHz <= config::STEP_FREQ_MAX_HZ) {
                    domain::gSpeed.store(cmd.speedHz, std::memory_order_release);
                }
                break;

            case MotorCommandType::SetAccel:
                domain::gAccel.store(cmd.accelHzPerS, std::memory_order_release);
                break;
            }
        }
    }
}

} // namespace ecotiter::infrastructure
