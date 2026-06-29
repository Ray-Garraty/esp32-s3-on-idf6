#include <unity.h>
#include <cmath>

// Include planner implementation directly — pure C++, no Arduino deps
#include "../../src/burette_planner.cpp"

void setUp(void) {}
void tearDown(void) {}

// ====================================================================
// plan_dose_volume — validation
// ====================================================================

void test_dose_reject_vol_zero(void) {
    DosePlan p = plan_dose_volume(0, 10, 3, 8.14f, false);
    TEST_ASSERT_EQUAL(DOSE_REJECT, p.action);
}

void test_dose_reject_speed_zero(void) {
    DosePlan p = plan_dose_volume(5, 0, 3, 8.14f, false);
    TEST_ASSERT_EQUAL(DOSE_REJECT, p.action);
}

void test_dose_reject_vol_too_small(void) {
    DosePlan p = plan_dose_volume(0.001f, 10, 3, 8.14f, false);
    TEST_ASSERT_EQUAL(DOSE_REJECT, p.action);
}

void test_dose_reject_vol_too_large(void) {
    DosePlan p = plan_dose_volume(51, 10, 3, 8.14f, false);
    TEST_ASSERT_EQUAL(DOSE_REJECT, p.action);
}

void test_dose_reject_speed_too_small(void) {
    DosePlan p = plan_dose_volume(5, 0.05f, 3, 8.14f, false);
    TEST_ASSERT_EQUAL(DOSE_REJECT, p.action);
}

void test_dose_reject_speed_too_large(void) {
    DosePlan p = plan_dose_volume(5, 25, 3, 8.14f, false);
    TEST_ASSERT_EQUAL(DOSE_REJECT, p.action);
}

void test_dose_reject_busy(void) {
    DosePlan p = plan_dose_volume(5, 10, 3, 8.14f, true);
    TEST_ASSERT_EQUAL(DOSE_REJECT, p.action);
}

// ====================================================================
// plan_dose_volume — the user's exact scenario
// ====================================================================

void test_dose_prefill_needed(void) {
    // 5ml dose, 3ml remaining, nominal=8.14, not busy
    DosePlan p = plan_dose_volume(5, 10, 3, 8.14f, false);
    TEST_ASSERT_EQUAL(DOSE_FILL_FIRST, p.action);
    TEST_ASSERT_EQUAL(1, p.total_cycles);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 5.0f, p.first_cycle_vol);
}

void test_dose_direct_enough(void) {
    // 5ml dose, 7ml remaining (enough)
    DosePlan p = plan_dose_volume(5, 10, 7, 8.14f, false);
    TEST_ASSERT_EQUAL(DOSE_DIRECT, p.action);
    TEST_ASSERT_EQUAL(1, p.total_cycles);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 5.0f, p.first_cycle_vol);
}

void test_dose_exact_match(void) {
    DosePlan p = plan_dose_volume(8.14f, 10, 8.14f, 8.14f, false);
    TEST_ASSERT_EQUAL(DOSE_DIRECT, p.action);
    TEST_ASSERT_EQUAL(1, p.total_cycles);
}

void test_dose_empty_burette(void) {
    DosePlan p = plan_dose_volume(5, 10, 0, 8.14f, false);
    TEST_ASSERT_EQUAL(DOSE_FILL_FIRST, p.action);
}

void test_dose_small_deficit(void) {
    DosePlan p = plan_dose_volume(5, 10, 4.999f, 8.14f, false);
    TEST_ASSERT_EQUAL(DOSE_FILL_FIRST, p.action);
}

// ====================================================================
// plan_dose_volume — multi-cycle
// ====================================================================

void test_dose_oversize_2_cycles(void) {
    DosePlan p = plan_dose_volume(12, 10, 8.14f, 8.14f, false);
    TEST_ASSERT_EQUAL(DOSE_DIRECT, p.action);
    TEST_ASSERT_EQUAL(2, p.total_cycles);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 8.14f, p.first_cycle_vol);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 3.86f, p.remaining_vol);
}

