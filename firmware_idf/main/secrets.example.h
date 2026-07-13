#ifndef SECRETS_EXAMPLE_H_
#define SECRETS_EXAMPLE_H_

/*
 * Copy this file to secrets.h, then replace the placeholder values.
 * Never commit secrets.h to Git.
 */
#define BLYNK_TEMPLATE_ID "YOUR_BLYNK_TEMPLATE_ID"
#define BLYNK_TEMPLATE_NAME "ParkingCar"
#define BLYNK_AUTH_TOKEN "YOUR_BLYNK_AUTH_TOKEN"

namespace secrets {
inline constexpr char kWifiSsid[] = "YOUR_WIFI_SSID";
inline constexpr char kWifiPassword[] = "YOUR_WIFI_PASSWORD";
}  // namespace secrets

#endif  // SECRETS_EXAMPLE_H_
