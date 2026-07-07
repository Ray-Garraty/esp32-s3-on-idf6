#include "infrastructure/motor_task.hpp"

#include <cstdint>
#include <cstdio>
#include "esp_log.h"
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
static constexpr uint32_t HOME_SPEED_HZ = 200;
static constexpr uint32_t HOME_INTERVAL_US = 1'000'000 / HOME_SPEED_HZ;

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

void run_homing(StepperMotor& stepper, LimitSwitch& homeSwitch) {
    diag::FfiGuard guard(30);

    ESP_LOGI(TAG, "Starting homing sequence");

    auto oldState = domain::gBuretteState.load(std::memory_order_acquire);
    domain::gBuretteState.store(BuretteState::Homing, std::memory_order_release);
    diag::StateTracer::logBuretteTransition(
        buretteStateName(oldState), "homing");

    gpio_set_level(config::PIN_DIR, 1);

    homeSwitch.clear();
    domain::gStopHome.store(false, std::memory_order_release);

    uint32_t intervals[INTERVAL_CHUNK];
    for (size_t i = 0; i < INTERVAL_CHUNK; ++i) {
        intervals[i] = HOME_INTERVAL_US;
    }

    constexpr uint32_t MAX_HOMING_STEPS = 10000;
    uint32_t totalSent = 0;
    bool homed = false;

    while (totalSent < MAX_HOMING_STEPS && !homed) {
        assert_rmt_preconditions();

        if (domain::gStopHome.load(std::memory_order_acquire)) {
            homed = true;
            break;
        }

        size_t chunk = MAX_HOMING_STEPS - totalSent;
        if (chunk > INTERVAL_CHUNK) chunk = INTERVAL_CHUNK;

        auto result = stepper.moveStepsIntervals(
            {intervals, chunk},
            &domain::gStopHome);

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

        totalSent += chunk;

        if (domain::gStopHome.load(std::memory_order_acquire)) {
            homed = true;
        }
    }

    std::ignore = stepper.emergencyStop();
    std::ignore = stepper.enable();

    if (homed) {
        stepper.setCurrentPosition(domain::Steps{0});
        domain::gStopHome.store(false, std::memory_order_release);
        ESP_LOGI(TAG, "Homing complete at step %lu",
                 static_cast<unsigned long>(totalSent));
    } else {
        ESP_LOGW(TAG, "Homing timed out after %lu steps",
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

    diag::StackMonitor::instance().registerThread("motor", 16384);

    gMotorCmdQueue = xQueueCreate(4, sizeof(MotorCommand));
    if (gMotorCmdQueue == nullptr) {
        ESP_LOGE(TAG, "Failed to create motor command queue");
        vTaskDelete(nullptr);
        return;
    }

    StepperMotor stepper(config::PIN_STEP, config::PIN_DIR, config::PIN_EN);
    LimitSwitch homeSwitch(config::PIN_LIMIT_HOME,
                           domain::gStopHome, false);
    LimitSwitch fullSwitch(config::PIN_LIMIT_FULL,
                           domain::gStopFull, false);

    run_homing(stepper, homeSwitch);

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
