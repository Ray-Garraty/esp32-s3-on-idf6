#include <catch2/catch_test_macros.hpp>
#include <domain/burette.hpp>
#include <domain/rinse_sm.hpp>
#include <domain/cal_dose_sm.hpp>
#include <domain/cal_speed_sm.hpp>
#include <domain/cal_run_planner.hpp>

using namespace ecotiter::domain;
using namespace ecotiter::domain::sm;

TEST_CASE("Burette starts in Idle", "[burette]") {
    BuretteController ctrl;
    REQUIRE(ctrl.state == BuretteState::Idle);
}

TEST_CASE("Fill from Idle succeeds", "[burette]") {
    BuretteController ctrl;
    auto result = ctrl.transition(BuretteCommand::Fill);
    REQUIRE(result);
    REQUIRE(ctrl.state == BuretteState::Filling);
}

TEST_CASE("Dose from Idle succeeds", "[burette]") {
    BuretteController ctrl;
    auto result = ctrl.transition(BuretteCommand::Dose);
    REQUIRE(result);
    REQUIRE(ctrl.state == BuretteState::Dosing);
}

TEST_CASE("Stop during active state succeeds", "[burette]") {
    BuretteController ctrl;
    std::ignore = ctrl.transition(BuretteCommand::Fill);
    auto result = ctrl.transition(BuretteCommand::Stop);
    REQUIRE(result);
    REQUIRE(ctrl.state == BuretteState::Stopping);
}

TEST_CASE("Stop from Idle fails", "[burette]") {
    BuretteController ctrl;
    auto result = ctrl.transition(BuretteCommand::Stop);
    REQUIRE(!result);
    REQUIRE(result.error() == StateError::InvalidTransition);
}

TEST_CASE("Fill when already busy fails", "[burette]") {
    BuretteController ctrl;
    std::ignore = ctrl.transition(BuretteCommand::Fill);
    auto result = ctrl.transition(BuretteCommand::Dose);
    REQUIRE(!result);
    REQUIRE(result.error() == StateError::Busy);
}

TEST_CASE("Reset only from Error", "[burette]") {
    BuretteController ctrl;
    auto result = ctrl.transition(BuretteCommand::Reset);
    REQUIRE(!result);
    REQUIRE(result.error() == StateError::InvalidTransition);
}

// ── Rinse SM tests ──────────────────────────────────────────────

TEST_CASE("RinseSm starts in PreFill when not full", "[rinse]") {
    RinseSm sm;
    sm.start(3, 0.0f, 8.14f);
    REQUIRE(sm.phase == RinseSm::Phase::PreFill);
    REQUIRE(sm.totalCycles == 3);
    REQUIRE(sm.currentCycle == 1);
}

TEST_CASE("RinseSm skips PreFill when near full", "[rinse]") {
    RinseSm sm;
    sm.start(3, 8.14f, 8.14f);
    REQUIRE(sm.phase == RinseSm::Phase::Emptying);
}

TEST_CASE("RinseSm PreFill transitions to EmptyToLimit", "[rinse]") {
    RinseSm sm;
    sm.start(3, 0.0f, 8.14f);
    auto action = sm.onMotorComplete(0.0f, 8.14f);
    REQUIRE(action == RinseAction::EmptyToLimit);
    REQUIRE(sm.phase == RinseSm::Phase::Emptying);
}

TEST_CASE("RinseSm Emptying transitions to FillToLimit", "[rinse]") {
    RinseSm sm;
    sm.start(3, 0.0f, 8.14f);
    std::ignore = sm.onMotorComplete(0.0f, 8.14f);
    auto action = sm.onMotorComplete(0.0f, 8.14f);
    REQUIRE(action == RinseAction::FillToLimit);
    REQUIRE(sm.phase == RinseSm::Phase::Filling);
}

