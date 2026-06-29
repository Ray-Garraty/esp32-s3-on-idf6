/**
 * @file stepperDrv.cpp
 * @brief TMCStepper library interface for TMC2209/2225
 */

#include "stepper_drv.h"
#include "logger.h"
#include "config.h"
#include <HardwareSerial.h>
#include <atomic>

static HardwareSerial stepperSerial(2);
TMC2209Stepper stepperDriver(&stepperSerial, R_SENSE, DRIVER_ADDRESS);

static std::atomic<bool> driver_connected{false};

static const uint16_t DRV_INIT_DELAY_MS = 100;
static const uint8_t DRV_BLANK_TIME = 24;
static const uint16_t DRV_RMS_CURRENT_MA = 800;
static const uint8_t DRV_COOLSTEP_SEMIN = 5;
static const uint8_t DRV_COOLSTEP_SEMAX = 2;
static const uint8_t DRV_MICROSTEP_RES = 4;
static const uint16_t DRV_PUSH_DELAY_MS = 10;

bool stepperDrv_init(void) {
    logger.info("StepperDrv: Init Start (TMCStepper)");

    stepperSerial.begin(STEPPER_BAUD_RATE, SERIAL_8N1, STEPPER_UART_RX, STEPPER_UART_TX);
    logger.info("StepperDrv: Serial2 begun RX=%d TX=%d at %d baud", STEPPER_UART_RX, STEPPER_UART_TX, STEPPER_BAUD_RATE);

    vTaskDelay(pdMS_TO_TICKS(DRV_INIT_DELAY_MS));
    stepperDriver.begin();
    logger.info("StepperDrv: begin() done");

    stepperDriver.pdn_disable(true);
    logger.info("StepperDrv: pdn_disable(true)");

    stepperDriver.toff(TMC_TOFF_DEFAULT);
    logger.info("StepperDrv: toff(%d)", TMC_TOFF_DEFAULT);

    stepperDriver.blank_time(DRV_BLANK_TIME);
    logger.info("StepperDrv: blank_time(%d)", DRV_BLANK_TIME);

    vTaskDelay(pdMS_TO_TICKS(TMC_INIT_DELAY_MS));

    logger.info("StepperDrv: Running test_connection()...");
    uint8_t conn_result = stepperDriver.test_connection();
    logger.info("StepperDrv: test_connection()=%u", conn_result);

    if (conn_result == 0) {
        driver_connected.store(true, std::memory_order_relaxed);
        logger.info("StepperDrv: Connection OK");

        uint8_t ver = stepperDriver.version();
        uint32_t gconf = stepperDriver.GCONF();
        uint32_t ioin = stepperDriver.IOIN();
        logger.info("StepperDrv: Version=0x%02X GCONF=0x%08X IOIN=0x%08X", ver, gconf, ioin);
    } else {
        logger.error("StepperDrv: Connection failed! Code=%u", conn_result);
        if (conn_result == 1) logger.error("StepperDrv: Check: 12/24V on VM, resistor 1kOhm, RX/TX pins");
        if (conn_result == 2) logger.error("StepperDrv: Check: driver power (VM) and EN pin");
        driver_connected.store(false, std::memory_order_relaxed);
    }

    logger.info("StepperDrv: Init End");
    return driver_connected.load(std::memory_order_relaxed);
}

bool stepperDrv_read_register(uint8_t reg, uint32_t* value) {
    if (!driver_connected.load(std::memory_order_relaxed) || !value) return false;

    switch (reg) {
        case STEPPER_DRV_REG_IOIN:
            *value = stepperDriver.IOIN();
            return true;
        case STEPPER_DRV_REG_SG_RESULT:
            *value = stepperDriver.SG_RESULT();
            return true;
        default:
            logger.warn("StepperDrv: No public method for reg 0x%02X", reg);
            return false;
    }
}

