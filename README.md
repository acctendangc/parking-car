# Parking Car - Hệ thống khóa giữ chỗ sạc thông minh cho xe điện

Mô hình hệ thống khóa giữ chỗ sạc dành cho xe điện (EV), sử dụng ESP32 làm bộ điều khiển trung tâm, kết hợp servo mô phỏng cơ cấu khóa, cảm biến khoảng cách để cảnh báo va chạm, LCD/LED/buzzer hiển thị trạng thái tại chỗ và Blynk để điều khiển/giám sát từ xa.

## Tính năng chính

- Hạ/nâng khóa giữ chỗ qua nút điều khiển trên app Blynk.
- Nhận biết xe đã cắm sạc bằng công tắc mô phỏng trạng thái charger.
- Cảnh báo nếu hạ khóa nhưng quá 10 giây chưa cắm sạc.
- Cảnh báo "chiếm chỗ" nếu rút sạc nhưng quá 10 giây vẫn chưa rời bãi.
- Cảnh báo va chạm/vật thể quá gần bằng cảm biến khoảng cách VL53L1X.
- Hiển thị trạng thái tại chỗ qua LCD I2C 16x2, LED RGB và LED đơn, kèm buzzer báo động.
- Gửi trạng thái khóa/sạc/khoảng cách lên Blynk Cloud để giám sát từ xa.

## Phần cứng sử dụng

| Thiết bị | GPIO | Vai trò |
|---|---|---|
| ESP32 Dev Module | - | Bộ điều khiển trung tâm |
| Servo SG90 | GPIO18 | Mô phỏng cơ cấu khóa giữ chỗ |
| Buzzer | GPIO19 | Còi báo động |
| Công tắc KCD1 | GPIO14 | Mô phỏng cắm/rút sạc |
| LED đơn | GPIO15 | Báo đang sạc |
| LED RGB (Common Cathode) | GPIO26 (R), GPIO27 (G), GPIO25 (B) | Báo trạng thái bãi đỗ |
| VL53L1X | GPIO21 (SDA), GPIO22 (SCL) | Cảm biến khoảng cách/va chạm |
| LCD I2C 16x2 (0x3F) | GPIO21 (SDA), GPIO22 (SCL) | Hiển thị trạng thái |

## Cấu trúc thư mục

```
parking-car/
├── parking_lock_v2.ino       # Code Arduino gốc (nạp bằng Arduino IDE)
├── Nhung4.ino                # Bản code Arduino tham khảo/đối chiếu
├── firmware/                 # Bản code Arduino khác để đối chiếu
├── firmware_idf/             # Project ESP-IDF (build/nạp bằng idf.py)
│   ├── main/                 # Code chính (parking_lock_v2.cpp) + CMakeLists
│   └── components/           # Thư viện local: Blynk, ESP32Servo, VL53L1X, LiquidCrystal_I2C
├── web/                       # Giao diện web demo (index.html, overview.html)
└── docs/                       # Tài liệu, báo cáo kỹ thuật
```

## Build & nạp firmware

### Cách 1: Arduino IDE

1. Mở `parking_lock_v2.ino` bằng Arduino IDE.
2. Cài board "ESP32" và các thư viện: Blynk, ESP32Servo, VL53L1X, LiquidCrystal_I2C.
3. Điền thông tin Wi-Fi (`ssid`, `pass`) và thông tin Blynk (`BLYNK_TEMPLATE_ID`, `BLYNK_TEMPLATE_NAME`, `BLYNK_AUTH_TOKEN`) trong code.
4. Chọn board ESP32 Dev Module, đúng cổng COM, sau đó Upload.

### Cách 2: ESP-IDF

```bash
cd firmware_idf
idf.py build
idf.py -p COM3 flash monitor
```

Project ESP-IDF dùng Arduino core cho ESP32 (qua `idf_component.yml`), nên giữ nguyên phong cách code `setup()`/`loop()` nhưng build/nạp bằng `idf.py`.

## Cấu hình Wi-Fi / Blynk

Thông tin Wi-Fi và Blynk (`ssid`, `pass`, `BLYNK_AUTH_TOKEN`...) hiện đang khai báo trực tiếp trong code (`parking_lock_v2.ino` / `parking_lock_v2.cpp`). Khi dùng cho thiết bị thật, cần thay bằng thông tin Wi-Fi và token Blynk của bạn.

## Web demo

Thư mục `web/` chứa giao diện demo dạng web tĩnh (`index.html`, `overview.html`) mô phỏng dashboard theo dõi các ô sạc.

## Tài liệu

Báo cáo kỹ thuật cho phần ESP-IDF nằm tại `docs/Bao_cao_Parking_Car_ESP_IDF.docx`, được sinh ra từ script `docs/create_esp_idf_report.py` (yêu cầu cài `python-docx`).
