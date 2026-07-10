#include "application/handlers/burette_ops.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "application/command.hpp"
#include "domain/calibration.hpp"
#include "domain/memory.hpp"
#include "domain/types.hpp"
#include "infrastructure/motor_task.hpp"
#include "infrastructure/storage/nvs.hpp"

namespace ecotiter::application::handlers::burette_ops {
namespace {

using Buf = domain::memory::ResponseBuffer;

CommandResponse makeCmdResponse(const char* cmdName,
                                const char* fmt = nullptr, ...) {
  CommandResponse rsp;
  rsp.kind = ResponseKind::Single;
  size_t off = 0;
  auto& buf = rsp.body;
  off = static_cast<size_t>(
      std::snprintf(buf.data(), buf.size(), R"({"cmd":"%s")", cmdName));
  if (fmt) {
    va_list args;
    va_start(args, fmt);
    int n = std::vsnprintf(buf.data() + off, buf.size() - off, fmt, args);
    va_end(args);
    if (n > 0) off += static_cast<size_t>(n);
  }
  if (off < buf.size() - 1) {
    buf[off++] = '}';
  }
  if (off < buf.size()) {
    buf[off] = '\0';
  }
  rsp.bodySize = off;
  return rsp;
}

bool sendMotorCommand(infrastructure::MotorCommand&& cmd) {
  if (infrastructure::gMotorCmdQueue == nullptr) return false;
  return xQueueSend(infrastructure::gMotorCmdQueue, &cmd, pdMS_TO_TICKS(10)) == pdTRUE;
}

} // anonymous namespace

std::expected<CommandResponse, domain::AppError> handleFill() {
  domain::gBuretteState.store(domain::BuretteState::Filling, std::memory_order_release);

  auto cal = infrastructure::storage::calibrationRead();
  if (!cal) return makeErrorResponse("calibration not available");

  int32_t steps = static_cast<int32_t>(cal->nominalVolumeMl * cal->stepsPerMl + 0.5f);
  domain::gDirection.store(domain::Direction::Cw, std::memory_order_release);

  infrastructure::MotorCommand cmd{};
  cmd.type = infrastructure::MotorCommandType::MoveSteps;
  cmd.steps = steps;
  cmd.direction = domain::Direction::Cw;
  cmd.speedHz = domain::gSpeed.load(std::memory_order_acquire);
  cmd.accelHzPerS = domain::gAccel.load(std::memory_order_acquire);
  sendMotorCommand(std::move(cmd));
  return makeAckThenResponse();
}

std::expected<CommandResponse, domain::AppError> handleEmpty() {
  domain::gBuretteState.store(domain::BuretteState::Emptying, std::memory_order_release);

  auto cal = infrastructure::storage::calibrationRead();
  if (!cal) return makeErrorResponse("calibration not available");

  float curVol = domain::gVolumeMl.load(std::memory_order_acquire);
  int32_t steps = static_cast<int32_t>(curVol * cal->stepsPerMl + 0.5f);
  if (steps < 10) steps = static_cast<int32_t>(cal->nominalVolumeMl * cal->stepsPerMl + 0.5f);
  domain::gDirection.store(domain::Direction::Ccw, std::memory_order_release);

  infrastructure::MotorCommand cmd{};
  cmd.type = infrastructure::MotorCommandType::MoveSteps;
  cmd.steps = steps;
  cmd.direction = domain::Direction::Ccw;
  cmd.speedHz = domain::gSpeed.load(std::memory_order_acquire);
  cmd.accelHzPerS = domain::gAccel.load(std::memory_order_acquire);
  sendMotorCommand(std::move(cmd));
  return makeAckThenResponse();
}

std::expected<CommandResponse, domain::AppError> handleDoseVolume(
    std::optional<domain::Ml> volume,
    std::optional<float> speedMlMin) {
  if (!volume) {
    return makeErrorResponse("doseVolume requires 'volume' param");
  }
  auto cal = infrastructure::storage::calibrationRead();
  if (!cal) return makeErrorResponse("calibration not available");

  float currentVol = domain::gVolumeMl.load(std::memory_order_acquire);
  auto plan = domain::planDose(volume->value, currentVol, *cal);
  if (!plan.valid) {
    return makeErrorResponse("volume out of range [0.01, 50.0]");
  }

  uint32_t speedHz = domain::gSpeed.load(std::memory_order_acquire);
  if (speedMlMin && *speedMlMin > 0.1f) {
    speedHz = domain::speedMlMinToHz(domain::MlMin{*speedMlMin}, *cal).value;
  }

  domain::gBuretteState.store(domain::BuretteState::Dosing, std::memory_order_release);

  int32_t steps = static_cast<int32_t>(plan.firstCycleVolMl * cal->stepsPerMl + 0.5f);
  domain::gDirection.store(domain::Direction::Cw, std::memory_order_release);

  infrastructure::MotorCommand cmd{};
  cmd.type = infrastructure::MotorCommandType::MoveSteps;
  cmd.steps = steps;
  cmd.direction = domain::Direction::Cw;
  cmd.speedHz = speedHz;
  cmd.accelHzPerS = domain::gAccel.load(std::memory_order_acquire);
  sendMotorCommand(std::move(cmd));

  return makeAckThenResponse();
}

std::expected<CommandResponse, domain::AppError> handleRinse(
    std::optional<uint32_t> cycles) {
  (void)cycles;
  auto cal = infrastructure::storage::calibrationRead();
  if (!cal) return makeErrorResponse("calibration not available");

  domain::gBuretteState.store(domain::BuretteState::Rinsing, std::memory_order_release);

  int32_t steps = static_cast<int32_t>(cal->nominalVolumeMl * cal->stepsPerMl + 0.5f);
  domain::gDirection.store(domain::Direction::Cw, std::memory_order_release);

  infrastructure::MotorCommand cmd{};
  cmd.type = infrastructure::MotorCommandType::MoveSteps;
  cmd.steps = steps;
  cmd.direction = domain::Direction::Cw;
  cmd.speedHz = domain::gSpeed.load(std::memory_order_acquire);
  cmd.accelHzPerS = domain::gAccel.load(std::memory_order_acquire);
  sendMotorCommand(std::move(cmd));
  return makeAckThenResponse();
}

std::expected<CommandResponse, domain::AppError> handleStop() {
  domain::gBuretteState.store(domain::BuretteState::Stopping,
                              std::memory_order_release);
  infrastructure::MotorCommand cmd{};
  cmd.type = infrastructure::MotorCommandType::Stop;
  sendMotorCommand(std::move(cmd));
  return makeAckThenResponse();
}

std::expected<CommandResponse, domain::AppError> handleEmergencyStop() {
  domain::gStopFull.store(true, std::memory_order_release);
  infrastructure::MotorCommand cmd{};
  cmd.type = infrastructure::MotorCommandType::EmergencyStop;
  sendMotorCommand(std::move(cmd));
  return makeAckThenResponse();
}

std::expected<CommandResponse, domain::AppError> handleGetStatus(
    domain::BuretteState state, int32_t tempCX100,
    domain::ValvePosition valvePos, float mv,
    domain::Direction dir, uint32_t speed,
    uint32_t accel, float volumeMl) {
  CommandResponse rsp;
  rsp.kind = ResponseKind::Single;
  size_t off = 0;
  bool volumeIsNull = (state == domain::BuretteState::Homing);
  serializeStatusJson(rsp.body, off, state, tempCX100,
                      valvePos, mv, dir, speed, accel, volumeMl, volumeIsNull);
  rsp.bodySize = off;
  return rsp;
}

std::expected<CommandResponse, domain::AppError> handleMoveSteps(
    std::optional<domain::Steps> steps) {
  if (!steps) {
    return makeErrorResponse("moveSteps requires 'steps' param");
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
    return makeErrorResponse("setDirection requires 'direction' param");
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
    return makeErrorResponse("setSpeed requires 'speed' param");
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
    return makeErrorResponse("setAccel requires 'accel' param");
  }
  domain::gAccel.store(*accelSteps, std::memory_order_release);
  infrastructure::MotorCommand cmd{};
  cmd.type = infrastructure::MotorCommandType::SetAccel;
  cmd.accelHzPerS = *accelSteps;
  sendMotorCommand(std::move(cmd));
  return makeAckThenResponse();
}

std::expected<CommandResponse, domain::AppError> handleSetVolume(
    std::optional<domain::Ml> volume) {
  if (!volume) {
    return makeErrorResponse("setVolume requires 'targetVolume' param");
  }
  return makeCmdResponse("setVolume",
                         R"(,"volume":%.1f)",
                         static_cast<double>(volume->value));
}

std::expected<CommandResponse, domain::AppError> handleConfigMove(
    std::optional<uint32_t> speed, std::optional<uint32_t> accel) {
  CommandResponse rsp;
  rsp.kind = ResponseKind::Single;
  size_t off = static_cast<size_t>(
      std::snprintf(rsp.body.data(), rsp.body.size(),
                    R"({"cmd":"configMove")"));
  if (speed) {
    off += static_cast<size_t>(
        std::snprintf(rsp.body.data() + off, rsp.body.size() - off,
                      R"(,"speed":%lu)", static_cast<unsigned long>(*speed)));
  }
  if (accel) {
    off += static_cast<size_t>(
        std::snprintf(rsp.body.data() + off, rsp.body.size() - off,
                      R"(,"accel":%lu)", static_cast<unsigned long>(*accel)));
  }
  if (off < rsp.body.size() - 1) {
    rsp.body[off++] = '}';
  }
  rsp.bodySize = off;
  return rsp;
}

std::expected<CommandResponse, domain::AppError> handleConfigHome(
    std::optional<uint32_t> speed) {
  if (!speed) {
    return makeErrorResponse("configHome requires 'homeSpeed' param");
  }
  return makeCmdResponse("configHome",
                         R"(,"homeSpeed":%lu)",
                         static_cast<unsigned long>(*speed));
}

std::expected<CommandResponse, domain::AppError> handleConfigSensor(
    std::optional<uint32_t> value) {
  if (!value) {
    return makeErrorResponse("configSensor requires 'sensorValue' param");
  }
  return makeCmdResponse("configSensor",
                         R"(,"sensorValue":%lu)",
                         static_cast<unsigned long>(*value));
}

} // namespace ecotiter::application::handlers::burette_ops
