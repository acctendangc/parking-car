/**
 * @file parking_lock_v2.cpp
 * @brief Smart parking lock controller for an ESP32-based EV charging bay.
 *
 * The application runs Arduino-compatible libraries as an ESP-IDF component.
 * It controls a servo lock, charger switch, LEDs, buzzer, VL53L1X distance
 * sensor, LCD and Blynk telemetry. The implementation follows a project-level
 * embedded coding standard derived from the Coursera course conventions and
 * MISRA-inspired defensive programming practices. It is not claimed to be
 * formally MISRA compliant because third-party Arduino/Blynk libraries are used.
 */

#include <Arduino.h>
#include "secrets.h"

#include <BlynkSimpleEsp32.h>
#include <LiquidCrystal_I2C.h>
#include <VL53L1X.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <Wire.h>

#include <cstdio>
#include <cstring>

#include "app_config.h"

namespace {

using namespace app_config;

constexpr int32_t kInvalidDistanceMm = -1;
constexpr size_t kLcdTextLength = 16U;
constexpr size_t kLcdBufferSize = kLcdTextLength + 1U;

enum class VehiclePresence : uint8_t {
    kUnknown = 0U,
    kPresent,
    kAbsent,
};

struct RuntimeState {
    bool sensor_ready = false;
    int32_t last_distance_mm = kInvalidDistanceMm;
    uint32_t last_sensor_read_ms = 0U;

    bool lock_is_lowered = false;
    bool general_alarm_active = false;
    bool collision_alarm_active = false;

    uint32_t lock_lowered_time_ms = 0U;
    uint32_t charger_disconnected_time_ms = 0U;
    bool waiting_after_charging = false;

    uint32_t vehicle_absent_since_ms = 0U;
    bool vehicle_exit_confirmation_active = false;

    uint32_t last_buzzer_toggle_ms = 0U;
    bool buzzer_output_high = false;

    bool previous_switch_on = false;
    bool previous_charger_connected = false;

    uint32_t last_red_blink_ms = 0U;
    bool red_blink_output_on = false;

