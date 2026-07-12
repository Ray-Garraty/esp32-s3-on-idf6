#include "application/handlers/burette_ops.hpp"

#include "application/command.hpp"
#include "domain/calibration.hpp"
#include "domain/cal_run_planner.hpp"
#include "domain/types.hpp"
#include "infrastructure/motor_task.hpp"
#include "infrastructure/storage/nvs.hpp"

namespace ecotiter::application::handlers::burette_ops {
namespace {

bool sendMotorCommand(infrastructure::MotorCommand&& cmd) {
  if (infrastructure::gMotorCmdQueue == nullptr) return false;
  return xQueueSend(infrastructure::gMotorCmdQueue, &cmd, pdMS_TO_TICKS(10)) == pdTRUE;
}

} // anonymous namespace

std::expected<CommandResponse, domain::AppError> handleFill() {
  domain::gBuretteState.store(domain::BuretteState::Filling, std::memory_order_release);

  auto cal = infrastructure::storage::calibrationRead();
  if (!cal) return makeErrorResponse("start_failed");

  int32_t steps = static_cast<int32_t>(cal->nominalVolumeMl * cal->stepsPerMl + 0.5f);
  domain::gDirection.store(domain::Direction::LiqIn, std::memory_order_release);

  infrastructure::MotorCommand cmd{};
  cmd.type = infrastructure::MotorCommandType::MoveSteps;
  cmd.steps = steps;
  cmd.direction = domain::Direction::LiqIn;
  cmd.speedHz = domain::gSpeed.load(std::memory_order_acquire);
  cmd.accelHzPerS = domain::gAccel.load(std::memory_order_acquire);
  sendMotorCommand(std::move(cmd));
  return makeAckThenResponse();
}

std::expected<CommandResponse, domain::AppError> handleEmpty() {
  domain::gBuretteState.store(domain::BuretteState::Emptying, std::memory_order_release);

  auto cal = infrastructure::storage::calibrationRead();
  if (!cal) return makeErrorResponse("start_failed");

  float curVol = domain::gVolumeMl.load(std::memory_order_acquire);
  int32_t steps = static_cast<int32_t>(curVol * cal->stepsPerMl + 0.5f);
  if (steps < 10) steps = static_cast<int32_t>(cal->nominalVolumeMl * cal->stepsPerMl + 0.5f);
  domain::gDirection.store(domain::Direction::LiqOut, std::memory_order_release);

  infrastructure::MotorCommand cmd{};
  cmd.type = infrastructure::MotorCommandType::MoveSteps;
  cmd.steps = steps;
  cmd.direction = domain::Direction::LiqOut;
  cmd.speedHz = domain::gSpeed.load(std::memory_order_acquire);
  cmd.accelHzPerS = domain::gAccel.load(std::memory_order_acquire);
  sendMotorCommand(std::move(cmd));
  return makeAckThenResponse();
}

std::expected<CommandResponse, domain::AppError> handleDoseVolume(
    std::optional<domain::Ml> volume,
    std::optional<float> speedMlMin) {
  if (!volume) {
    return makeErrorResponse("invalid_params");
  }
  auto cal = infrastructure::storage::calibrationRead();
  if (!cal) return makeErrorResponse("start_failed");

  float currentVol = domain::gVolumeMl.load(std::memory_order_acquire);
  auto plan = domain::planDose(volume->value, currentVol, *cal);
  if (!plan.valid) {
    return makeErrorResponse("invalid_params");
  }

  uint32_t speedHz = domain::gSpeed.load(std::memory_order_acquire);
  if (speedMlMin && *speedMlMin > 0.1f) {
    speedHz = domain::speedMlMinToHz(domain::MlMin{*speedMlMin}, *cal).value;
  }

  domain::gBuretteState.store(domain::BuretteState::Dosing, std::memory_order_release);

  int32_t steps = static_cast<int32_t>(plan.firstCycleVolMl * cal->stepsPerMl + 0.5f);
  domain::gDirection.store(domain::Direction::LiqIn, std::memory_order_release);

  infrastructure::MotorCommand cmd{};
  cmd.type = infrastructure::MotorCommandType::MoveSteps;
  cmd.steps = steps;
  cmd.direction = domain::Direction::LiqIn;
  cmd.speedHz = speedHz;
  cmd.accelHzPerS = domain::gAccel.load(std::memory_order_acquire);
  sendMotorCommand(std::move(cmd));

  return makeAckThenResponse();
}

std::expected<CommandResponse, domain::AppError> handleRinse(
    std::optional<uint32_t> cycles) {
  if (!cycles || *cycles == 0) {
    return makeErrorResponse("invalid_params");
  }
  auto cal = infrastructure::storage::calibrationRead();
  if (!cal) return makeErrorResponse("start_failed");

  domain::gBuretteState.store(domain::BuretteState::Rinsing, std::memory_order_release);

  infrastructure::MotorCommand cmd{};
  cmd.type = infrastructure::MotorCommandType::StartRinse;
  cmd.startRinse.cycles = static_cast<uint8_t>(*cycles);
  cmd.startRinse.speedMlMin = 50.0f;
  sendMotorCommand(std::move(cmd));
  return makeAckThenResponse();
}

std::expected<CommandResponse, domain::AppError> handleCalRun(
    std::optional<std::string_view> mode,
    std::optional<float> freqHz,
    std::optional<float> speedMlMin) {
  if (!mode) {
    return makeErrorResponse("invalid_params");
  }

  auto cal = infrastructure::storage::calibrationRead();
  if (!cal) return makeErrorResponse("start_failed");

  auto state = domain::gBuretteState.load(std::memory_order_acquire);
  bool isBusy = (state != domain::BuretteState::Idle);

  uint16_t freq = (freqHz && *freqHz > 0)
      ? static_cast<uint16_t>(*freqHz + 0.5f) : 0;
  float speed = speedMlMin.value_or(0.0f);

  char modeBuf[8];
  modeBuf[0] = '\0';
  size_t modeLen = mode->size();
  if (modeLen > sizeof(modeBuf) - 1) modeLen = sizeof(modeBuf) - 1;
  std::memcpy(modeBuf, mode->data(), modeLen);
  modeBuf[modeLen] = '\0';

  auto plan = domain::sm::planCalRun(
      modeBuf, speed, freq, static_cast<float>(cal->maxFreqHz),
      cal->speedCoeff, isBusy);

  if (plan.action == domain::sm::CalRunAction::Reject) {
    const char* reason = "invalid_params";
    if (plan.rejectReason == domain::sm::CalRunRejectReason::BuretteBusy) {
      reason = "burette_busy";
    }
    return makeErrorResponse(reason);
  }

  domain::gBuretteState.store(domain::BuretteState::Dosing, std::memory_order_release);

  if (plan.action == domain::sm::CalRunAction::CalDose) {
    infrastructure::MotorCommand cmd{};
    cmd.type = infrastructure::MotorCommandType::StartCalDose;
    cmd.startCalDose.speedMlMin = plan.speedMlMin;
    sendMotorCommand(std::move(cmd));
  } else {
    infrastructure::MotorCommand cmd{};
    cmd.type = infrastructure::MotorCommandType::StartCalSpeed;
    cmd.startCalSpeed.speedMlMin = plan.speedMlMin;
    cmd.startCalSpeed.testFreqHz = plan.freqHz;
    sendMotorCommand(std::move(cmd));
  }

  return makeAckThenResponse();
}

std::expected<CommandResponse, domain::AppError> handleStop() {
  domain::gBuretteState.store(domain::BuretteState::Stopping,
                              std::memory_order_release);
  infrastructure::MotorCommand cmd{};
  cmd.type = infrastructure::MotorCommandType::Stop;
  sendMotorCommand(std::move(cmd));
  return makeSingleResponse(
      std::string_view(R"({"status":"stopped"})"),
      std::string_view(R"({"status":"stopped"})").size());
}

std::expected<CommandResponse, domain::AppError> handleEmergencyStop() {
  domain::gStopFull.store(true, std::memory_order_release);
  infrastructure::MotorCommand cmd{};
  cmd.type = infrastructure::MotorCommandType::EmergencyStop;
  sendMotorCommand(std::move(cmd));
  return makeSingleResponse(
      std::string_view(R"({"status":"emergency_stopped"})"),
      std::string_view(R"({"status":"emergency_stopped"})").size());
}

std::expected<CommandResponse, domain::AppError> handleGetStatus(
    domain::BuretteState state, int32_t tempCX100,
    domain::ValvePosition valvePos, float mv,
    domain::Direction dir, uint32_t speed,
    uint32_t accel, float volumeMl) {
  bool volumeIsNull = (state == domain::BuretteState::Homing);
  return makeStatusResponse(0, state, tempCX100, valvePos, mv,
                            dir, speed, accel, volumeMl, volumeIsNull);
}

std::expected<CommandResponse, domain::AppError> handleMoveSteps(
    std::optional<domain::Steps> steps) {
  if (!steps) {
    return makeErrorResponse("invalid_params");
  }
  infrastructure::MotorCommand cmd{};
  cmd.type = infrastructure::MotorCommandType::MoveSteps;
  cmd.steps = steps->value;
  sendMotorCommand(std::move(cmd));
  return makeAckThenResponse();
}

std::expected<CommandResponse, domain::AppError> handleSetDirection(
    std::optional<domain::Direction> dir) {
  if (!dir) {
    return makeErrorResponse("invalid_params");
  }
  domain::gDirection.store(*dir, std::memory_order_release);
  infrastructure::MotorCommand cmd{};
  cmd.type = infrastructure::MotorCommandType::SetDirection;
  cmd.direction = *dir;
  sendMotorCommand(std::move(cmd));
  return makeAckThenResponse();
}

std::expected<CommandResponse, domain::AppError> handleSetSpeed(
    std::optional<uint32_t> speedHz) {
  if (!speedHz) {
    return makeErrorResponse("invalid_params");
  }
  domain::gSpeed.store(*speedHz, std::memory_order_release);
  infrastructure::MotorCommand cmd{};
  cmd.type = infrastructure::MotorCommandType::SetSpeed;
  cmd.speedHz = *speedHz;
  sendMotorCommand(std::move(cmd));
  return makeAckThenResponse();
}

std::expected<CommandResponse, domain::AppError> handleSetAccel(
    std::optional<uint32_t> accelSteps) {
  if (!accelSteps) {
    return makeErrorResponse("invalid_params");
  }
  domain::gAccel.store(*accelSteps, std::memory_order_release);
  infrastructure::MotorCommand cmd{};
  cmd.type = infrastructure::MotorCommandType::SetAccel;
  cmd.accelHzPerS = *accelSteps;
  sendMotorCommand(std::move(cmd));
  return makeAckThenResponse();
}

} // namespace ecotiter::application::handlers::burette_ops
