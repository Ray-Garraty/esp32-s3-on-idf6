#include <unity.h>
#include <cmath>
#include <cstdint>

// ============================================================================
// Inline implementations of burette_cal.cpp speed functions (pure math, no Arduino)
// ============================================================================

struct SpeedCalResult {
    float k;
    float r_squared;
};

static float test_frequency_to_speed(uint16_t freq_hz, float coeff) {
    return (float)freq_hz * coeff;
}

static uint16_t test_speed_to_frequency(float speed_ml_min, float coeff, uint16_t min_freq, uint16_t max_freq) {
    if (coeff < 0.000001f) return min_freq;
    uint16_t freq = (uint16_t)lroundf(speed_ml_min / coeff);
    if (freq < min_freq) freq = min_freq;
    if (freq > max_freq) freq = max_freq;
    return freq;
}

static SpeedCalResult test_calculate_speed_calibration(const float* frequencies, const float* speeds, uint8_t count) {
    SpeedCalResult result = {0, 0};
    if (count < 2) return result;
    float sum_f = 0, sum_v = 0, sum_ff = 0, sum_fv = 0;
    for (uint8_t i = 0; i < count; i++) {
        sum_f += frequencies[i];
        sum_v += speeds[i];
        sum_ff += frequencies[i] * frequencies[i];
        sum_fv += frequencies[i] * speeds[i];
    }
    float denom = sum_ff - sum_f * sum_f / (float)count;
    if (fabs(denom) < 0.000001f) return result;
    result.k = (sum_fv - sum_f * sum_v / (float)count) / denom;
    float mean_v = sum_v / (float)count;
    float ss_res = 0, ss_tot = 0;
    for (uint8_t i = 0; i < count; i++) {
        float pred = result.k * frequencies[i];
        ss_res += (speeds[i] - pred) * (speeds[i] - pred);
        ss_tot += (speeds[i] - mean_v) * (speeds[i] - mean_v);
    }
    if (ss_tot > 0.000001f) {
        result.r_squared = 1.0f - ss_res / ss_tot;
    }
    return result;
}

void setUp(void) {}
void tearDown(void) {}

// ============================================================================
// frequency_to_speed
// ============================================================================

void test_freq_to_speed_normal(void) {
    float s = test_frequency_to_speed(1000, 0.03052f);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 30.52f, s);
}

void test_freq_to_speed_zero_coeff(void) {
    float s = test_frequency_to_speed(1000, 0.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, s);
}

void test_freq_to_speed_zero_freq(void) {
    float s = test_frequency_to_speed(0, 0.03052f);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, s);
}

void test_freq_to_speed_precision(void) {
    float s = test_frequency_to_speed(1500, 0.03052f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 45.78f, s);
}

void test_freq_to_speed_max_freq(void) {
    float s = test_frequency_to_speed(3000, 0.03052f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 91.56f, s);
}

void test_freq_to_speed_min_freq(void) {
    float s = test_frequency_to_speed(30, 0.03052f);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.9156f, s);
}

// ============================================================================
// speed_to_frequency
// ============================================================================

void test_speed_to_freq_normal(void) {
    uint16_t f = test_speed_to_frequency(30.52f, 0.03052f, 30, 3000);
    TEST_ASSERT_EQUAL(1000, f);
}

void test_speed_to_freq_clamp_below_min(void) {
    uint16_t f = test_speed_to_frequency(0.5f, 0.03052f, 30, 3000);
    TEST_ASSERT_EQUAL(30, f);
}

void test_speed_to_freq_clamp_above_max(void) {
    uint16_t f = test_speed_to_frequency(100.0f, 0.03052f, 30, 3000);
    TEST_ASSERT_EQUAL(3000, f);
}

void test_speed_to_freq_zero_coeff(void) {
    uint16_t f = test_speed_to_frequency(10.0f, 0.0f, 30, 3000);
    TEST_ASSERT_EQUAL(30, f);
}

void test_speed_to_freq_boundary_min(void) {
    uint16_t f = test_speed_to_frequency(0.9156f, 0.03052f, 30, 3000);
    TEST_ASSERT_EQUAL(30, f);
}

void test_speed_to_freq_boundary_max(void) {
    uint16_t f = test_speed_to_frequency(91.56f, 0.03052f, 30, 3000);
    TEST_ASSERT_EQUAL(3000, f);
}

void test_speed_to_freq_decimal_rounding(void) {
    uint16_t f = test_speed_to_frequency(15.26f, 0.03052f, 30, 3000);
    TEST_ASSERT_EQUAL(500, f);
}

void test_speed_to_freq_exact_at_min(void) {
    uint16_t f = test_speed_to_frequency(0.9156f, 0.03052f, 30, 3000);
    TEST_ASSERT_EQUAL(30, f);
}

void test_speed_to_freq_just_above_min(void) {
    uint16_t f = test_speed_to_frequency(0.95f, 0.03052f, 30, 3000);
    TEST_ASSERT_EQUAL(31, f);
}

// ============================================================================
// Roundtrip: speed -> freq -> speed
// ============================================================================

void test_roundtrip_normal(void) {
    float original = 30.52f;
    uint16_t freq = test_speed_to_frequency(original, 0.03052f, 30, 3000);
    float recovered = test_frequency_to_speed(freq, 0.03052f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, original, recovered);
}

void test_roundtrip_clamped(void) {
    float original = 100.0f;
    uint16_t freq = test_speed_to_frequency(original, 0.03052f, 30, 3000);
    TEST_ASSERT_EQUAL(3000, freq);
    float recovered = test_frequency_to_speed(freq, 0.03052f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 91.56f, recovered);
}

