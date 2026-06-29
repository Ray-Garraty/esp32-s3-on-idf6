#include "command.h"
#include "handlers/handlers.h"
#include "handlers/common.h"
#include "logger.h"
#include <ArduinoJson.h>
#include <cstring>

typedef struct {
    const char* cmd;
    cmd_handler_t handler;
} cmd_entry_t;

static const cmd_entry_t cmd_table[] = {
    {"burette.doseVolume",      handle_dose_volume},
    {"burette.fill",            handle_fill},
    {"burette.empty",           handle_empty},
    {"burette.rinse",           handle_rinse},
    {"burette.stop",            handle_burette_stop},
    {"burette.emergencyStop",   handle_emergency_stop},
    {"burette.moveSteps",       handle_burette_move_steps},
    {"burette.moveToStop",      handle_burette_move_to_stop},
    {"burette.setDirection",    handle_burette_set_direction},
    {"burette.getStatus",       handle_burette_get_status},
    {"burette.cal.get",         handle_burette_cal_get},
    {"burette.cal.calcVolume",  handle_burette_cal_calc_volume},
    {"burette.cal.calcSpeed",   handle_burette_cal_calc_speed},
    {"burette.cal.save",        handle_burette_cal_save},
    {"burette.cal.reset",       handle_burette_cal_reset},
    {"burette.cal.run",         handle_burette_cal_run},
    {"burette.cal.runSpeedSeq", handle_burette_cal_run_speed_seq},
    {"burette.cal.getResult",   handle_burette_cal_get_result},
    {"valve.setPosition",       handle_valve_set_position},
    {"valve.getState",          handle_valve_get_state},
    {"temperature.read",        handle_temp_read},
    {"stallGuard.getThreshold",  handle_sg_get_threshold},
    {"stallGuard.setThreshold",  handle_sg_set_threshold},
    {"system.getStatus",        handle_system_get_status},
    {"system.getFormattedLogs", handle_get_formatted_logs},
    {"system.readLog",          handle_system_read_log},
    {"adc.cal.get",             handle_adc_cal_get},
    {"adc.cal.measure",         handle_adc_cal_measure},
    {"adc.cal.compute",         handle_adc_cal_compute},
    {"adc.cal.save",            handle_adc_cal_save},
    {"adc.cal.reset",           handle_adc_cal_reset},
    {"serial.ping",             handle_serial_ping},
};

static const size_t CMD_TABLE_SIZE = sizeof(cmd_table) / sizeof(cmd_table[0]);

static cmd_handler_t find_handler(const char* cmd) {
    for (size_t i = 0; i < CMD_TABLE_SIZE; i++) {
        if (strcmp(cmd, cmd_table[i].cmd) == 0) {
            return cmd_table[i].handler;
        }
    }
    return nullptr;
}

String execute_json_command(const String& json_str, bool* success) {
    char response_buf[1024];
    const size_t buf_size = sizeof(response_buf);

    logger.debug("cmd_rx: %s", json_str.c_str());

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json_str);

    if (err) {
        *success = false;
        logger.error("cmd_parse_error: %s", err.c_str());
        make_response_error(response_buf, buf_size, 0, "Invalid JSON");
        return String(response_buf);
    }

    uint64_t id = doc["id"].as<uint64_t>();

    const char* cmd = doc["cmd"];
    if (!cmd) {
        *success = false;
        logger.error("cmd_missing_field: cmd");
        make_response_error(response_buf, buf_size, id, "Missing cmd field");
        return String(response_buf);
    }

    logger.debug("cmd_dispatch: %s", cmd);

    cmd_handler_t handler = find_handler(cmd);
    logger.debug("cmd_handler: %s -> %p", cmd, handler);

    if (handler) {
        return handler(doc, id, success, response_buf, buf_size);
    }

    *success = false;
    logger.warn("cmd_unknown: %s", cmd);
    make_response_error(response_buf, buf_size, id, "Unknown command");
    return String(response_buf);
}