TEST_CASE("RinseSm cycles through correct number of operations", "[rinse]") {
    RinseSm sm;
    sm.start(3, 0.0f, 8.14f);

    // PreFill -> EmptyToLimit
    auto a = sm.onMotorComplete(0.0f, 8.14f);
    REQUIRE(a == RinseAction::EmptyToLimit);
    REQUIRE(sm.phase == RinseSm::Phase::Emptying);

    // Emptying -> FillToLimit
    a = sm.onMotorComplete(0.0f, 8.14f);
    REQUIRE(a == RinseAction::FillToLimit);
    REQUIRE(sm.phase == RinseSm::Phase::Filling);

    // Filling -> cycle becomes 2, 2<=3, EmptyToLimit
    a = sm.onMotorComplete(8.14f, 8.14f);
    REQUIRE(a == RinseAction::EmptyToLimit);
    REQUIRE(sm.currentCycle == 2);
    REQUIRE(sm.phase == RinseSm::Phase::Filling);

    // Filling -> cycle becomes 3, 3<=3, EmptyToLimit
    a = sm.onMotorComplete(8.14f, 8.14f);
    REQUIRE(a == RinseAction::EmptyToLimit);
    REQUIRE(sm.currentCycle == 3);

    // Filling -> cycle becomes 4 > 3, Complete
    a = sm.onMotorComplete(8.14f, 8.14f);
    REQUIRE(a == RinseAction::Complete);
    REQUIRE(sm.phase == RinseSm::Phase::Done);
    REQUIRE(sm.isComplete());
}

TEST_CASE("RinseSm one cycle", "[rinse]") {
    RinseSm sm;
    sm.start(1, 0.0f, 8.14f);

    // PreFill -> EmptyToLimit
    auto a = sm.onMotorComplete(0.0f, 8.14f);
    REQUIRE(a == RinseAction::EmptyToLimit);

    // Emptying -> FillToLimit
    a = sm.onMotorComplete(0.0f, 8.14f);
    REQUIRE(a == RinseAction::FillToLimit);

    // Filling -> cycle becomes 2 > 1, Complete
    a = sm.onMotorComplete(8.14f, 8.14f);
    REQUIRE(a == RinseAction::Complete);
    REQUIRE(sm.isComplete());
}

TEST_CASE("RinseSm skip PreFill when full cycles correctly", "[rinse]") {
    RinseSm sm;
    sm.start(2, 8.14f, 8.14f);
    REQUIRE(sm.phase == RinseSm::Phase::Emptying);

    // Emptying -> FillToLimit
    auto a = sm.onMotorComplete(0.0f, 8.14f);
    REQUIRE(a == RinseAction::FillToLimit);
    REQUIRE(sm.phase == RinseSm::Phase::Filling);

    // Filling -> cycle becomes 2, 2<=2, EmptyToLimit
    a = sm.onMotorComplete(8.14f, 8.14f);
    REQUIRE(a == RinseAction::EmptyToLimit);

    // Filling -> cycle becomes 3 > 2, Complete
    a = sm.onMotorComplete(8.14f, 8.14f);
    REQUIRE(a == RinseAction::Complete);
    REQUIRE(sm.isComplete());
}

TEST_CASE("RinseSm isComplete returns false during operation", "[rinse]") {
    RinseSm sm;
    sm.start(3, 0.0f, 8.14f);
    REQUIRE_FALSE(sm.isComplete());
    std::ignore = sm.onMotorComplete(0.0f, 8.14f);
    REQUIRE_FALSE(sm.isComplete());
}

// ── CalDose SM tests ────────────────────────────────────────────

TEST_CASE("CalDoseSm start sets phase to Idle", "[cal_dose]") {
    CalDoseSm sm;
    sm.start();
    REQUIRE(sm.phase == CalDoseSm::Phase::Idle);
}

TEST_CASE("CalDoseSm onStart returns FillToLimit when not full", "[cal_dose]") {
    CalDoseSm sm;
    sm.start();
    auto a = sm.onStart(0.0f, 8.14f);
    REQUIRE(a == CalDoseAction::FillToLimit);
    REQUIRE(sm.phase == CalDoseSm::Phase::Filling);
}

TEST_CASE("CalDoseSm onStart skips fill when near full", "[cal_dose]") {
    CalDoseSm sm;
    sm.start();
    auto a = sm.onStart(8.1f, 8.14f);
    REQUIRE(a == CalDoseAction::EmptyToLimit);
    REQUIRE(sm.phase == CalDoseSm::Phase::Emptying);
}