void test_dose_oversize_3_cycles(void) {
    DosePlan p = plan_dose_volume(20, 10, 8.14f, 8.14f, false);
    TEST_ASSERT_EQUAL(DOSE_DIRECT, p.action);
    TEST_ASSERT_EQUAL(3, p.total_cycles);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 8.14f, p.first_cycle_vol);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 3.72f, p.remaining_vol);
}

void test_dose_oversize_exact_multiple(void) {
    DosePlan p = plan_dose_volume(16.28f, 10, 8.14f, 8.14f, false);
    TEST_ASSERT_EQUAL(2, p.total_cycles);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 8.14f, p.remaining_vol);
}

void test_dose_direct_if_just_full(void) {
    // current == first_cycle_vol (8.14 == 8.14, single cycle)
    DosePlan p = plan_dose_volume(8.14f, 10, 8.14f, 8.14f, false);
    TEST_ASSERT_EQUAL(DOSE_DIRECT, p.action);
    TEST_ASSERT_EQUAL(1, p.total_cycles);
}

void test_dose_multi_cycle_prefill_needed(void) {
    // 12ml dose, 4ml current (less than 8.14 first cycle)
    DosePlan p = plan_dose_volume(12, 10, 4, 8.14f, false);
    TEST_ASSERT_EQUAL(DOSE_FILL_FIRST, p.action);
    TEST_ASSERT_EQUAL(2, p.total_cycles);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 8.14f, p.first_cycle_vol);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 3.86f, p.remaining_vol);
}

// ====================================================================
// plan_fill / plan_empty
// ====================================================================

void test_fill_ok(void) {
    SimplePlan p = plan_fill(10, false);
    TEST_ASSERT_EQUAL(SIMPLE_EXECUTE, p.action);
}

void test_fill_speed_zero(void) {
    SimplePlan p = plan_fill(0, false);
    TEST_ASSERT_EQUAL(SIMPLE_REJECT, p.action);
}

void test_fill_speed_too_low(void) {
    SimplePlan p = plan_fill(0.05f, false);
    TEST_ASSERT_EQUAL(SIMPLE_REJECT, p.action);
}

void test_fill_speed_too_high(void) {
    SimplePlan p = plan_fill(25, false);
    TEST_ASSERT_EQUAL(SIMPLE_REJECT, p.action);
}

void test_fill_busy(void) {
    SimplePlan p = plan_fill(10, true);
    TEST_ASSERT_EQUAL(SIMPLE_REJECT, p.action);
}

void test_empty_ok(void) {
    SimplePlan p = plan_empty(10, false);
    TEST_ASSERT_EQUAL(SIMPLE_EXECUTE, p.action);
}

void test_empty_speed_zero(void) {
    SimplePlan p = plan_empty(0, false);
    TEST_ASSERT_EQUAL(SIMPLE_REJECT, p.action);
}

void test_empty_busy(void) {
    SimplePlan p = plan_empty(10, true);
    TEST_ASSERT_EQUAL(SIMPLE_REJECT, p.action);
}

// ====================================================================
// plan_rinse
// ====================================================================

void test_rinse_ok(void) {
    RinsePlan p = plan_rinse(3, 10, false);
    TEST_ASSERT_EQUAL(SIMPLE_EXECUTE, p.action);
    TEST_ASSERT_EQUAL(3, p.cycles);
}

void test_rinse_cycles_zero(void) {
    RinsePlan p = plan_rinse(0, 10, false);
    TEST_ASSERT_EQUAL(SIMPLE_REJECT, p.action);
}

void test_rinse_speed_zero(void) {
    RinsePlan p = plan_rinse(3, 0, false);
    TEST_ASSERT_EQUAL(SIMPLE_REJECT, p.action);
}

void test_rinse_busy(void) {
    RinsePlan p = plan_rinse(3, 10, true);
    TEST_ASSERT_EQUAL(SIMPLE_REJECT, p.action);
}

