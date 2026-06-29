#ifndef STATUS_H
#define STATUS_H

#include <ArduinoJson.h>

/** @brief Fill JsonDocument with compact status (temp, mv, vlv, brt) */
void format_status_response_doc(JsonDocument& doc);
/** @brief Return compact status as JSON string */
String format_status_response(void);
/** @brief Fill JsonDocument with debug info (ADC, driver, StallGuard) */
void format_debug_response_doc(JsonDocument& doc);

#endif
