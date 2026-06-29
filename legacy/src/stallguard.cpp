/**
 * @file stallguard.cpp
 * @brief StallGuard4 logic for stepper driver
 *
 * Monitors SG_RESULT from stepper driver and detects motor stall
 * Adapted from PICO version
 */

#include "stallguard.h"
#include "logger.h"
#include "stepper_drv.h"
#include "config.h"
#include <Preferences.h>

// Current threshold
static uint8_t sg_threshold = STALLGUARD_DEFAULT_THRESHOLD;

static const uint16_t SG_RESULT_MASK = 0x3FF;

// NVS namespace for stallguard
static Preferences stallguard_prefs;

// Filter counter for false positive prevention
static uint8_t consecutive_stall_count = 0;

/**
 * @brief Load threshold from NVS
 */
static bool stallguard_load_threshold(void) {
    stallguard_prefs.begin("stallguard", true);
    bool has_key = stallguard_prefs.isKey("threshold");
    if (has_key) {
        sg_threshold = stallguard_prefs.getUChar("threshold", STALLGUARD_DEFAULT_THRESHOLD);
    }
    stallguard_prefs.end();

    logger.info("StallGuard: threshold loaded from NVS: %u", sg_threshold);
    return has_key;
}

/**
 * @brief Save threshold to NVS
 */
static bool stallguard_save_threshold(void) {
    stallguard_prefs.begin("stallguard", false);
    stallguard_prefs.putUChar("threshold", sg_threshold);
    stallguard_prefs.end();

    logger.info("StallGuard: threshold saved to NVS: %u", sg_threshold);
    return true;
}

bool stallguard_init(void) {
    if (!stepperDrv_is_connected()) {
        logger.warn("Stepper driver not connected");
        return false;
    }

    // Load threshold from NVS (or use default if not found)
    stallguard_load_threshold();

    // Set initial threshold to driver
    uint32_t sgthrs = sg_threshold;
    if (!stepperDrv_write_register(STEPPER_DRV_REG_SGTHRS, sgthrs)) {
        logger.error("Failed to set initial threshold");
        return false;
    }

    // Set TCOOLTHRS to enable StallGuard at all speeds
    // TCOOLTHRS = 0 enables at all speeds
    if (!stepperDrv_write_register(STEPPER_DRV_REG_COOLCONF, 0x00000000)) {
        logger.error("Failed to configure COOLCONF");
        return false;
    }

    logger.info("Initialized (threshold=%d)", sg_threshold);
    return true;
}

bool stallguard_get_threshold(uint8_t* threshold) {
    *threshold = sg_threshold;
    return true;
}

bool stallguard_set_threshold(uint8_t threshold) {
    sg_threshold = threshold;

    if (!stepperDrv_write_register(STEPPER_DRV_REG_SGTHRS, threshold)) {
        logger.error("Failed to set threshold to %d", threshold);
        return false;
    }

    // Save to NVS
    stallguard_save_threshold();

    logger.info("Threshold set to %d", threshold);
    return true;
}

bool stallguard_read_result(uint16_t* sg_result) {
    uint32_t value;
    if (!stepperDrv_read_register(STEPPER_DRV_REG_SG_RESULT, &value)) {
        return false;
    }

    // SG_RESULT is 10-bit (0-1023)
    *sg_result = value & SG_RESULT_MASK;
    return true;
}

bool stallguard_check_stall(void) {
    // Check if motor is actually moving
    stepperDrv_drv_status_t drv_status;
    if (!stepperDrv_read_drv_status(&drv_status)) {
        return false;
    }

    // StallGuard only works when motor is moving
    if (drv_status.stst) {
        consecutive_stall_count = 0;  // Reset counter when motor is stopped
        return false;
    }

    uint16_t sg_result;
    if (!stallguard_read_result(&sg_result)) {
        return false;
    }

    // Stall when SG_RESULT <= threshold * 2
    if (sg_result <= sg_threshold * 2) {
        consecutive_stall_count++;
        
        // Filter: require consecutive stalls
        if (consecutive_stall_count >= STALLGUARD_FILTER_COUNT) {
            return true;
        }
    } else {
        consecutive_stall_count = 0;  // Reset counter
    }

    return false;
}
