# Parking Car — Hệ thống khóa giữ chỗ sạc thông minh cho xe điện

Mô hình hệ thống khóa giữ chỗ sạc dành cho xe điện (EV) sử dụng ESP32 làm bộ điều khiển trung tâm. Người dùng điều khiển và giám sát qua app **Blynk IoT**, trong khi thiết bị phản hồi trạng thái trực tiếp qua LCD, LED RGB và buzzer.

---

## Mục lục

- [Tính năng](#tính-năng)
- [Phần cứng](#phần-cứng)
- [Sơ đồ kết nối GPIO](#sơ-đồ-kết-nối-gpio)
- [Luồng hoạt động](#luồng-hoạt-động)
- [Blynk Virtual Pins](#blynk-virtual-pins)
- [Trạng thái LCD](#trạng-thái-lcd)
- [Trạng thái LED RGB](#trạng-thái-led-rgb)
- [Cấu trúc thư mục](#cấu-trúc-thư-mục)
- [Cài đặt & Build](#cài-đặt--build)
- [Cấu hình Wi-Fi và Blynk](#cấu-hình-wi-fi-và-blynk)

---

## Tính năng

| # | Tính năng | Chi tiết |
|---|---|---|
| 1 | Điều khiển khóa từ xa | Nút trên app Blynk hạ/nâng servo mô phỏng cơ cấu khóa |
| 2 | Phát hiện cắm sạc | Công tắc KCD1 mô phỏng trạng thái cắm/rút sạc |
| 3 | Cảnh báo chưa sạc | Hạ khóa nhưng sau 10 giây không cắm sạc → còi + LED đỏ nháy |
| 4 | Tự động nâng khóa sau khi sạc xong | Rút sạc → đếm ngược 10 giây → tự động nâng khóa, không cần thao tác |
| 5 | Tự động nâng khóa khi xe rời đi | Cảm biến xác nhận không còn xe (> 60 mm) liên tục 5 giây → tự nâng khóa |
| 6 | Cảnh báo dùng trái phép | Bật công tắc khi chưa có xe trong bãi → còi + LED đỏ nháy |
| 7 | Cảnh báo va chạm | Vật thể < 20 mm phía trước cảm biến → còi + LED đỏ nháy |
| 8 | Hiển thị trạng thái tại chỗ | LCD 16×2, LED RGB (3 màu), LED đơn |
| 9 | Giám sát từ xa | Gửi khoảng cách, trạng thái khóa/sạc lên Blynk Cloud theo thời gian thực |

---

## Phần cứng

| Thiết bị | Mô tả |
|---|---|
| ESP32 Dev Module | Vi điều khiển trung tâm, có Wi-Fi tích hợp |
| Servo SG90 | Mô phỏng cơ cấu khóa giữ chỗ (quay 0°↔90°) |
| Buzzer thụ động | Còi báo động nhấp nháy 300 ms |
| Công tắc KCD1 | Mô phỏng việc cắm/rút sạc |
| LED đơn | Báo hiệu đang sạc |
| LED RGB Common Cathode | Báo trạng thái tổng thể (xanh/xanh dương/đỏ) |
| VL53L1X | Cảm biến khoảng cách ToF I2C, đo 0–4000 mm |
| LCD I2C 16×2 | Hiển thị trạng thái hệ thống, địa chỉ I2C `0x3F` |

---

## Sơ đồ kết nối GPIO

```
ESP32 GPIO   →  Thiết bị
─────────────────────────────────────────
GPIO 18      →  Servo SG90 (tín hiệu PWM)
GPIO 19      →  Buzzer (OUTPUT)
GPIO 14      →  Công tắc KCD1 (INPUT_PULLUP — LOW khi bật)
GPIO 15      →  LED đơn báo sạc (OUTPUT)
GPIO 26      →  LED RGB chân R (OUTPUT)
GPIO 27      →  LED RGB chân G (OUTPUT)
GPIO 25      →  LED RGB chân B (OUTPUT)
GPIO 21      →  SDA (chung cho VL53L1X và LCD)
GPIO 22      →  SCL (chung cho VL53L1X và LCD)
─────────────────────────────────────────
```

> VL53L1X và LCD I2C dùng chung bus I2C (SDA=21, SCL=22). Địa chỉ LCD là `0x3F` (xác định bằng I2C Scanner).

---

## Luồng hoạt động

### Luồng chính (happy path)

```
[App Blynk V9=1]
      │
      ▼
Servo hạ xuống 90° ──► isUnlocked = true
      │
      ├─ Trong 10 giây: người dùng cắm sạc (bật công tắc KCD1)
      │         │
      │         ▼
      │   Đang sạc bình thường (LED xanh dương, LCD "Dang sac OK")
      │         │
      │   Rút sạc (tắt công tắc) → đếm ngược 10 giây
      │         │
      │         ▼
      │   Sau 10 giây → Servo nâng lên 0° (tự động, không cần thao tác)
      │
      └─ Không cắm sạc sau 10 giây → CẢnh báo (còi + LED đỏ nháy)
```

### Các tình huống cảnh báo

| Tình huống | Điều kiện kích hoạt | Hành động |
|---|---|---|
| Chưa cắm sạc | Hạ khóa 10 giây, công tắc vẫn tắt | Còi nhấp nháy + LED đỏ nháy + LCD "Chua cam sac" |
| Chiếm chỗ sau sạc | Đã nâng khóa tự động nhưng xe còn đó (phát hiện qua cảm biến khoảng cách <120cm không liên tục) | — (chỉ nâng khóa, không có cảnh báo thêm) |
| Dùng trái phép | Bật công tắc khi chưa có lệnh hạ khóa | Còi nhấp nháy + LED đỏ nháy + LCD "Dung sai phep" |
| Va chạm | Vật thể < 20 mm phía trước VL53L1X | Còi nhấp nháy + LED đỏ nháy + LCD hiện khoảng cách |

### Tự động nâng khóa khi xe rời đi

Khi:
- Khóa đang hạ (`isUnlocked = true`)
- Công tắc đang tắt (không đang sạc)
- Cảm biến đo được khoảng cách > 60 mm liên tục trong 5 giây

→ Hệ thống tự nâng khóa, không cần thao tác trên app.

---

## Blynk Virtual Pins

| Virtual Pin | Chiều | Loại | Mô tả |
|---|---|---|---|
| **V1** | ESP32 → App | Số nguyên (cm) | Khoảng cách hợp lệ đo được từ VL53L1X |
| **V2** | ESP32 → App | Giá trị 0/1 | Phát hiện xe: khoảng cách ≤ 60 mm và công tắc đang bật |
| **V3** | ESP32 → App | Giá trị 0/1 | Trạng thái kết nối sạc |
| **V6** | ESP32 → App | Giá trị 0/1 | Trạng thái khóa: `1` = đã hạ, `0` = đã nâng |
| **V7** | ESP32 → App | Giá trị 0/1 | Trạng thái sạc nghiệp vụ |
| **V9** | App → ESP32 | Button 0/1 | Lệnh điều khiển: `1` = hạ khóa, `0` = nâng khóa |

> V9 phải được cấu hình kiểu **Switch** trong Blynk để giữ trạng thái 0/1.

---

## Trạng thái LCD

LCD 16×2 hiển thị theo thứ tự ưu tiên từ cao đến thấp:

| Ưu tiên | Dòng 1 | Dòng 2 | Khi nào |
|---|---|---|---|
| 1 (cao nhất) | `!! VA CHAM !!` | `KC: Xmm` | Vật thể < 20 mm phía trước |
| 2 | `!! CANH BAO !!` | `Dung sai phep` | Bật công tắc khi chưa có xe |
| 2 | `!! CANH BAO !!` | `Chiem cho!` | Chiếm chỗ sau sạc (logic cũ, hiện không dùng) |
| 2 | `!! CANH BAO !!` | `Chua cam sac` | Hạ khóa 10s chưa cắm sạc |
| 3 | `Bai do trong` | `San sang` | Khóa đang nâng, bãi trống |
| 3 | `Co xe do` | `Dang sac OK` | Đang sạc bình thường |
| 3 | `Sac xong - roi` | `Con: Xs` | Đang đếm ngược 10s sau rút sạc |
| 3 (thấp nhất) | `Co xe do` | `Cam sac: Xs` | Đang đếm ngược 10s chờ cắm sạc |

---

## Trạng thái LED RGB

| Màu | Ý nghĩa |
|---|---|
| Xanh lá (Green) | Bãi đỗ trống, sẵn sàng nhận xe |
| Xanh dương (Blue) | Có xe đang đỗ (khóa hạ) |
| Đỏ nhấp nháy (Red blink) | Có cảnh báo (va chạm / chưa sạc / dùng trái phép) |

---

## Cấu trúc thư mục

```
parking-car/
│
├── README.md                          # Tài liệu này
│
├── parking_lock_v2.ino                # Code Arduino gốc (nạp bằng Arduino IDE)
├── Nhung4.ino                         # Phiên bản tham khảo (dùng WiFi/Blynk khác)
│
├── firmware/
│   └── Nhung4.ino                     # Bản backup/đối chiếu
│
├── firmware_idf/                      # Project ESP-IDF (build & nạp bằng idf.py)
│   ├── CMakeLists.txt                 # Khai báo tên project cho ESP-IDF
│   ├── sdkconfig                      # Cấu hình SDK (tự sinh sau idf.py build)
│   ├── sdkconfig.defaults             # Cấu hình mặc định khi chưa có sdkconfig
│   ├── dependencies.lock              # Lock file phiên bản các managed component
│   │
│   ├── main/
│   │   ├── CMakeLists.txt             # Khai báo file nguồn + thư viện cần dùng
│   │   ├── idf_component.yml          # Khai báo espressif/arduino-esp32 tự tải về
│   │   ├── parking_lock_v2.cpp        # Logic điều khiển chính đã chuẩn hóa
│   │   ├── app_config.h               # GPIO, ngưỡng và thời gian hệ thống
│   │   ├── secrets.example.h          # Mẫu cấu hình Wi-Fi/Blynk để commit
│   │   └── secrets.h                  # Cấu hình cục bộ, bị loại khỏi Git
│   │
│   ├── components/                    # Thư viện thêm thủ công
│   │   ├── Blynk/                     # Blynk client for ESP32
│   │   ├── ESP32Servo/                # Điều khiển servo trên ESP32
│   │   ├── VL53L1X/                   # Driver cảm biến ToF VL53L1X
│   │   └── LiquidCrystal_I2C/         # Driver LCD I2C
│   │
│   ├── managed_components/            # Tự sinh: espressif/arduino-esp32 + deps
│   └── build/                         # Tự sinh sau idf.py build
│
├── web/
│   ├── index.html                     # Dashboard demo theo dõi ô sạc (web tĩnh)
│   └── overview.html                  # Trang tổng quan bãi đỗ
│
└── docs/
    ├── Bao_cao_Parking_Car_ESP_IDF.docx   # Báo cáo kỹ thuật
    └── create_esp_idf_report.py            # Script tạo báo cáo (dùng python-docx)
```

---

## Cài đặt & Build

### Cách 1: Arduino IDE

1. Mở `parking_lock_v2.ino` bằng Arduino IDE (2.x trở lên).
2. Cài board **ESP32** qua Board Manager (tìm `esp32` của Espressif Systems).
3. Cài các thư viện sau qua Library Manager:
   - `Blynk` (by Volodymyr Shymanskyy)
   - `ESP32Servo`
   - `VL53L1X` (by Pololu)
   - `LiquidCrystal_I2C` (by Frank de Brabander)
4. Điền Wi-Fi và Blynk token (xem phần [Cấu hình](#cấu-hình-wi-fi-và-blynk)).
5. Chọn board **ESP32 Dev Module**, chọn đúng cổng COM, nhấn **Upload**.

### Cách 2: ESP-IDF (khuyến nghị cho production)

#### Yêu cầu

- ESP-IDF v5.x (đã test với v5.5.2)
- Python 3.8+
- CMake 3.16+, Ninja

#### Khởi tạo môi trường (mỗi lần mở terminal mới)

**Windows (PowerShell):**
```powershell
$env:IDF_PATH = "C:\Users\<user>\esp\v5.5.2\esp-idf"
& "$env:IDF_PATH\export.ps1"
```

> Để không phải chạy tay mỗi lần, thêm 2 dòng trên vào file PowerShell Profile (`notepad $PROFILE`).

#### Build và nạp

```powershell
cd firmware_idf

# Lần đầu: tải thư viện + build toàn bộ (mất 5–15 phút)
idf.py build

# Nạp firmware + mở Serial Monitor
idf.py -p COM5 flash monitor
```

> Thay `COM5` bằng cổng COM thực tế của ESP32 (kiểm tra trong Device Manager).

Các lần build sau (chỉ build lại file đã thay đổi, nhanh hơn nhiều):
```powershell
idf.py build
idf.py -p COM5 flash monitor
```

#### Thoát Serial Monitor

Nhấn `Ctrl + ]`

---

## Cấu hình Wi-Fi và Blynk

Đối với bản ESP-IDF, không đặt token hoặc mật khẩu trực tiếp trong `parking_lock_v2.cpp`.

1. Sao chép `firmware_idf/main/secrets.example.h` thành `firmware_idf/main/secrets.h`.
2. Điền Wi-Fi, Blynk Template ID và Blynk Auth Token trong `secrets.h`.
3. Không commit `secrets.h`; tệp này đã được khai báo trong `.gitignore`.

```cpp
#define BLYNK_TEMPLATE_ID "TMPLxxxxxxxxx"
#define BLYNK_TEMPLATE_NAME "ParkingCar"
#define BLYNK_AUTH_TOKEN "your_auth_token_here"

namespace secrets {
inline constexpr char kWifiSsid[] = "Ten_WiFi_cua_ban";
inline constexpr char kWifiPassword[] = "Mat_khau_WiFi";
}
```

### Thiết lập Blynk

1. Tạo Template và Device trong Blynk Console.
2. Khai báo các Datastream V1, V2, V3, V6, V7 và V9 theo bảng phía trên.
3. Cấu hình V9 là nút kiểu Switch.

### Cấu hình GPIO, ngưỡng và thời gian

Các giá trị kỹ thuật được tập trung trong `firmware_idf/main/app_config.h`, ví dụ:

```cpp
inline constexpr uint16_t kCollisionThresholdMm = 20U;
inline constexpr uint16_t kVehiclePresentThresholdMm = 60U;
inline constexpr uint32_t kConnectChargerTimeoutMs = 10000U;
inline constexpr uint32_t kVehicleExitConfirmationMs = 5000U;
```

## Coding standard của bản ESP-IDF

Bản chuẩn hóa áp dụng quy ước của khóa Coursera về team coding standard, kết hợp các nguyên tắc MISRA-inspired phù hợp với dự án Arduino/ESP-IDF:

- Hằng số có kiểu bằng `inline constexpr`; không dùng magic number trong logic.
- Thời gian dùng `uint32_t` và phép trừ không dấu để an toàn khi `millis()` tràn.
- Khoảng cách được xử lý nhất quán theo milimét; chỉ đổi sang centimét khi gửi telemetry.
- Dùng `enum class` để phân biệt rõ xe có mặt, vắng mặt và lỗi cảm biến.
- Trạng thái runtime được gom trong một cấu trúc thay vì nhiều biến toàn cục rời rạc.
- Hàm nội bộ nằm trong anonymous namespace để giới hạn linkage.
- LCD dùng buffer ký tự cố định, tránh tạo nhiều đối tượng `String` động.
- Lỗi cảm biến được xử lý fail-safe: không coi lỗi đo là xe đã rời khỏi ô.
- Định dạng nguồn được mô tả trong `.clang-format`.

Đây là chuẩn nội bộ **lấy cảm hứng từ MISRA**, không phải chứng nhận tuân thủ MISRA đầy đủ vì dự án phụ thuộc vào Arduino core, Blynk và các thư viện bên thứ ba.

