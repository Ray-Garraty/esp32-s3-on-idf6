/**
 * @file command.h
 * @brief JSON command execution module
 *
 * Handles JSON commands from any source:
 * - Web API (/api/command)
 * - USB Serial (debugging)
 * - etc.
 */

#ifndef COMMAND_H
#define COMMAND_H

#include <Arduino.h>

/**
 * @brief Execute JSON command and return response
 *
 * @param json_str JSON command string (e.g. {"id":1,"cmd":"system.getStatus"})
 * @param success pointer to bool that will be set to true/false
 * @return JSON response string
 */
String execute_json_command(const String& json_str, bool* success);

#endif // COMMAND_H