// ====================================================================
// plan_cal_run
// ====================================================================

void test_cal_run_dose_default_freq(void) {
    CalRunPlan p = plan_cal_run("dose", 0, 0, 3000, 0.03052f, false);
    TEST_ASSERT_EQUAL(CAL_DOSE, p.action);
    TEST_ASSERT_EQUAL(1500, p.freq_hz);
}

void test_cal_run_dose_with_freq(void) {
    CalRunPlan p = plan_cal_run("dose", 0, 2000, 3000, 0.03052f, false);
    TEST_ASSERT_EQUAL(CAL_DOSE, p.action);
    TEST_ASSERT_EQUAL(2000, p.freq_hz);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 2000 * 0.03052f, p.speed_ml_min);
}

void test_cal_run_speed_no_freq(void) {
    CalRunPlan p = plan_cal_run("speed", 0, 0, 3000, 0.03052f, false);
    TEST_ASSERT_EQUAL(CAL_REJECT, p.action);
}

void test_cal_run_speed_ok(void) {
    CalRunPlan p = plan_cal_run("speed", 0, 2000, 3000, 0.03052f, false);
    TEST_ASSERT_EQUAL(CAL_SPEED, p.action);
    TEST_ASSERT_EQUAL(2000, p.freq_hz);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1500 * 0.03052f, p.speed_ml_min);
}

void test_cal_run_speed_with_fill_speed(void) {
    CalRunPlan p = plan_cal_run("speed", 25, 2000, 3000, 0.03052f, false);
    TEST_ASSERT_EQUAL(CAL_SPEED, p.action);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 25.0f, p.speed_ml_min);
}

void test_cal_run_invalid_mode(void) {
    CalRunPlan p = plan_cal_run("invalid", 0, 0, 3000, 0.03052f, false);
    TEST_ASSERT_EQUAL(CAL_REJECT, p.action);
}

void test_cal_run_busy(void) {
    CalRunPlan p = plan_cal_run("dose", 0, 0, 3000, 0.03052f, true);
    TEST_ASSERT_EQUAL(CAL_REJECT, p.action);
}

// ====================================================================
// plan_cal_speed_seq
// ====================================================================

void test_cal_speed_seq_ok(void) {
    CalSpeedSeqPlan p = plan_cal_speed_seq(3, 0, 3000, 0.03052f, false);
    TEST_ASSERT_EQUAL(SIMPLE_EXECUTE, p.action);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1500 * 0.03052f, p.fill_speed_ml_min);
}

void test_cal_speed_seq_wrong_count(void) {
    CalSpeedSeqPlan p = plan_cal_speed_seq(0, 0, 3000, 0.03052f, false);
    TEST_ASSERT_EQUAL(SIMPLE_REJECT, p.action);
}

void test_cal_speed_seq_count_2(void) {
    CalSpeedSeqPlan p = plan_cal_speed_seq(2, 0, 3000, 0.03052f, false);
    TEST_ASSERT_EQUAL(SIMPLE_REJECT, p.action);
}

void test_cal_speed_seq_busy(void) {
    CalSpeedSeqPlan p = plan_cal_speed_seq(3, 0, 3000, 0.03052f, true);
    TEST_ASSERT_EQUAL(SIMPLE_REJECT, p.action);
}

void test_cal_speed_seq_with_fill_speed(void) {
    CalSpeedSeqPlan p = plan_cal_speed_seq(3, 20, 3000, 0.03052f, false);
    TEST_ASSERT_EQUAL(SIMPLE_EXECUTE, p.action);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 20.0f, p.fill_speed_ml_min);
}

// ====================================================================
// calc_total_cycles / calc_remaining_vol
// ====================================================================

void test_calc_total_cycles_single(void) {
    TEST_ASSERT_EQUAL(1, calc_total_cycles(5, 8.14f));
}

void test_calc_total_cycles_double(void) {
    TEST_ASSERT_EQUAL(2, calc_total_cycles(12, 8.14f));
}