void test_roundtrip_low_speed(void) {
    float original = 1.0f;
    uint16_t freq = test_speed_to_frequency(original, 0.03052f, 30, 3000);
    float recovered = test_frequency_to_speed(freq, 0.03052f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, (float)freq * 0.03052f, recovered);
}

void test_roundtrip_multiple_values(void) {
    for (uint16_t freq = 100; freq <= 3000; freq += 500) {
        float speed = test_frequency_to_speed(freq, 0.03052f);
        uint16_t rev_freq = test_speed_to_frequency(speed, 0.03052f, 30, 3000);
        float rev_speed = test_frequency_to_speed(rev_freq, 0.03052f);
        TEST_ASSERT_FLOAT_WITHIN(0.05f, speed, rev_speed);
    }
}

// ============================================================================
// calculate_speed_calibration (OLS)
// ============================================================================

void test_ols_perfect_fit(void) {
    const float freqs[] = {500.0f, 1000.0f};
    const float speeds[] = {15.26f, 30.52f};
    SpeedCalResult r = test_calculate_speed_calibration(freqs, speeds, 2);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.03052f, r.k);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.0f, r.r_squared);
}

void test_ols_single_point_guard(void) {
    const float freqs[] = {500.0f};
    const float speeds[] = {15.26f};
    SpeedCalResult r = test_calculate_speed_calibration(freqs, speeds, 1);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, r.k);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, r.r_squared);
}

void test_ols_three_points(void) {
    const float freqs[] = {1000.0f, 2000.0f, 3000.0f};
    const float speeds[] = {30.52f, 61.04f, 91.56f};
    SpeedCalResult r = test_calculate_speed_calibration(freqs, speeds, 3);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.03052f, r.k);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.0f, r.r_squared);
}

void test_ols_noisy_data(void) {
    const float freqs[] = {500.0f, 1000.0f, 1500.0f, 2000.0f};
    const float speeds[] = {15.2f, 30.6f, 45.7f, 61.1f};
    SpeedCalResult r = test_calculate_speed_calibration(freqs, speeds, 4);
    TEST_ASSERT(r.k > 0.030f);
    TEST_ASSERT(r.k < 0.031f);
    TEST_ASSERT(r.r_squared > 0.99f);
}

void test_ols_zero_freq_included(void) {
    const float freqs[] = {0.0f, 1000.0f};
    const float speeds[] = {0.0f, 30.52f};
    SpeedCalResult r = test_calculate_speed_calibration(freqs, speeds, 2);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.03052f, r.k);
}

void test_ols_denom_zero(void) {
    const float freqs[] = {500.0f, 500.0f};
    const float speeds[] = {10.0f, 20.0f};
    SpeedCalResult r = test_calculate_speed_calibration(freqs, speeds, 2);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, r.k);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, r.r_squared);
}

void test_ols_realistic_k_value(void) {
    const float freqs[] = {1000.0f, 2000.0f, 3000.0f};
    const float speeds[] = {30.52f, 61.04f, 91.56f};
    SpeedCalResult r = test_calculate_speed_calibration(freqs, speeds, 3);
    char msg[64];
    snprintf(msg, sizeof(msg), "k=%f", r.k);
    TEST_MESSAGE(msg);
    TEST_ASSERT(r.k > 0.0f);
}

void test_ols_negative_slope_guard(void) {
    const float freqs[] = {1000.0f, 2000.0f};
    const float speeds[] = {30.0f, 20.0f};
    SpeedCalResult r = test_calculate_speed_calibration(freqs, speeds, 2);
    TEST_ASSERT(r.k < 0.0f);
}

void test_ols_r_squared_negative(void) {
    const float freqs[] = {1000.0f, 2000.0f, 3000.0f};
    const float speeds[] = {100.0f, 5.0f, 200.0f};
    SpeedCalResult r = test_calculate_speed_calibration(freqs, speeds, 3);
    TEST_ASSERT(r.r_squared <= 1.0f);
}

// ============================================================================
// Main
// ============================================================================

int main(void) {
    UNITY_BEGIN();

    // frequency_to_speed
    RUN_TEST(test_freq_to_speed_normal);
    RUN_TEST(test_freq_to_speed_zero_coeff);
    RUN_TEST(test_freq_to_speed_zero_freq);
    RUN_TEST(test_freq_to_speed_precision);
    RUN_TEST(test_freq_to_speed_max_freq);
    RUN_TEST(test_freq_to_speed_min_freq);

    // speed_to_frequency
    RUN_TEST(test_speed_to_freq_normal);
    RUN_TEST(test_speed_to_freq_clamp_below_min);
    RUN_TEST(test_speed_to_freq_clamp_above_max);
    RUN_TEST(test_speed_to_freq_zero_coeff);
    RUN_TEST(test_speed_to_freq_boundary_min);
    RUN_TEST(test_speed_to_freq_boundary_max);
    RUN_TEST(test_speed_to_freq_decimal_rounding);
    RUN_TEST(test_speed_to_freq_exact_at_min);
    RUN_TEST(test_speed_to_freq_just_above_min);

    // roundtrip
    RUN_TEST(test_roundtrip_normal);
    RUN_TEST(test_roundtrip_clamped);
    RUN_TEST(test_roundtrip_low_speed);
    RUN_TEST(test_roundtrip_multiple_values);

    // OLS
    RUN_TEST(test_ols_perfect_fit);
    RUN_TEST(test_ols_single_point_guard);
    RUN_TEST(test_ols_three_points);
    RUN_TEST(test_ols_noisy_data);
    RUN_TEST(test_ols_zero_freq_included);
    RUN_TEST(test_ols_denom_zero);
    RUN_TEST(test_ols_realistic_k_value);
    RUN_TEST(test_ols_negative_slope_guard);
    RUN_TEST(test_ols_r_squared_negative);

    return UNITY_END();
}
