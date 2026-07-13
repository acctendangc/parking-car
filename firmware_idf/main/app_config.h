#ifndef APP_CONFIG_H_
#define APP_CONFIG_H_

#include <stdint.h>

namespace app_config {

/* GPIO mapping */
inline constexpr uint8_t kServoPin = 18U;
inline constexpr uint8_t kBuzzerPin = 19U;
inline constexpr uint8_t kChargerSwitchPin = 14U;
inline constexpr uint8_t kChargingLedPin = 15U;
inline constexpr uint8_t kRgbRedPin = 26U;
inline constexpr uint8_t kRgbGreenPin = 27U;
inline constexpr uint8_t kRgbBluePin = 25U;
inline constexpr uint8_t kI2cSdaPin = 21U;
inline constexpr uint8_t kI2cSclPin = 22U;

/* LCD configuration */
inline constexpr uint8_t kLcdI2cAddress = 0x3FU;
inline constexpr uint8_t kLcdColumns = 16U;
inline constexpr uint8_t kLcdRows = 2U;
inline constexpr uint32_t kLcdUpdateIntervalMs = 500U;

/* Distance sensor configuration */
inline constexpr uint16_t kCollisionThresholdMm = 20U;
inline constexpr uint16_t kVehiclePresentThresholdMm = 60U;
inline constexpr uint16_t kMaximumValidDistanceMm = 4000U;
inline constexpr uint16_t kSensorTimeoutMs = 500U;
inline constexpr uint32_t kSensorTimingBudgetUs = 50000U;
inline constexpr uint16_t kSensorContinuousPeriodMs = 50U;
inline constexpr uint32_t kSensorReadIntervalMs = 100U;

/* Servo PWM configuration */
inline constexpr uint8_t kServoLockedAngleDeg = 0U;
inline constexpr uint8_t kServoUnlockedAngleDeg = 90U;
inline constexpr uint8_t kServoLedcChannel = 0U;
inline constexpr uint16_t kServoFrequencyHz = 50U;
inline constexpr uint8_t kServoResolutionBits = 16U;
inline constexpr uint16_t kServoMinimumPulseUs = 500U;
inline constexpr uint16_t kServoMaximumPulseUs = 2400U;
inline constexpr uint32_t kServoPeriodUs = 20000U;

/* Application timing */
inline constexpr uint32_t kConnectChargerTimeoutMs = 10000U;
inline constexpr uint32_t kLeaveParkingTimeoutMs = 10000U;
inline constexpr uint32_t kVehicleExitConfirmationMs = 5000U;
inline constexpr uint32_t kAlarmToggleIntervalMs = 300U;
inline constexpr uint32_t kRgbBlinkIntervalMs = 300U;
inline constexpr uint32_t kMainLoopDelayMs = 1U;

/* Communication */
inline constexpr uint32_t kSerialBaudRate = 115200U;
inline constexpr uint32_t kI2cClockHz = 400000U;

}  // namespace app_config

#endif  // APP_CONFIG_H_