void test_calc_total_cycles_triple(void) {
    TEST_ASSERT_EQUAL(3, calc_total_cycles(20, 8.14f));
}

void test_calc_remaining_vol_normal(void) {
    float r = calc_remaining_vol(12, 8.14f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 3.86f, r);
}

void test_calc_remaining_vol_exact(void) {
    float r = calc_remaining_vol(16.28f, 8.14f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 8.14f, r);
}

void test_calc_remaining_vol_small_residual(void) {
    float r = calc_remaining_vol(16.285f, 8.14f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 8.14f, r);
}

// ====================================================================
// steps_to_volume / volume_to_steps (pure math, extracted from burette_cal.cpp)
// ====================================================================

static int32_t test_volume_to_steps(float volume_ml, float steps_per_ml) {
    if (volume_ml < 0) return 0;
    return lroundf(volume_ml * steps_per_ml);
}

static float test_steps_to_volume(int32_t steps, int32_t base_steps,
                                   float steps_per_ml, float nominal_vol) {
    if (steps_per_ml < 0.001f) return 0;
    int32_t delta = steps - base_steps;
    float vol = (float)delta / steps_per_ml;
    if (vol < 0) vol = 0;
    if (vol > nominal_vol) vol = nominal_vol;
    return vol;
}

void test_steps_to_vol_zero_spm(void) {
    float v = test_steps_to_volume(1000, 0, 0, 50);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0, v);
}

void test_steps_to_vol_zero_delta(void) {
    float v = test_steps_to_volume(5000, 5000, 100, 50);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0, v);
}

void test_steps_to_vol_positive_delta(void) {
    float v = test_steps_to_volume(7000, 5000, 100, 50);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 20.0f, v);
}

void test_steps_to_vol_negative_delta(void) {
    float v = test_steps_to_volume(3000, 5000, 100, 50);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0, v);
}

void test_steps_to_vol_clamp_nominal(void) {
    float v = test_steps_to_volume(15000, 5000, 100, 50);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 50.0f, v);
}

void test_steps_to_vol_fractional(void) {
    float v = test_steps_to_volume(5230, 5000, 100, 50);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 2.3f, v);
}

void test_steps_to_vol_at_full(void) {
    // base=FULL(5000), position back at FULL after refill → dispensed=0
    float v = test_steps_to_volume(5000, 5000, 100, 50);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0, v);
}

void test_vol_to_steps_normal(void) {
    int32_t s = test_volume_to_steps(5.0f, 1000);
    TEST_ASSERT_EQUAL(5000, s);
}

void test_vol_to_steps_zero(void) {
    int32_t s = test_volume_to_steps(0, 1000);
    TEST_ASSERT_EQUAL(0, s);
}

void test_vol_to_steps_negative(void) {
    int32_t s = test_volume_to_steps(-1, 1000);
    TEST_ASSERT_EQUAL(0, s);
}

void test_vol_to_steps_fractional(void) {
    int32_t s = test_volume_to_steps(5.5f, 1000);
    TEST_ASSERT_EQUAL(5500, s);
}

void test_vol_to_steps_large(void) {
    int32_t s = test_volume_to_steps(50.0f, 7730);
    TEST_ASSERT_EQUAL(386500, s);
}

void test_roundtrip_normal(void) {
    float vol = test_steps_to_volume(
        test_volume_to_steps(5.0f, 1000), 0, 1000, 50);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 5.0f, vol);
}

void test_roundtrip_zero(void) {
    float vol = test_steps_to_volume(
        test_volume_to_steps(0, 1000), 0, 1000, 50);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0, vol);
}

void test_roundtrip_nominal(void) {
    float vol = test_steps_to_volume(
        test_volume_to_steps(50.0f, 1000), 0, 1000, 50);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 50.0f, vol);
}

// ====================================================================
// Main
// ====================================================================

