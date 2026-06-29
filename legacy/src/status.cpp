#include "status.h"
#include "stepper.h"
#include "stepper_drv.h"
#include "stallguard.h"
#include "valve.h"
#include "temperature.h"
#include "adc.h"
#include "webserver.h"
#include "ble.h"
#include <ArduinoJson.h>
#include <WiFi.h>

void format_status_response_doc(JsonDocument& doc) {
    doc["ts"] = millis();

    temp_sensor_state_t temp_state = temperature_get_state();
    if (temp_state.isConnected && !isnan(temp_state.value)) {
        doc["temp"] = roundf(temp_state.value * 10.0f) / 10.0f;
    } else {
        doc["temp"] = nullptr;
    }

    doc["mv"] = adc_get_calibrated_mv();

    const char* vpos = valve_get_position();
    if (strcmp(vpos, "input") == 0) {
        doc["vlv"] = "in";
    } else if (strcmp(vpos, "output") == 0) {
        doc["vlv"] = "out";
    } else {
        doc["vlv"] = "unk";
    }

    stepper_state_t stepper_state;
    stepper_get_state(&stepper_state);

    JsonDocument brt;
    if (stepper_state.stall_detected) {
        brt["sts"] = "error";
    } else if (stepper_state.busy || g_pending.id != 0) {
        brt["sts"] = "working";
    } else {
        brt["sts"] = "idle";
    }
    if (stepper_is_homing()) {
        brt["vl"] = nullptr;
    } else {
        brt["vl"] = roundf(stepper_state.current_volume_ml * 100.0f) / 100.0f;
    }
    brt["spd"] = roundf(stepper_state.current_speed_ml_min * 10.0f) / 10.0f;
    doc["brt"] = brt;
}

String format_status_response(void) {
    JsonDocument doc;
    format_status_response_doc(doc);
    String json;
    serializeJson(doc, json);
    return json;
}

void format_debug_response_doc(JsonDocument& doc) {
    // ADC raw value
    doc["adc"]["raw_mv"] = adc_get_raw_mv();

    // Transport connection status
    doc["usbSerialConnected"] = false;
    doc["bleConnected"] = ble_is_client_connected();

    // Burette steps
    stepper_state_t stepper_state;
    stepper_get_state(&stepper_state);
    JsonDocument stepsDoc;
    stepsDoc["taken"] = stepper_get_actual_steps();
    doc["buretteSteps"] = stepsDoc;

    // Stepper driver status
    JsonDocument drv;
    drv["isConnected"] = stepperDrv_is_connected();

    if (stepperDrv_is_connected()) {
        stepperDrv_drv_status_t drv_status;
        if (stepperDrv_read_drv_status(&drv_status)) {
            drv["otpw"] = drv_status.otpw;
            drv["ot"] = drv_status.ot;
        }

        drv["motor"]["isMoving"] = stepper_state.busy;

        // StallGuard
        uint16_t sg_result = 0;
        stallguard_read_result(&sg_result);
        drv["motor"]["stallGuard"]["value"] = sg_result;
        drv["motor"]["stallGuard"]["isStalled"] = stallguard_check_stall();
        uint8_t sg_thr = 0;
        stallguard_get_threshold(&sg_thr);
        drv["motor"]["stallGuard"]["threshold"] = sg_thr;
    }

    doc["stepperDrv"] = drv;
}
