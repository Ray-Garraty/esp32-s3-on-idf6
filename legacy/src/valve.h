#ifndef VALVE_H
#define VALVE_H

#include <Arduino.h>

#define PIN_VALVE 14

#define VALVE_POSITION_INPUT "input"
#define VALVE_POSITION_OUTPUT "output"

/** @brief Init valve GPIO (default: INPUT position) */
bool valve_init(void);
/** @brief Set valve position: "input" (LOW) or "output" (HIGH) */
bool valve_set_position(const char* position);
/** @brief Get current position string ("input" / "output") */
const char* valve_get_position(void);
/** @brief Serialize valve state to JSON buffer */
bool valve_get_state_json(char* buffer, size_t buffer_size);

#endif
