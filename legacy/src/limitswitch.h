#ifndef LIMITSWITCH_H
#define LIMITSWITCH_H

#include <Arduino.h>
#include "config.h"

/** @brief State of both limit switches */
typedef struct {
    bool full;
    bool empty;
} limitswitch_state_t;

/** @brief Init limit switch GPIOs (INPUT_PULLDOWN) */
void limitswitch_init(void);
/** @brief Read both limit switches, return state struct */
limitswitch_state_t limitswitch_read(void);
#endif
