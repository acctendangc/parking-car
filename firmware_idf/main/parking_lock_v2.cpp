/* ============================================
   HỆ THỐNG KHOÁ GIỮ CHỖ SẠC THÔNG MINH CHO XE ĐIỆN
   Smart Parking Car for EV Charging
   ============================================
   Phần cứng:
   - ESP32 Dev Module
   - Servo SG90 (D18) - mô phỏng khóa giữ chỗ
   - Buzzer (D19) - còi báo động
   - Công tắc KCD1 (D14) - mô phỏng cắm sạc
   - LED đơn (D15) - báo đang sạc
   - LED RGB Catot (D26 R, D27 G, D25 B) - báo trạng thái
   - VL53L1X (D21 SDA, D22 SCL) - cảm biến va chạm
   - LCD I2C 16x2 (D21 SDA, D22 SCL) - hiển thị trạng thái
   ============================================ */
#include <Arduino.h>
/* Điền thông tin Blynk của bạn vào đây */
#define BLYNK_TEMPLATE_ID "TMPL6fUC766cO"
#define BLYNK_TEMPLATE_NAME "ParkingCar"
#define BLYNK_AUTH_TOKEN "8kMyasAuHrqFZRGrk_dA-K4VJI1i6EIN"

/* Khai báo thư viện */
#include <WiFi.h>
#include <WiFiClient.h>
#include <BlynkSimpleEsp32.h>
#include <ESP32Servo.h>
#include <Wire.h>
#include <VL53L1X.h>
#include <LiquidCrystal_I2C.h>

/* Thông tin WiFi */
char ssid[] = "Xiaomi 15T";
char pass[] = "xuanducshare";

/* ====== ĐỊNH NGHĨA CÁC CHÂN GPIO ====== */
const int servoPin  = 18; // Servo SG90
const int buzzerPin = 19; // Còi báo động
const int switchPin = 14; // Công tắc KCD1 (mô phỏng cắm sạc)
const int ledPin    = 15; // LED đơn báo sạc

/* LED RGB Catot (chân R/G/B đã xác định bằng test) */
const int ledR = 26; // Đỏ
const int ledG = 27; // Xanh lá
const int ledB = 25; // Xanh dương

/* I2C dùng chung cho VL53L1X và LCD */
const int sdaPin = 21;
const int sclPin = 22;

/* LCD I2C địa chỉ 0x3F (đã xác định bằng I2C Scanner) */
LiquidCrystal_I2C lcd(0x3F, 16, 2);

/* Ngưỡng cảnh báo va chạm (mm) - dưới 5cm là nguy hiểm */
const int COLLISION_THRESHOLD_MM = 40;

/* ====== CẤU HÌNH SERVO ======
   Logic đảo ngược: khi nút Blynk BẬT (hạ khóa) -> servo về 90 độ
                    khi nút Blynk TẮT (nâng khóa) -> servo về 0 độ */
Servo myServo;
const int angleDefault = 0;   // Trạng thái mặc định: khóa nâng (chặn xe)
const int angleUnlock  = 90;  // Trạng thái hạ khóa: cho xe vào

/* ====== CẢM BIẾN KHOẢNG CÁCH ====== */
VL53L1X distSensor;
bool sensorOk = false;
int  lastDistanceCm = -1;
unsigned long lastSensorRead = 0;

/* ====== BIẾN LOGIC HỆ THỐNG ====== */
unsigned long unlockTime = 0;
bool isUnlocked    = false;  // Trạng thái khóa đã hạ
bool alarmActive   = false;  // Cảnh báo chung (chưa sạc / chiếm chỗ / dùng trái phép)
bool collisionAlarm = false; // Cảnh báo va chạm

/* Biến điều khiển còi nhấp nháy */
unsigned long lastBeepTime = 0;
bool buzzerState = false;

/* Biến theo dõi trạng thái công tắc */
bool lastSwitchState = false;

/* Biến đếm 10s sau khi rút sạc */
unsigned long chargerOffTime = 0;
bool waitingAfterCharge = false;

/* Biến điều khiển LED RGB nhấp nháy đỏ */
unsigned long lastRedBlinkTime = 0;
bool redBlinkState = false;

/* Biến quản lý cập nhật LCD (tránh nháy màn hình) */
String lcdLine1Prev = "";
String lcdLine2Prev = "";
unsigned long lastLcdUpdate = 0;

