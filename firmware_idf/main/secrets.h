#ifndef SECRETS_H_
#define SECRETS_H_

/*
 * Local development configuration.
 * Replace the placeholder values before flashing the ESP32.
 * This file is excluded from Git by .gitignore.
 */
#define BLYNK_TEMPLATE_ID "TMPL6d0PgvUhe"
#define BLYNK_TEMPLATE_NAME "ParkingCar"
#define BLYNK_AUTH_TOKEN "REPLACE_WITH_NEW_BLYNK_TOKEN"

namespace secrets {
inline constexpr char kWifiSsid[] = "REPLACE_WITH_WIFI_SSID";
inline constexpr char kWifiPassword[] = "REPLACE_WITH_WIFI_PASSWORD";
}  // namespace secrets

#endif  // SECRETS_H_
