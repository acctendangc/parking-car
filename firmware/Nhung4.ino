/* Điền thông tin Blynk của bạn vào đây */
#define BLYNK_TEMPLATE_ID "TMPL6la2SNFSa"
#define BLYNK_TEMPLATE_NAME "ParkingCar"
#define BLYNK_AUTH_TOKEN "N7OFNh5W2bXFpSDSCxZQjLaaL64z9GCi"

/* Khai báo thư viện */
#include <WiFi.h>
#include <WiFiClient.h>
#include <BlynkSimpleEsp32.h>
#include <ESP32Servo.h>
#include <Wire.h>
#include <VL53L1X.h>
#include <LiquidCrystal_I2C.h>

/* Thông tin WiFi */
char ssid[] = "1395 GIAI PHONG";
char pass[] = "12345678";

/* Định nghĩa các chân GPIO */
const int servoPin  = 18;
const int buzzerPin = 19;
const int switchPin = 14;
const int ledPin    = 15;

/* LED RGB Catot */
const int ledR = 26;
const int ledG = 27;
const int ledB = 25;

/* I2C cho VL53L1X và LCD */
const int sdaPin = 21;
const int sclPin = 22;

/* LCD I2C địa chỉ 0x3F (đã xác định bằng I2C Scanner) */
LiquidCrystal_I2C lcd(0x3F, 16, 2);

/* Ngưỡng cảnh báo va chạm (mm) */
const int COLLISION_THRESHOLD_MM = 50;

/* Cấu hình Servo */
Servo myServo;
const int angleDefault = 90;
const int angleUnlock  = 0;

/* Cảm biến khoảng cách */
VL53L1X distSensor;
bool sensorOk = false;
int  lastDistanceCm = -1;
unsigned long lastSensorRead = 0;

/* Biến logic */
unsigned long unlockTime = 0;
bool isUnlocked  = false;
bool alarmActive = false;
bool collisionAlarm = false;

unsigned long lastBeepTime = 0;
bool buzzerState = false;

bool lastSwitchState = false;

unsigned long chargerOffTime = 0;
bool waitingAfterCharge = false;

unsigned long lastRedBlinkTime = 0;
bool redBlinkState = false;

/* Biến quản lý cập nhật LCD */
String lcdLine1Prev = "";
String lcdLine2Prev = "";
unsigned long lastLcdUpdate = 0;

/* ========== HÀM ĐIỀU KHIỂN LED RGB ========== */
void setLEDColor(bool red, bool green, bool blue) {
  digitalWrite(ledR, red   ? HIGH : LOW);
  digitalWrite(ledG, green ? HIGH : LOW);
  digitalWrite(ledB, blue  ? HIGH : LOW);
}

void turnOffRGB()  { setLEDColor(false, false, false); }
void setRGBGreen() { setLEDColor(false, true,  false); }
void setRGBBlue()  { setLEDColor(false, false, true);  }
void setRGBRed()   { setLEDColor(true,  false, false); }

/* ========== HÀM ĐIỀU KHIỂN LCD ========== */
void showLCD(String line1, String line2) {
  while (line1.length() < 16) line1 += " ";
  while (line2.length() < 16) line2 += " ";
  line1 = line1.substring(0, 16);
  line2 = line2.substring(0, 16);

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

/* Đọc công tắc */
bool isSwitchPhysicallyOn() {
  return digitalRead(switchPin) == LOW;
}

bool isChargerConnected() {
  return isSwitchPhysicallyOn() && isUnlocked;
}

/* Cập nhật LCD theo trạng thái hệ thống */
void updateLCDStatus() {
  if (millis() - lastLcdUpdate < 500) return;
  lastLcdUpdate = millis();

  // ƯU TIÊN 1: cảnh báo va chạm
  if (collisionAlarm) {
    String distStr = (lastDistanceCm >= 0) ? ("KC: " + String(lastDistanceCm) + "cm") : "KC: ??";
    showLCD("!! VA CHAM !!", distStr);
    return;
  }

  // ƯU TIÊN 2: cảnh báo chung
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

/* Đọc khoảng cách */
int readDistanceCm() {
  if (!sensorOk) return -1;
  uint16_t mm = distSensor.read(false);
  if (distSensor.timeoutOccurred()) return -1;
  if (mm == 0 || mm > 4000) return -1;
  return mm / 10;
}

/* Cập nhật LED RGB */
void updateRGB() {
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
    setRGBBlue();
    redBlinkState = false;
  }
  else {
    setRGBGreen();
    redBlinkState = false;
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(buzzerPin, OUTPUT);
  digitalWrite(buzzerPin, LOW);

  pinMode(switchPin, INPUT_PULLUP);
  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, LOW);

  pinMode(ledR, OUTPUT);
  pinMode(ledG, OUTPUT);
  pinMode(ledB, OUTPUT);
  setRGBGreen();

  // Khởi tạo I2C dùng chung cho LCD và VL53L1X
  Wire.begin(sdaPin, sclPin);
  Wire.setClock(400000);

  // Khởi tạo LCD
  lcd.init();
  lcd.backlight();
  lcd.clear();
  showLCD("Khoi dong...", "Ket noi WiFi");

  // Khởi tạo VL53L1X
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

  // Servo
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);
  myServo.setPeriodHertz(50);
  myServo.attach(servoPin, 500, 2400);
  myServo.write(angleDefault);
  Serial.println("Hệ thống sẵn sàng. Servo đang ở 90 độ.");

  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);

  showLCD("Bai do trong", "San sang");
}