int main(void) {
    UNITY_BEGIN();

    // plan_dose_volume — validation
    RUN_TEST(test_dose_reject_vol_zero);
    RUN_TEST(test_dose_reject_speed_zero);
    RUN_TEST(test_dose_reject_vol_too_small);
    RUN_TEST(test_dose_reject_vol_too_large);
    RUN_TEST(test_dose_reject_speed_too_small);
    RUN_TEST(test_dose_reject_speed_too_large);
    RUN_TEST(test_dose_reject_busy);

    // plan_dose_volume — fill/direct decision
    RUN_TEST(test_dose_prefill_needed);
    RUN_TEST(test_dose_direct_enough);
    RUN_TEST(test_dose_exact_match);
    RUN_TEST(test_dose_empty_burette);
    RUN_TEST(test_dose_small_deficit);

    // plan_dose_volume — multi-cycle
    RUN_TEST(test_dose_oversize_2_cycles);
    RUN_TEST(test_dose_oversize_3_cycles);
    RUN_TEST(test_dose_oversize_exact_multiple);
    RUN_TEST(test_dose_direct_if_just_full);
    RUN_TEST(test_dose_multi_cycle_prefill_needed);

    // fill / empty
    RUN_TEST(test_fill_ok);
    RUN_TEST(test_fill_speed_zero);
    RUN_TEST(test_fill_speed_too_low);
    RUN_TEST(test_fill_speed_too_high);
    RUN_TEST(test_fill_busy);
    RUN_TEST(test_empty_ok);
    RUN_TEST(test_empty_speed_zero);
    RUN_TEST(test_empty_busy);

    // rinse
    RUN_TEST(test_rinse_ok);
    RUN_TEST(test_rinse_cycles_zero);
    RUN_TEST(test_rinse_speed_zero);
    RUN_TEST(test_rinse_busy);

    // cal_run
    RUN_TEST(test_cal_run_dose_default_freq);
    RUN_TEST(test_cal_run_dose_with_freq);
    RUN_TEST(test_cal_run_speed_no_freq);
    RUN_TEST(test_cal_run_speed_ok);
    RUN_TEST(test_cal_run_speed_with_fill_speed);
    RUN_TEST(test_cal_run_invalid_mode);
    RUN_TEST(test_cal_run_busy);

    // cal_speed_seq
    RUN_TEST(test_cal_speed_seq_ok);
    RUN_TEST(test_cal_speed_seq_wrong_count);
    RUN_TEST(test_cal_speed_seq_count_2);
    RUN_TEST(test_cal_speed_seq_busy);
    RUN_TEST(test_cal_speed_seq_with_fill_speed);

    // helpers
    RUN_TEST(test_calc_total_cycles_single);
    RUN_TEST(test_calc_total_cycles_double);
    RUN_TEST(test_calc_total_cycles_triple);
    RUN_TEST(test_calc_remaining_vol_normal);
    RUN_TEST(test_calc_remaining_vol_exact);
    RUN_TEST(test_calc_remaining_vol_small_residual);

    // steps_to_volume / volume_to_steps
    RUN_TEST(test_steps_to_vol_zero_spm);
    RUN_TEST(test_steps_to_vol_zero_delta);
    RUN_TEST(test_steps_to_vol_positive_delta);
    RUN_TEST(test_steps_to_vol_negative_delta);
    RUN_TEST(test_steps_to_vol_clamp_nominal);
    RUN_TEST(test_steps_to_vol_fractional);
    RUN_TEST(test_steps_to_vol_at_full);
    RUN_TEST(test_vol_to_steps_normal);
    RUN_TEST(test_vol_to_steps_zero);
    RUN_TEST(test_vol_to_steps_negative);
    RUN_TEST(test_vol_to_steps_fractional);
    RUN_TEST(test_vol_to_steps_large);
    RUN_TEST(test_roundtrip_normal);
    RUN_TEST(test_roundtrip_zero);
    RUN_TEST(test_roundtrip_nominal);

    return UNITY_END();
}