TEST_CASE("CalDoseSm fill then empty records steps", "[cal_dose]") {
    CalDoseSm sm;
    sm.start();

    auto a = sm.onStart(0.0f, 8.14f);
    REQUIRE(a == CalDoseAction::FillToLimit);

    a = sm.onFillComplete(10000);
    REQUIRE(a == CalDoseAction::EmptyToLimit);
    REQUIRE(sm.phase == CalDoseSm::Phase::Emptying);
    REQUIRE(sm.stepsBefore == 10000);

    a = sm.onEmptyComplete(5000);
    REQUIRE(a == CalDoseAction::Complete);
    REQUIRE(sm.phase == CalDoseSm::Phase::Done);
    REQUIRE(sm.stepsTaken == 5000);
    REQUIRE(sm.isComplete());
}

TEST_CASE("CalDoseSm steps taken absolute value", "[cal_dose]") {
    CalDoseSm sm;
    sm.start();
    std::ignore = sm.onStart(0.0f, 8.14f);
    std::ignore = sm.onFillComplete(5000);

    // Empty moves CW, so position might increase
    auto a = sm.onEmptyComplete(15000);
    REQUIRE(a == CalDoseAction::Complete);
    REQUIRE(sm.stepsTaken == 10000);
}

// ── CalSpeedSingle SM tests ─────────────────────────────────────

TEST_CASE("CalSpeedSingleSm start sets phase Idle", "[cal_speed]") {
    CalSpeedSingleSm sm;
    sm.start();
    REQUIRE(sm.phase == CalSpeedSingleSm::Phase::Idle);
}

TEST_CASE("CalSpeedSingleSm fill then empty measures speed", "[cal_speed]") {
    CalSpeedSingleSm sm;
    sm.start();

    auto a = sm.onStart(0.0f, 8.14f);
    REQUIRE(a == CalSpeedAction::FillToLimit);

    a = sm.onFillComplete(1000);
    REQUIRE(a == CalSpeedAction::EmptyToLimit);
    REQUIRE(sm.elapsedMs == 1000);

    // Nominal 8.14ml emptied in 10000ms = 8.14 / (10000/60000) = 48.84 ml/min
    a = sm.onEmptyComplete(11000, 8.14f);
    REQUIRE(a == CalSpeedAction::Complete);
    REQUIRE(sm.elapsedMs == 10000);
    REQUIRE(sm.measuredSpeedMlMin > 48.0f);
    REQUIRE(sm.measuredSpeedMlMin < 49.0f);
    REQUIRE(sm.isComplete());
}

TEST_CASE("CalSpeedSingleSm skips fill when full", "[cal_speed]") {
    CalSpeedSingleSm sm;
    sm.start();
    auto a = sm.onStart(8.1f, 8.14f);
    REQUIRE(a == CalSpeedAction::EmptyToLimit);
    REQUIRE(sm.phase == CalSpeedSingleSm::Phase::Emptying);
}

// ── CalSpeedSeq SM tests ─────────────────────────────────────────

TEST_CASE("CalSpeedSeqSm start sets phase FillFirst", "[cal_speed_seq]") {
    CalSpeedSeqSm sm;
    uint16_t freqs[3] = {100, 500, 1000};
    sm.start(freqs);
    REQUIRE(sm.phase == CalSpeedSeqSm::Phase::FillFirst);
    REQUIRE(sm.seqIdx == 0);
    REQUIRE_FALSE(sm.isComplete());
}

TEST_CASE("CalSpeedSeqSm progresses through fill, settle, empty cycle", "[cal_speed_seq]") {
    CalSpeedSeqSm sm;
    uint16_t freqs[3] = {100, 500, 1000};
    sm.start(freqs);

    // FillFirst -> settle
    auto a = sm.onTick(0);
    REQUIRE(a == CalSpeedAction::SettleValve);
    REQUIRE(sm.phase == CalSpeedSeqSm::Phase::ValveSettleAfterFill);

    // Still settling
    a = sm.onTick(500);
    REQUIRE(a == CalSpeedAction::SettleValve);
    REQUIRE(sm.phase == CalSpeedSeqSm::Phase::ValveSettleAfterFill);

    // Settle complete -> empty
    a = sm.onTick(1000);
    REQUIRE(a == CalSpeedAction::EmptyToLimit);
    REQUIRE(sm.phase == CalSpeedSeqSm::Phase::Empty);

    // Empty complete -> settle after empty
    a = sm.onTick(55000);
    REQUIRE(a == CalSpeedAction::SettleValve);
    REQUIRE(sm.seqIdx == 1);
    REQUIRE(sm.results[0] > 0.0f);
}

