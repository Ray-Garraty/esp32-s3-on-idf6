#include "valve.h"
#include "logger.h"
#include <atomic>

// Current position (atomic for cross-task read)
static std::atomic<const char*> current_position{VALVE_POSITION_INPUT};

bool valve_init(void) {
    pinMode(PIN_VALVE, OUTPUT);
    digitalWrite(PIN_VALVE, LOW);  // Default to input position
    current_position.store(VALVE_POSITION_INPUT, std::memory_order_relaxed);

    logger.info("Valve Initialized (GPIO14)");
    return true;
}

bool valve_set_position(const char* position) {
    if (strcmp(position, VALVE_POSITION_INPUT) == 0) {
        digitalWrite(PIN_VALVE, LOW);
        current_position.store(VALVE_POSITION_INPUT, std::memory_order_relaxed);
        logger.info("Valve set to INPUT");
        return true;
    } else if (strcmp(position, VALVE_POSITION_OUTPUT) == 0) {
        digitalWrite(PIN_VALVE, HIGH);
        current_position.store(VALVE_POSITION_OUTPUT, std::memory_order_relaxed);
        logger.info("Valve set to OUTPUT");
        return true;
    }

    logger.warn("Invalid valve position: %s", position);
    return false;
}

const char* valve_get_position(void) {
    return current_position.load(std::memory_order_relaxed);
}

bool valve_get_state_json(char* buffer, size_t buffer_size) {
    const char* pos = current_position.load(std::memory_order_relaxed);
    int written = snprintf(buffer, buffer_size,
                          "{\"position\":\"%s\"}",
                          pos);
    
    if (written > 0 && (size_t)written < buffer_size) {
        return true;
    }
    return false;
}
