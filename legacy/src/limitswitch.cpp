#include "limitswitch.h"
#include "logger.h"
#include <ArduinoJson.h>

void limitswitch_init(void) {
    pinMode(PIN_LIMIT_FULL, INPUT_PULLDOWN);
    pinMode(PIN_LIMIT_EMPTY, INPUT_PULLDOWN);
    logger.debug("Limit switches initialized: FULL=GPIO%d, EMPTY=GPIO%d",
                 PIN_LIMIT_FULL, PIN_LIMIT_EMPTY);
}

limitswitch_state_t limitswitch_read(void) {
    limitswitch_state_t state;
    state.full = (digitalRead(PIN_LIMIT_FULL) == HIGH);
    state.empty = (digitalRead(PIN_LIMIT_EMPTY) == HIGH);
    return state;
}