TEST_CASE("CalSpeedSeqSm completes after 3 points", "[cal_speed_seq]") {
    CalSpeedSeqSm sm;
    uint16_t freqs[3] = {100, 500, 1000};
    sm.start(freqs);

    uint32_t t = 0;

    // Point 0: Fill -> settle -> empty
    auto a = sm.onTick(t);
    REQUIRE(a == CalSpeedAction::SettleValve);
    t = 1000;
    a = sm.onTick(t);
    REQUIRE(a == CalSpeedAction::EmptyToLimit);
    t = 55000;
    a = sm.onTick(t);
    REQUIRE(a == CalSpeedAction::SettleValve);

    // Point 1: settle -> fill -> settle -> empty
    t = 56000;
    a = sm.onTick(t);
    REQUIRE(a == CalSpeedAction::FillToLimit);
    t = 57000;
    a = sm.onTick(t);
    REQUIRE(a == CalSpeedAction::SettleValve);
    t = 58000;
    a = sm.onTick(t);
    REQUIRE(a == CalSpeedAction::EmptyToLimit);
    t = 113000;
    a = sm.onTick(t);
    REQUIRE(a == CalSpeedAction::SettleValve);
    REQUIRE(sm.seqIdx == 2);

    // Point 2: settle -> fill -> settle -> empty
    t = 114000;
    a = sm.onTick(t);
    REQUIRE(a == CalSpeedAction::FillToLimit);
    t = 115000;
    a = sm.onTick(t);
    REQUIRE(a == CalSpeedAction::SettleValve);
    t = 116000;
    a = sm.onTick(t);
    REQUIRE(a == CalSpeedAction::EmptyToLimit);
    t = 171000;
    a = sm.onTick(t);
    REQUIRE(a == CalSpeedAction::Complete);
    REQUIRE(sm.isComplete());
    REQUIRE(sm.seqIdx == 3);
}

// ── CalRunPlanner tests ─────────────────────────────────────────

TEST_CASE("planCalRun returns Reject for invalid mode", "[planner]") {
    auto p = planCalRun("invalid", 0.0f, 0, 3000.0f, 0.03052f, false);
    REQUIRE(p.action == CalRunAction::Reject);
    REQUIRE(p.rejectReason == CalRunRejectReason::InvalidMode);
}

TEST_CASE("planCalRun returns Reject when busy", "[planner]") {
    auto p = planCalRun("dose", 0.0f, 0, 3000.0f, 0.03052f, true);
    REQUIRE(p.action == CalRunAction::Reject);
    REQUIRE(p.rejectReason == CalRunRejectReason::BuretteBusy);
}

TEST_CASE("planCalRun dose mode default freq = maxFreq/2", "[planner]") {
    auto p = planCalRun("dose", 0.0f, 0, 3000.0f, 0.03052f, false);
    REQUIRE(p.action == CalRunAction::CalDose);
    REQUIRE(p.freqHz == 1500);
    REQUIRE(p.speedMlMin > 45.0f);
}

TEST_CASE("planCalRun dose mode with freq", "[planner]") {
    auto p = planCalRun("dose", 0.0f, 2000, 3000.0f, 0.03052f, false);
    REQUIRE(p.action == CalRunAction::CalDose);
    REQUIRE(p.freqHz == 2000);
}

TEST_CASE("planCalRun speed mode requires freq_hz", "[planner]") {
    auto p = planCalRun("speed", 0.0f, 0, 3000.0f, 0.03052f, false);
    REQUIRE(p.action == CalRunAction::Reject);
}

TEST_CASE("planCalRun speed mode", "[planner]") {
    auto p = planCalRun("speed", 0.0f, 2000, 3000.0f, 0.03052f, false);
    REQUIRE(p.action == CalRunAction::CalSpeed);
    REQUIRE(p.freqHz == 2000);
}

TEST_CASE("planCalRun speed mode with fill speed", "[planner]") {
    auto p = planCalRun("speed", 25.0f, 2000, 3000.0f, 0.03052f, false);
    REQUIRE(p.action == CalRunAction::CalSpeed);
    REQUIRE(p.speedMlMin == 25.0f);
}

TEST_CASE("planCalRun null mode returns invalid", "[planner]") {
    auto p = planCalRun(nullptr, 0.0f, 0, 3000.0f, 0.03052f, false);
    REQUIRE(p.action == CalRunAction::Reject);
}