/* ========================================
   HÀM ĐIỀU KHIỂN LED RGB (Common Cathode)
   ======================================== */
void setLEDColor(bool red, bool green, bool blue) {
  digitalWrite(ledR, red   ? HIGH : LOW);
  digitalWrite(ledG, green ? HIGH : LOW);
  digitalWrite(ledB, blue  ? HIGH : LOW);
}

void turnOffRGB()  { setLEDColor(false, false, false); }
void setRGBGreen() { setLEDColor(false, true,  false); } // Bãi trống
void setRGBBlue()  { setLEDColor(false, false, true);  } // Có xe đỗ
void setRGBRed()   { setLEDColor(true,  false, false); } // Cảnh báo

/* ========================================
   HÀM ĐIỀU KHIỂN LCD
   ======================================== */
void showLCD(String line1, String line2) {
  // Đệm cho đủ 16 ký tự để xóa nội dung cũ
  while (line1.length() < 16) line1 += " ";
  while (line2.length() < 16) line2 += " ";
  line1 = line1.substring(0, 16);
  line2 = line2.substring(0, 16);

  // Chỉ ghi lại khi nội dung thay đổi - tránh nháy màn hình
  if (line1 != lcdLine1Prev) {
    lcd.setCursor(0, 0);
    lcd.print(line1);
    lcdLine1Prev = line1;
  }
  if (line2 != lcdLine2Prev) {
    lcd.setCursor(0, 1);
    lcd.print(line2);
    lcdLine2Prev = line2;
  }
}

/* ========================================
   HÀM ĐỌC TRẠNG THÁI CÔNG TẮC
   ======================================== */
bool isSwitchPhysicallyOn() {
  return digitalRead(switchPin) == LOW;
}

bool isChargerConnected() {
  return isSwitchPhysicallyOn() && isUnlocked;
}

/* ========================================
   HÀM CẬP NHẬT LCD THEO TRẠNG THÁI (CÓ ƯU TIÊN)
   ======================================== */
void updateLCDStatus() {
  if (millis() - lastLcdUpdate < 500) return;
  lastLcdUpdate = millis();

  // ƯU TIÊN 1: Cảnh báo va chạm
  if (collisionAlarm) {
    String distStr = (lastDistanceCm >= 0)
                     ? ("KC: " + String(lastDistanceCm) + "cm")
                     : "KC: ??";
    showLCD("!! VA CHAM !!", distStr);
    return;
  }

  // ƯU TIÊN 2: Cảnh báo chung
  if (alarmActive) {
    if (!isUnlocked) {
      showLCD("!! CANH BAO !!", "Dung sai phep");
    } else if (waitingAfterCharge) {
      showLCD("!! CANH BAO !!", "Chiem cho!");
    } else {
      showLCD("!! CANH BAO !!", "Chua cam sac");
    }
    return;
  }

  // TRẠNG THÁI BÌNH THƯỜNG
  if (!isUnlocked) {
    showLCD("Bai do trong", "San sang");
  }
  else if (isSwitchPhysicallyOn()) {
    showLCD("Co xe do", "Dang sac OK");
  }
  else if (waitingAfterCharge) {
    unsigned long elapsed = (millis() - chargerOffTime) / 1000;
    long remain = 10 - (long)elapsed;
    if (remain < 0) remain = 0;
    showLCD("Sac xong - roi", "Con: " + String(remain) + "s");
  }
  else {
    unsigned long elapsed = (millis() - unlockTime) / 1000;
    long remain = 10 - (long)elapsed;
    if (remain < 0) remain = 0;
    showLCD("Co xe do", "Cam sac: " + String(remain) + "s");
  }
}

/* ========================================
   HÀM ĐỌC KHOẢNG CÁCH TỪ VL53L1X
   ======================================== */
int readDistanceCm() {
  if (!sensorOk) return -1;
  uint16_t mm = distSensor.read(false);
  if (distSensor.timeoutOccurred()) return -1;
  if (mm == 0 || mm > 4000) return -1;
  return mm / 10;
}

/* ========================================
   HÀM CẬP NHẬT MÀU LED RGB THEO TRẠNG THÁI
   ======================================== */