void loop() {
  Blynk.run();

  bool switchOn  = isSwitchPhysicallyOn();
  bool chargerOn = isChargerConnected();

  // LED đơn
  digitalWrite(ledPin, chargerOn ? HIGH : LOW);

  // Đọc cảm biến khoảng cách
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

  // Phát hiện thay đổi công tắc
  if (switchOn != lastSwitchState) {
    if (switchOn) {
      if (isUnlocked) {
        Serial.println("Đã cắm sạc - LED sáng.");
        Blynk.virtualWrite(V7, 1);
        alarmActive = false;
        waitingAfterCharge = false;
        digitalWrite(buzzerPin, LOW);
      } else {
        Serial.println("CẢNH BÁO: Dùng trụ sạc khi chưa có xe!");
        alarmActive = true;
      }
    } else {
      if (isUnlocked) {
        Serial.println("Đã rút sạc - đếm 10s.");
        Blynk.virtualWrite(V7, 0);
        waitingAfterCharge = true;
        chargerOffTime = millis();
      } else {
        Serial.println("Tắt công tắc - hết cảnh báo.");
        alarmActive = false;
        digitalWrite(buzzerPin, LOW);
      }
    }
    lastSwitchState = switchOn;
  }

  // Logic 1: 10s sau hạ khóa mà không sạc
  if (isUnlocked && !alarmActive && !waitingAfterCharge) {
    if (!chargerOn && millis() - unlockTime >= 10000) {
      Serial.println("CẢNH BÁO: 10s chưa cắm sạc!");
      alarmActive = true;
    }
  }

  // Logic 2: 10s sau rút sạc mà không rời bãi
  if (waitingAfterCharge && !alarmActive) {
    if (millis() - chargerOffTime >= 10000) {
      Serial.println("CẢNH BÁO: Sạc xong nhưng chưa rời bãi!");
      alarmActive = true;
      waitingAfterCharge = false;
    }
  }

  // Điều khiển còi
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

  // Cập nhật LED RGB và LCD
  updateRGB();
  updateLCDStatus();
}

BLYNK_WRITE(V9) {
  int unlock_cmd = param.asInt();

  if (unlock_cmd == 1) {
    Serial.println("Lệnh hạ khóa kích hoạt.");
    myServo.write(angleUnlock);
    Blynk.virtualWrite(V6, 1);

    isUnlocked  = true;
    unlockTime  = millis();
    alarmActive = false;
    waitingAfterCharge = false;
    digitalWrite(buzzerPin, LOW);

    if (isSwitchPhysicallyOn()) {
      Serial.println("Công tắc đã bật sẵn → sạc ngay.");
      Blynk.virtualWrite(V7, 1);
    }
  }
  else {
    Serial.println("Lệnh nâng khóa.");
    myServo.write(angleDefault);
    Blynk.virtualWrite(V6, 0);
    Blynk.virtualWrite(V7, 0);

    isUnlocked  = false;
    waitingAfterCharge = false;
    digitalWrite(ledPin, LOW);

    if (isSwitchPhysicallyOn()) {
      Serial.println("CẢNH BÁO: Công tắc vẫn ON sau nâng khóa!");
      alarmActive = true;
    } else {
      alarmActive = false;
      digitalWrite(buzzerPin, LOW);
    }
  }
}