    uint32_t last_lcd_update_ms = 0U;
    char previous_lcd_line_1[kLcdBufferSize] = "";
    char previous_lcd_line_2[kLcdBufferSize] = "";
};

VL53L1X g_distance_sensor;
LiquidCrystal_I2C g_lcd(kLcdI2cAddress, kLcdColumns, kLcdRows);
RuntimeState g_state;

bool HasElapsed(uint32_t now_ms, uint32_t start_ms, uint32_t interval_ms) {
    return static_cast<uint32_t>(now_ms - start_ms) >= interval_ms;
}

void SetLedColor(bool red_on, bool green_on, bool blue_on) {
    digitalWrite(kRgbRedPin, red_on ? HIGH : LOW);
    digitalWrite(kRgbGreenPin, green_on ? HIGH : LOW);
    digitalWrite(kRgbBluePin, blue_on ? HIGH : LOW);
}

void SetRgbOff() {
    SetLedColor(false, false, false);
}

void SetRgbGreen() {
    SetLedColor(false, true, false);
}

void SetRgbBlue() {
    SetLedColor(false, false, true);
}

void SetRgbRed() {
    SetLedColor(true, false, false);
}

void NormalizeLcdLine(const char* source, char* destination) {
    std::memset(destination, ' ', kLcdTextLength);
    destination[kLcdTextLength] = '\0';

    if (source == nullptr) {
        return;
    }

    size_t source_length = 0U;
    while ((source_length < kLcdTextLength) && (source[source_length] != '\0')) {
        ++source_length;
    }
    std::memcpy(destination, source, source_length);
}

void ShowLcd(const char* line_1, const char* line_2) {
    char normalized_line_1[kLcdBufferSize];
    char normalized_line_2[kLcdBufferSize];

    NormalizeLcdLine(line_1, normalized_line_1);
    NormalizeLcdLine(line_2, normalized_line_2);

    if (std::strncmp(normalized_line_1, g_state.previous_lcd_line_1, kLcdBufferSize) != 0) {
        g_lcd.setCursor(0, 0);
        g_lcd.print(normalized_line_1);
        std::memcpy(g_state.previous_lcd_line_1, normalized_line_1, kLcdBufferSize);
    }

    if (std::strncmp(normalized_line_2, g_state.previous_lcd_line_2, kLcdBufferSize) != 0) {
        g_lcd.setCursor(0, 1);
        g_lcd.print(normalized_line_2);
        std::memcpy(g_state.previous_lcd_line_2, normalized_line_2, kLcdBufferSize);
    }
}

void AttachServoPwm() {
    ledcDetach(kServoPin);

    const bool attached = ledcAttachChannel(
        kServoPin,
        kServoFrequencyHz,
        kServoResolutionBits,
        kServoLedcChannel);

    if (!attached) {
        Serial.println("LOI: Khong cau hinh duoc PWM servo tren GPIO18!");
    }
}

void WriteServoAngle(uint8_t angle_deg) {
    const uint8_t constrained_angle = (angle_deg > 180U) ? 180U : angle_deg;
    const uint32_t pulse_width_us = static_cast<uint32_t>(map(
        constrained_angle,
        0U,
        180U,
        kServoMinimumPulseUs,
        kServoMaximumPulseUs));

    const uint32_t maximum_duty = (1UL << kServoResolutionBits) - 1UL;
    const uint32_t duty = static_cast<uint32_t>(
        (static_cast<uint64_t>(pulse_width_us) * maximum_duty) / kServoPeriodUs);

    ledcWrite(kServoPin, duty);
}

bool IsChargerSwitchOn() {
    return digitalRead(kChargerSwitchPin) == LOW;
}

int32_t ReadDistanceMm() {
    if (!g_state.sensor_ready) {
        return kInvalidDistanceMm;
    }

    const uint16_t distance_mm = g_distance_sensor.read(false);

    if (g_distance_sensor.timeoutOccurred()) {
        return kInvalidDistanceMm;
    }

    if ((distance_mm == 0U) || (distance_mm > kMaximumValidDistanceMm)) {
        return kInvalidDistanceMm;
    }

    return static_cast<int32_t>(distance_mm);
}

VehiclePresence GetVehiclePresence() {
    if (g_state.last_distance_mm == kInvalidDistanceMm) {
        return VehiclePresence::kUnknown;
    }

    if (g_state.last_distance_mm <= static_cast<int32_t>(kVehiclePresentThresholdMm)) {
        return VehiclePresence::kPresent;
    }

    return VehiclePresence::kAbsent;
}

void ResetVehicleExitConfirmation() {
    g_state.vehicle_exit_confirmation_active = false;
    g_state.vehicle_absent_since_ms = 0U;
}

void StopBuzzer() {
    g_state.buzzer_output_high = false;
    digitalWrite(kBuzzerPin, LOW);
}

void RaiseLockAndResetState() {
    WriteServoAngle(kServoLockedAngleDeg);
    Blynk.virtualWrite(V6, 0);
    Blynk.virtualWrite(V7, 0);

    g_state.lock_is_lowered = false;
    g_state.general_alarm_active = false;
    g_state.waiting_after_charging = false;
    ResetVehicleExitConfirmation();
    StopBuzzer();
}

void UpdateDistanceSensorAndTelemetry(uint32_t now_ms, bool switch_on) {
    if (!HasElapsed(now_ms, g_state.last_sensor_read_ms, kSensorReadIntervalMs)) {
        return;
    }

    g_state.last_sensor_read_ms = now_ms;
    const int32_t distance_mm = ReadDistanceMm();

    if (distance_mm == kInvalidDistanceMm) {
        g_state.last_distance_mm = kInvalidDistanceMm;
        Blynk.virtualWrite(V2, 0);
        return;
    }

    g_state.last_distance_mm = distance_mm;

    const int32_t distance_cm = distance_mm / 10;
    Blynk.virtualWrite(V1, distance_cm);

    const bool vehicle_detected =
        (distance_mm <= static_cast<int32_t>(kVehiclePresentThresholdMm)) && switch_on;
    Blynk.virtualWrite(V2, vehicle_detected ? 1 : 0);

    const bool collision_detected =
        distance_mm < static_cast<int32_t>(kCollisionThresholdMm);

    if (collision_detected && !g_state.collision_alarm_active) {
        g_state.collision_alarm_active = true;
        Serial.println("!! CANH BAO VA CHAM !!");
    } else if (!collision_detected && g_state.collision_alarm_active) {
        g_state.collision_alarm_active = false;
        Serial.println("Khoang cach an toan tro lai.");
    }
}

void UpdateChargingTelemetry(bool charger_connected) {
    digitalWrite(kChargingLedPin, charger_connected ? HIGH : LOW);

    if (charger_connected != g_state.previous_charger_connected) {
        Blynk.virtualWrite(V3, charger_connected ? 1 : 0);
        g_state.previous_charger_connected = charger_connected;
    }
}

void ProcessSwitchTransition(uint32_t now_ms, bool switch_on) {
    if (switch_on == g_state.previous_switch_on) {
        return;
    }

    if (switch_on) {
        if (g_state.lock_is_lowered) {
            Serial.println("Da cam sac - LED sang.");
            Blynk.virtualWrite(V7, 1);
            g_state.general_alarm_active = false;
            g_state.waiting_after_charging = false;
            ResetVehicleExitConfirmation();
            StopBuzzer();
        } else {
            Serial.println("CANH BAO: Dung tru sac khi chua co xe!");
            g_state.general_alarm_active = true;
        }
    } else if (g_state.lock_is_lowered) {
        Serial.println("Da rut sac - bat dau dem 10s.");
        Blynk.virtualWrite(V7, 0);
        g_state.waiting_after_charging = true;
        g_state.charger_disconnected_time_ms = now_ms;
        ResetVehicleExitConfirmation();
    } else {
        Serial.println("Tat cong tac - het canh bao.");
        g_state.general_alarm_active = false;
        StopBuzzer();
    }

    g_state.previous_switch_on = switch_on;
}

void ProcessConnectChargerTimeout(uint32_t now_ms, bool charger_connected) {
    if (!g_state.lock_is_lowered || g_state.general_alarm_active ||
        g_state.waiting_after_charging || charger_connected) {
        return;
    }

    if (HasElapsed(now_ms, g_state.lock_lowered_time_ms, kConnectChargerTimeoutMs)) {
        Serial.println("CANH BAO: 10s chua cam sac!");
        g_state.general_alarm_active = true;
    }
}

void ProcessPostChargeState(uint32_t now_ms) {
    if (!g_state.waiting_after_charging) {
        return;
    }

    switch (GetVehiclePresence()) {
        case VehiclePresence::kPresent:
            ResetVehicleExitConfirmation();

            if (HasElapsed(
                    now_ms,
                    g_state.charger_disconnected_time_ms,
                    kLeaveParkingTimeoutMs) &&
                !g_state.general_alarm_active) {
                Serial.println("CANH BAO: Rut sac nhung xe chua roi bai!");
                g_state.general_alarm_active = true;
            }
            break;

        case VehiclePresence::kAbsent:
            if (!g_state.vehicle_exit_confirmation_active) {
                g_state.vehicle_exit_confirmation_active = true;
                g_state.vehicle_absent_since_ms = now_ms;
                g_state.general_alarm_active = false;
                StopBuzzer();
            } else if (HasElapsed(
                           now_ms,
                           g_state.vehicle_absent_since_ms,
                           kVehicleExitConfirmationMs)) {
                Serial.println("Xe da roi bai an toan - tu dong nang khoa.");
                RaiseLockAndResetState();
            }
            break;

        case VehiclePresence::kUnknown:
        default:
            /* Fail-safe: an invalid sensor reading must never be treated as a
             * confirmed vehicle exit. Require a new continuous valid absence
             * interval before the lock can be raised. */
            ResetVehicleExitConfirmation();
            break;
    }
}

void UpdateBuzzer(uint32_t now_ms) {
    if (!(g_state.general_alarm_active || g_state.collision_alarm_active)) {
        StopBuzzer();
        return;
    }

    if (HasElapsed(now_ms, g_state.last_buzzer_toggle_ms, kAlarmToggleIntervalMs)) {
        g_state.last_buzzer_toggle_ms = now_ms;
        g_state.buzzer_output_high = !g_state.buzzer_output_high;
        digitalWrite(kBuzzerPin, g_state.buzzer_output_high ? HIGH : LOW);
    }
}

void UpdateRgb(uint32_t now_ms) {
    if (g_state.general_alarm_active || g_state.collision_alarm_active) {
        if (HasElapsed(now_ms, g_state.last_red_blink_ms, kRgbBlinkIntervalMs)) {
            g_state.last_red_blink_ms = now_ms;
            g_state.red_blink_output_on = !g_state.red_blink_output_on;

            if (g_state.red_blink_output_on) {
                SetRgbRed();
            } else {
                SetRgbOff();
            }
        }
        return;
    }

    g_state.red_blink_output_on = false;

    if (g_state.lock_is_lowered) {
        SetRgbBlue();
    } else {
        SetRgbGreen();
    }
}

void UpdateLcd(uint32_t now_ms) {
    if (!HasElapsed(now_ms, g_state.last_lcd_update_ms, kLcdUpdateIntervalMs)) {
        return;
    }

    g_state.last_lcd_update_ms = now_ms;
    char line_2[kLcdBufferSize];

    if (g_state.collision_alarm_active) {
        if (g_state.last_distance_mm != kInvalidDistanceMm) {
            std::snprintf(
                line_2,
                sizeof(line_2),
                "KC: %ldmm",
                static_cast<long>(g_state.last_distance_mm));
        } else {
            std::snprintf(line_2, sizeof(line_2), "KC: ??");
        }

        ShowLcd("!! VA CHAM !!", line_2);
        return;
    }

    if (g_state.general_alarm_active) {
        if (!g_state.lock_is_lowered) {
            ShowLcd("!! CANH BAO !!", "Dung sai phep");
        } else if (g_state.waiting_after_charging) {
            ShowLcd("!! CANH BAO !!", "Chiem cho!");
        } else {
            ShowLcd("!! CANH BAO !!", "Chua cam sac");
        }
        return;
    }

    if (!g_state.lock_is_lowered) {
        ShowLcd("Bai do trong", "San sang");
        return;
    }

    if (IsChargerSwitchOn()) {
        ShowLcd("Co xe do", "Dang sac OK");
        return;
    }

    if (g_state.waiting_after_charging) {
        if (g_state.vehicle_exit_confirmation_active) {
            const uint32_t elapsed_seconds =
                static_cast<uint32_t>(now_ms - g_state.vehicle_absent_since_ms) / 1000U;
            const uint32_t confirmation_seconds = kVehicleExitConfirmationMs / 1000U;
            const uint32_t remaining_seconds =
                (elapsed_seconds < confirmation_seconds)
                    ? (confirmation_seconds - elapsed_seconds)
                    : 0U;

            std::snprintf(
                line_2,
                sizeof(line_2),
                "Khoa sau: %lus",
                static_cast<unsigned long>(remaining_seconds));
            ShowLcd("Xe da roi di", line_2);
        } else {
            const uint32_t elapsed_seconds =
                static_cast<uint32_t>(now_ms - g_state.charger_disconnected_time_ms) / 1000U;
            const uint32_t timeout_seconds = kLeaveParkingTimeoutMs / 1000U;
            const uint32_t remaining_seconds =
                (elapsed_seconds < timeout_seconds) ? (timeout_seconds - elapsed_seconds) : 0U;

            std::snprintf(
                line_2,
                sizeof(line_2),
                "Con: %lus",
                static_cast<unsigned long>(remaining_seconds));
            ShowLcd("Sac xong - roi", line_2);
        }
        return;
    }

    const uint32_t elapsed_seconds =
        static_cast<uint32_t>(now_ms - g_state.lock_lowered_time_ms) / 1000U;
    const uint32_t timeout_seconds = kConnectChargerTimeoutMs / 1000U;
    const uint32_t remaining_seconds =
        (elapsed_seconds < timeout_seconds) ? (timeout_seconds - elapsed_seconds) : 0U;

    std::snprintf(
        line_2,
        sizeof(line_2),
        "Cam sac: %lus",
        static_cast<unsigned long>(remaining_seconds));
    ShowLcd("Co xe do", line_2);
}

void InitializeHardware() {
    Serial.begin(kSerialBaudRate);
    Serial.println("========================================");
    Serial.println("Khoi dong he thong Smart Parking Car...");
    Serial.println("========================================");

    pinMode(kBuzzerPin, OUTPUT);
    StopBuzzer();

    pinMode(kChargerSwitchPin, INPUT_PULLUP);
    pinMode(kChargingLedPin, OUTPUT);
    digitalWrite(kChargingLedPin, LOW);

    pinMode(kRgbRedPin, OUTPUT);
    pinMode(kRgbGreenPin, OUTPUT);
    pinMode(kRgbBluePin, OUTPUT);
    SetRgbGreen();

    Wire.begin(kI2cSdaPin, kI2cSclPin);
    Wire.setClock(kI2cClockHz);

    g_lcd.init();
    g_lcd.backlight();
    g_lcd.clear();
    ShowLcd("Khoi dong...", "Ket noi WiFi");

    g_distance_sensor.setTimeout(kSensorTimeoutMs);
    if (g_distance_sensor.init()) {
        g_distance_sensor.setDistanceMode(VL53L1X::Short);
        g_distance_sensor.setMeasurementTimingBudget(kSensorTimingBudgetUs);
        g_distance_sensor.startContinuous(kSensorContinuousPeriodMs);
        g_state.sensor_ready = true;
        Serial.println("VL53L1X san sang.");
    } else {
        g_state.sensor_ready = false;
        Serial.println("LOI: Khong tim thay VL53L1X!");
    }

    AttachServoPwm();
    WriteServoAngle(kServoLockedAngleDeg);
    Serial.print("Servo dat o goc mac dinh: ");
    Serial.println(kServoLockedAngleDeg);

    g_state.previous_switch_on = IsChargerSwitchOn();

    Blynk.begin(BLYNK_AUTH_TOKEN, secrets::kWifiSsid, secrets::kWifiPassword);
    Serial.println("Da ket noi Blynk Cloud.");

    ShowLcd("Bai do trong", "San sang");
    Serial.println("He thong san sang!");
    Serial.println("========================================");
}

void RunApplicationCycle() {
    Blynk.run();

    const uint32_t now_ms = millis();
    const bool switch_on = IsChargerSwitchOn();
    const bool charger_connected = switch_on && g_state.lock_is_lowered;

    UpdateChargingTelemetry(charger_connected);
    UpdateDistanceSensorAndTelemetry(now_ms, switch_on);
    ProcessSwitchTransition(now_ms, switch_on);
    ProcessConnectChargerTimeout(now_ms, charger_connected);
    ProcessPostChargeState(now_ms);
    UpdateBuzzer(now_ms);
    UpdateRgb(now_ms);
    UpdateLcd(now_ms);
}

void HandleLockCommand(int32_t unlock_command) {
    if (unlock_command == 1) {
        Serial.println("Lenh ha khoa kich hoat - Servo ve 90 do.");
        WriteServoAngle(kServoUnlockedAngleDeg);
        Blynk.virtualWrite(V6, 1);

        g_state.lock_is_lowered = true;
        g_state.lock_lowered_time_ms = millis();
        g_state.general_alarm_active = false;
        g_state.waiting_after_charging = false;
        ResetVehicleExitConfirmation();
        StopBuzzer();

        if (IsChargerSwitchOn()) {
            Serial.println("Cong tac da bat san -> bat dau sac ngay.");
            Blynk.virtualWrite(V7, 1);
        }
        return;
    }

    Serial.println("Lenh nang khoa - Servo ve 0 do.");
    WriteServoAngle(kServoLockedAngleDeg);
    Blynk.virtualWrite(V6, 0);
    Blynk.virtualWrite(V7, 0);

    g_state.lock_is_lowered = false;
    g_state.waiting_after_charging = false;
    ResetVehicleExitConfirmation();
    digitalWrite(kChargingLedPin, LOW);

    if (IsChargerSwitchOn()) {
        Serial.println("CANH BAO: Cong tac van ON sau nang khoa!");
        g_state.general_alarm_active = true;
    } else {
        g_state.general_alarm_active = false;
        StopBuzzer();
    }
}

}  // namespace

BLYNK_WRITE(V9) {
    HandleLockCommand(param.asInt());
}

extern "C" void app_main(void) {
    initArduino();
    InitializeHardware();

    while (true) {
        RunApplicationCycle();
        vTaskDelay(pdMS_TO_TICKS(app_config::kMainLoopDelayMs));
    }
}