void updateRGB() {
  // Bất kỳ cảnh báo nào -> LED đỏ nhấp nháy
  if (alarmActive || collisionAlarm) {
    unsigned long currentMillis = millis();
    if (currentMillis - lastRedBlinkTime >= 300) {
      lastRedBlinkTime = currentMillis;
      redBlinkState = !redBlinkState;
      if (redBlinkState) setRGBRed();
      else               turnOffRGB();
    }
  }
  else if (isUnlocked) {
    setRGBBlue();    // Có xe đỗ
    redBlinkState = false;
  }
  else {
    setRGBGreen();   // Bãi trống
    redBlinkState = false;
  }
}

/* ========================================
   SETUP - KHỞI TẠO HỆ THỐNG
   ======================================== */
void setup() {
  Serial.begin(115200);
  Serial.println("========================================");
  Serial.println("Khoi dong he thong Smart Parking Car...");
  Serial.println("========================================");

  // Cấu hình các chân
  pinMode(buzzerPin, OUTPUT);
  digitalWrite(buzzerPin, LOW);

  pinMode(switchPin, INPUT_PULLUP);
  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, LOW);

  pinMode(ledR, OUTPUT);
  pinMode(ledG, OUTPUT);
  pinMode(ledB, OUTPUT);
  setRGBGreen(); // Khởi động: bãi trống -> xanh lá

  // Khởi tạo I2C dùng chung cho LCD và VL53L1X
  Wire.begin(sdaPin, sclPin);
  Wire.setClock(400000);

  // Khởi tạo LCD
  lcd.init();
  lcd.backlight();
  lcd.clear();
  showLCD("Khoi dong...", "Ket noi WiFi");

  // Khởi tạo cảm biến VL53L1X
  distSensor.setTimeout(500);
  if (distSensor.init()) {
    distSensor.setDistanceMode(VL53L1X::Short);
    distSensor.setMeasurementTimingBudget(50000);
    distSensor.startContinuous(50);
    sensorOk = true;
    Serial.println("VL53L1X san sang.");
  } else {
    sensorOk = false;
    Serial.println("LOI: Khong tim thay VL53L1X!");
  }

  // Cấu hình Servo PWM
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);
  myServo.setPeriodHertz(50);
  myServo.attach(servoPin, 500, 2400);
  myServo.write(angleDefault); // Mặc định: khóa nâng (0 độ)
  Serial.print("Servo dat o goc mac dinh: ");
  Serial.println(angleDefault);

  // Kết nối Blynk
  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);
  Serial.println("Da ket noi Blynk Cloud.");

  showLCD("Bai do trong", "San sang");
  Serial.println("He thong san sang!");
  Serial.println("========================================");
}

/* ========================================
   LOOP - VÒNG LẶP CHÍNH
   ======================================== */
