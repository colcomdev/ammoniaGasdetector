/*******************************************************
 * COMPLETE GAS ALERT + LOGGING SYSTEM (ESP32)
 *******************************************************/

#include <WiFi.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ---------------- LCD ----------------
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ---------------- PIN CONFIG ----------------
#define GAS_SENSOR_PIN 34

#define GREEN_LED 18
#define YELLOW_LED 19
#define RED_LED 23
#define BUZZER 5

// GSM UART pins
#define GSM_RX 16
#define GSM_TX 17

String title = "Colcom Ammonia Gas Detector";

unsigned long startTime = 0;
bool showSplash = true;

// Scrolling
int scrollPos = 0;
unsigned long lastScroll = 0;
int scrollDelay = 300; // speed
// ---------------- GSM SETTINGS ----------------

#define CALL_TIMEOUT 20000
#define ALERT_COOLDOWN 60000

// ---------------- WIFI SETTINGS ----------------
const char* ssid = "ManlowWifi";
const char* password = "123456789";
const char* serverUrl = "http://192.168.X.X:5000/api/gas";

// ---------------- DEVICE INFO ----------------
String deviceID = "ESP32_GAS_001";

// ---------------- GAS THRESHOLDS ----------------
const float WARNING_PPM  = 25.0;
const float HARMFUL_PPM  = 35.0;
const float CRITICAL_PPM = 50.0;
const float IDLH_PPM     = 300.0;

// ---------------- LOGGING ----------------
unsigned long logInterval = 600000;
unsigned long lastLogTime = 0;
float maxPPM = 0;

// ---------------- PHONE NUMBERS ----------------
String phoneNumbers[] = {
  "+263718357831",
  "+263773466252"
};

const int totalNumbers = sizeof(phoneNumbers) / sizeof(phoneNumbers[0]);

// -----------------------------------------------------
float convertToPPM(int analogValue) {
  return (analogValue / 4095.0) * 500.0;
}

// -----------------------------------------------------
String getGasLevelCategory(float ppm) {
  if (ppm >= IDLH_PPM) return "IDLH";
  if (ppm >= CRITICAL_PPM) return "CRITICAL";
  if (ppm >= HARMFUL_PPM) return "HARMFUL";
  if (ppm >= WARNING_PPM) return "WARNING";
  return "SAFE";
}

// -----------------------------------------------------
void updateIndicators(String level) {

  digitalWrite(GREEN_LED, LOW);
  digitalWrite(YELLOW_LED, LOW);
  digitalWrite(RED_LED, LOW);
  noTone(BUZZER);

  if (level == "SAFE") {
    digitalWrite(GREEN_LED, HIGH);
  }
  else if (level == "WARNING") {
    digitalWrite(YELLOW_LED, HIGH);
  }
  else {
    digitalWrite(RED_LED, HIGH);
    tone(BUZZER, 1000);
  }
}

// -----------------------------------------------------
void updateLCD(float ppm, String level) {

  // After 15 seconds, disable splash
  if (millis() - startTime > 15000) {
    showSplash = false;
  }

  if (showSplash) {
    return; // keep showing startup screen
  }

  // Scroll title
  if (millis() - lastScroll > scrollDelay) {
    lastScroll = millis();
    scrollPos++;

    if (scrollPos > title.length()) {
      scrollPos = 0;
    }
  }

  lcd.clear();

  // Create visible window of text
  String visibleText = title.substring(scrollPos);

  lcd.setCursor(0, 0);
  lcd.print(visibleText.substring(0, 16));

  // Bottom row: gas data
  lcd.setCursor(0, 1);
  lcd.print(level);
  lcd.print(" ");
  lcd.print(ppm, 1);
  lcd.print("ppm");
}

// -----------------------------------------------------
void makeCall(String number) {
  Serial.println("Dialing " + number);
  Serial2.println("ATD" + number + ";");
}

// -----------------------------------------------------
void hangCall() {
  Serial2.println("ATH");
}

// -----------------------------------------------------
bool isCallAnswered(unsigned long timeout) {
  unsigned long startTime = millis();

  while (millis() - startTime < timeout) {
    if (Serial2.available()) {
      String response = Serial2.readString();

      if (response.indexOf("CONNECT") != -1) {
        return true;
      }
    }
  }
  return false;
}

// -----------------------------------------------------
void sendSMS(String number, String message) {

  Serial2.println("AT+CMGF=1");
  delay(1000);

  Serial2.println("AT+CMGS=\"" + number + "\"");
  delay(1000);

  Serial2.print(message);
  delay(500);

  Serial2.write(26);
  delay(3000);
}

// -----------------------------------------------------
void processAlert(float ppm, String level) {

  for (int i = 0; i < totalNumbers; i++) {

    String number = phoneNumbers[i];

    makeCall(number);

    bool answered = isCallAnswered(CALL_TIMEOUT);

    hangCall();

    String message = "GAS ALERT\n";
    message += "Device: " + deviceID + "\n";
    message += "Level: " + level + "\n";
    message += "PPM: " + String(ppm);
    Serial.println("-----------Sending SMS---------");
    sendSMS(number, message);

    if (answered) {
      Serial.println("Answered");
    } else {
      Serial.println("No answer");
    }

    delay(5000);
  }
}

// -----------------------------------------------------
void sendToAPI(float ppm, String level) {

  if (WiFi.status() == WL_CONNECTED) {

    HTTPClient http;

    http.begin(serverUrl);
    http.addHeader("Content-Type", "application/json");

    String jsonData = "{";
    jsonData += "\"device_id\":\"" + deviceID + "\",";
    jsonData += "\"gas_ppm\":" + String(ppm) + ",";
    jsonData += "\"level\":\"" + level + "\"";
    jsonData += "}";

    int response = http.POST(jsonData);

    Serial.println("HTTP: " + String(response));

    http.end();
  }
}

// -----------------------------------------------------
void setup() {
  Serial.begin(115200);

  // GSM UART2
  Serial2.begin(9600, SERIAL_8N1, GSM_RX, GSM_TX);

  // LCD
  Wire.begin(21, 22);
  lcd.begin(16, 2);
  lcd.backlight();

  // Pins
  pinMode(GREEN_LED, OUTPUT);
  pinMode(YELLOW_LED, OUTPUT);
  pinMode(RED_LED, OUTPUT);

  lcd.setCursor(0, 0);
  lcd.print("Connecting WiFi");

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("WiFi Connected");

  lcd.clear();
  lcd.print("WiFi Connected");

  delay(2000);
  startTime = millis();

// Splash screen
lcd.clear();
lcd.setCursor(0, 0);
lcd.print("Colcom Ammonia");

lcd.setCursor(0, 1);
lcd.print("Gas Detector");
}

// -----------------------------------------------------
void loop() {

  int raw = analogRead(GAS_SENSOR_PIN);
  float ppm = convertToPPM(raw);

  String level = getGasLevelCategory(ppm);

  Serial.println("PPM: " + String(ppm) + " | " + level);

  updateIndicators(level);
  updateLCD(ppm, level);

  if (ppm > maxPPM) {
    maxPPM = ppm;
  }

  if (level == "HARMFUL" || level == "CRITICAL" || level == "IDLH") {

    processAlert(ppm, level);
    sendToAPI(ppm, level);

    delay(ALERT_COOLDOWN);
    maxPPM = 0;
    return;
  }

  if (millis() - lastLogTime >= logInterval) {
    lastLogTime = millis();

    sendToAPI(maxPPM, "NORMAL_LOG");
    maxPPM = 0;
  }

  delay(2000);
}