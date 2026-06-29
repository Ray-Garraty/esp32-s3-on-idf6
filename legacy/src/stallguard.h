/**
 * @file stallguard.h
 * @brief StallGuard4 logic for TMC2209
 *
 * Monitors SG_RESULT from TMC2209 and detects motor stall
 */

#ifndef STALLGUARD_H
#define STALLGUARD_H

#include <Arduino.h>
#include "config.h"
#include "stepper_drv.h"

// Filter count for false positive prevention
#define STALLGUARD_FILTER_COUNT 3

// Function declarations
bool stallguard_init(void);
bool stallguard_get_threshold(uint8_t* threshold);
bool stallguard_set_threshold(uint8_t threshold);
bool stallguard_read_result(uint16_t* sg_result);
bool stallguard_check_stall(void);

#endif // STALLGUARD_H