void loop() {
  Blynk.run();

  bool switchOn  = isSwitchPhysicallyOn();
  bool chargerOn = isChargerConnected();

  // LED đơn báo trạng thái sạc
  digitalWrite(ledPin, chargerOn ? HIGH : LOW);

  // ===== ĐỌC CẢM BIẾN KHOẢNG CÁCH (MỖI 100ms) =====
  if (millis() - lastSensorRead >= 100) {
    lastSensorRead = millis();
    int distCm = readDistanceCm();

    if (distCm >= 0) {
      lastDistanceCm = distCm;
      Blynk.virtualWrite(V8, distCm);

      bool tooClose = (distCm * 10) < COLLISION_THRESHOLD_MM;
      if (tooClose && !collisionAlarm) {
        collisionAlarm = true;
        Serial.println("!! CANH BAO VA CHAM !!");
      } else if (!tooClose && collisionAlarm) {
        collisionAlarm = false;
        Serial.println("Khoang cach an toan tro lai.");
      }
    }
  }

  // ===== PHÁT HIỆN THAY ĐỔI TRẠNG THÁI CÔNG TẮC =====
  if (switchOn != lastSwitchState) {

    if (switchOn) {
      // Vừa BẬT công tắc
      if (isUnlocked) {
        // Hợp lệ: xe đã vào bãi, bắt đầu sạc
        Serial.println("Da cam sac - LED sang.");
        Blynk.virtualWrite(V7, 1);
        alarmActive = false;
        waitingAfterCharge = false;
        digitalWrite(buzzerPin, LOW);
      } else {
        // Cảnh báo: bật công tắc khi chưa có xe trong bãi
        Serial.println("CANH BAO: Dung tru sac khi chua co xe!");
        alarmActive = true;
      }
    }
    else {
      // Vừa TẮT công tắc
      if (isUnlocked) {
        // Đang trong bãi mà rút sạc -> bắt đầu đếm 10s "chiếm chỗ"
        Serial.println("Da rut sac - bat dau dem 10s.");
        Blynk.virtualWrite(V7, 0);
        waitingAfterCharge = true;
        chargerOffTime = millis();
      } else {
        // Tắt công tắc khi chưa có xe -> hết cảnh báo dùng trái phép
        Serial.println("Tat cong tac - het canh bao.");
        alarmActive = false;
        digitalWrite(buzzerPin, LOW);
      }
    }

    lastSwitchState = switchOn;
  }

  // ===== LOGIC 1: 10s SAU HẠ KHÓA MÀ KHÔNG SẠC =====
  if (isUnlocked && !alarmActive && !waitingAfterCharge) {
    if (!chargerOn && millis() - unlockTime >= 10000) {
      Serial.println("CANH BAO: 10s chua cam sac!");
      alarmActive = true;
    }
  }

  // ===== LOGIC 2: 10s SAU RÚT SẠC MÀ KHÔNG RỜI BÃI =====
  if (waitingAfterCharge) {
    if (millis() - chargerOffTime >= 10000) {
      Serial.println("Sac xong - tu dong nang khoa.");
      myServo.write(angleDefault);
      Blynk.virtualWrite(V6, 0);
      Blynk.virtualWrite(V7, 0);
      isUnlocked = false;
      alarmActive = false;
      waitingAfterCharge = false;
      digitalWrite(buzzerPin, LOW);
    }
  }

  // ===== ĐIỀU KHIỂN CÒI BÁO ĐỘNG =====
  if (alarmActive || collisionAlarm) {
    unsigned long currentMillis = millis();
    if (currentMillis - lastBeepTime >= 300) {
      lastBeepTime = currentMillis;
      buzzerState = !buzzerState;
      digitalWrite(buzzerPin, buzzerState);
    }
  } else {
    digitalWrite(buzzerPin, LOW);
  }

  // ===== CẬP NHẬT LED RGB VÀ LCD =====
  updateRGB();
  updateLCDStatus();
}

/* ========================================
   BLYNK CALLBACK - NHẬN LỆNH HẠ KHÓA TỪ APP
   V9: Button hạ/nâng khóa
   ======================================== */
BLYNK_WRITE(V9) {
  int unlock_cmd = param.asInt();

  if (unlock_cmd == 1) {
    // Bật nút "hạ khóa" trên Blynk -> servo về 90 độ (cho xe vào)
    Serial.println("Lenh ha khoa kich hoat - Servo ve 90 do.");
    myServo.write(angleUnlock);
    Blynk.virtualWrite(V6, 1);

    isUnlocked  = true;
    unlockTime  = millis();
    alarmActive = false;
    waitingAfterCharge = false;
    digitalWrite(buzzerPin, LOW);

    // Nếu công tắc đã bật sẵn -> coi như sạc ngay
    if (isSwitchPhysicallyOn()) {
      Serial.println("Cong tac da bat san -> bat dau sac ngay.");
      Blynk.virtualWrite(V7, 1);
    }
  }
  else {
    // Tắt nút "hạ khóa" -> servo về 0 độ (chặn xe)
    Serial.println("Lenh nang khoa - Servo ve 0 do.");
    myServo.write(angleDefault);
    Blynk.virtualWrite(V6, 0);
    Blynk.virtualWrite(V7, 0);

    isUnlocked  = false;
    waitingAfterCharge = false;
    digitalWrite(ledPin, LOW);

    // Nếu nâng khóa mà công tắc vẫn ON -> cảnh báo dùng trái phép
    if (isSwitchPhysicallyOn()) {
      Serial.println("CANH BAO: Cong tac van ON sau nang khoa!");
      alarmActive = true;
    } else {
      alarmActive = false;
      digitalWrite(buzzerPin, LOW);
    }
  }
}
extern "C" void app_main(void)
{
    initArduino();
    setup();

    while (true) {
        loop();
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}
