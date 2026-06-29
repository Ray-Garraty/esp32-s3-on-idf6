#ifndef STEPPER_DRV_H
#define STEPPER_DRV_H
#include <Arduino.h>
#include <TMCStepper.h>

// === Конфигурация ===
// TMC2209 использует полудуплекс (single-wire) через PDN_UART.
// ESP32 работает в полном дуплексе, резистор 1кОм на TX создаёт физический half-duplex.
#define STEPPER_UART_TX 17      // TX -> резистор 1кОм -> PDN_UART
#define STEPPER_UART_RX 16      // RX -> напрямую -> PDN_UART
#define STEPPER_BAUD_RATE 115200
#define R_SENSE 0.11f           // BTT TMC2209 v1.3 использует шунт 0.11 Ом
#define DRIVER_ADDRESS 0x00     // Адрес по умолчанию

// === TMC2209 Register Addresses ===
#define STEPPER_DRV_REG_IOIN       0x08
#define STEPPER_DRV_REG_SGTHRS     0x40
#define STEPPER_DRV_REG_SG_RESULT  0x41
#define STEPPER_DRV_REG_COOLCONF   0x42

// Структура статуса драйвера TMC2209 (регистр DRV_STATUS, адрес 0x6F)
typedef struct {
    bool otpw;     // Бит 0: Over Temperature Pre-Warning
    bool ot;       // Бит 1: Over Temperature - драйвер отключен
    bool s2ga;     // Бит 2: Short to GND, фаза A
    bool s2gb;     // Бит 3: Short to GND, фаза B
    bool s2vsa;    // Бит 4: Short to VS, фаза A
    bool s2vsb;    // Бит 5: Short to VS, фаза B
    bool ola;      // Бит 6: Open Load, фаза A
    bool olb;      // Бит 7: Open Load, фаза B
    bool stst;     // Бит 31: Standstill - мотор стоит

    // Обратная совместимость со старым кодом
    bool s2ola;    // = s2ga (Short to GND A)
    bool s2olb;    // = s2gb (Short to GND B)
} stepperDrv_drv_status_t;

// Глобальный экземпляр драйвера (доступен для прямого вызова методов библиотеки)
extern TMC2209Stepper stepperDriver;

/** @brief Init TMC2209 UART, test connection, configure basic registers */
bool stepperDrv_init(void);
/** @brief Read a TMC2209 register by symbolic ID (IOIN, SG_RESULT) */
bool stepperDrv_read_register(uint8_t reg, uint32_t* value);
/** @brief Write a TMC2209 register by symbolic ID (SGTHRS, COOLCONF) */
bool stepperDrv_write_register(uint8_t reg, uint32_t value);
/** @brief Configure StealthChop, CoolStep, PWM, microstep resolution */
bool stepperDrv_configure_stealthchop(void);
/** @brief Enable driver (set TOFF to default) */
void stepperDrv_enable(void);
/** @brief True if TMC2209 communication was successful */
bool stepperDrv_is_connected(void);
/** @brief Decode DRV_STATUS register into struct with error flags */
bool stepperDrv_read_drv_status(stepperDrv_drv_status_t* status);

#endif // STEPPER_DRV_H