bool stepperDrv_write_register(uint8_t reg, uint32_t value) {
    if (!driver_connected.load(std::memory_order_relaxed)) return false;

    switch (reg) {
        case STEPPER_DRV_REG_SGTHRS:
            stepperDriver.SGTHRS(value);
            return true;
        case STEPPER_DRV_REG_COOLCONF:
            stepperDriver.COOLCONF(value);
            return true;
        default:
            logger.warn("StepperDrv: No public method for writing reg 0x%02X", reg);
            return false;
    }
}

bool stepperDrv_configure_stealthchop(void) {
    if (!driver_connected.load(std::memory_order_relaxed)) return false;

    logger.info("StepperDrv: Configuring StealthChop...");

    stepperDriver.rms_current(DRV_RMS_CURRENT_MA);
    logger.info("StepperDrv: rms_current(%dmA)", DRV_RMS_CURRENT_MA);

    stepperDriver.blank_time(DRV_BLANK_TIME);
    logger.info("StepperDriver: blank_time(%d)", DRV_BLANK_TIME);

    stepperDriver.TCOOLTHRS(0xFFFFF);
    logger.info("StepperDrv: TCOOLTHRS(0xFFFFF)");

    stepperDriver.semin(DRV_COOLSTEP_SEMIN);
    stepperDriver.semax(DRV_COOLSTEP_SEMAX);
    stepperDriver.sedn(0b01);
    logger.info("StepperDrv: coolstep configured");

    stepperDriver.pdn_disable(true);
    stepperDriver.I_scale_analog(true);
    stepperDriver.en_spreadCycle(false);
    logger.info("StepperDrv: GCONF set (StealthChop)");

    stepperDriver.toff(TMC_TOFF_DEFAULT);
    stepperDriver.tbl(TMC_TBL_DEFAULT);
    stepperDriver.vsense(false);
    stepperDriver.mres(DRV_MICROSTEP_RES);
    logger.info("StepperDrv: CHOPCONF set (mres=4 -> 16 microsteps)");

    stepperDriver.pwm_autoscale(true);
    stepperDriver.pwm_autograd(true);
    stepperDriver.pwm_freq(TMC_PWM_FREQ_DEFAULT);
    logger.info("StepperDrv: PWMCONF set");

    stepperDriver.push();
    vTaskDelay(pdMS_TO_TICKS(DRV_PUSH_DELAY_MS));

    logger.info("StepperDrv: Reading back registers...");
    uint32_t ioin = stepperDriver.IOIN();
    uint8_t ver = stepperDriver.version();
    uint16_t mres_read = stepperDriver.mres();
    uint32_t drv_status = stepperDriver.DRV_STATUS();
    logger.info("StepperDrv: IOIN=0x%08X Version=%u mres=%u DRV_STATUS=0x%08X",
                ioin, ver, mres_read, drv_status);

    return true;
}

void stepperDrv_enable(void) {
    if (!driver_connected.load(std::memory_order_relaxed)) return;
    stepperDriver.toff(TMC_TOFF_DEFAULT);
}

bool stepperDrv_is_connected(void) {
    return driver_connected.load(std::memory_order_relaxed);
}

bool stepperDrv_read_drv_status(stepperDrv_drv_status_t* status) {
    if (!status || !driver_connected.load(std::memory_order_relaxed)) return false;

    uint32_t reg = stepperDriver.DRV_STATUS();
    status->otpw  = bitRead(reg, 0);   // OTPW
    status->ot    = bitRead(reg, 1);   // OT
    status->s2ga  = bitRead(reg, 2);   // S2GA
    status->s2gb  = bitRead(reg, 3);   // S2GB
    status->s2vsa = bitRead(reg, 4);   // S2VSA
    status->s2vsb = bitRead(reg, 5);   // S2VSB
    status->ola   = bitRead(reg, 6);   // OLA
    status->olb   = bitRead(reg, 7);   // OLB
    status->stst  = bitRead(reg, 31);  // STST (Standstill)

    // Обратная совместимость
    status->s2ola = status->s2ga;  // Short to GND A
    status->s2olb = status->s2gb;  // Short to GND B

    return true;
}