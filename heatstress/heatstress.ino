#include <Wire.h>
#include <SPI.h>
#include <LoRa.h>
#include <Adafruit_SHT31.h>
#include <BH1750.h>
#include <RtcDS1302.h>  // Include the Makuna RTCDS1302 library
#include <ThreeWire.h>
#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"

#define FIREBASE_URL "https://smartwear-95f1f-default-rtdb.asia-southeast1.firebasedatabase.app"
#define FIREBASE_API "AIzaSyDy5p9F3Nt7yr9ei0rgSHili9SDbJrF8fc"
#define WIFI_SSID "globe wifi"
#define WIFI_PASSWORD "123456789"

FirebaseAuth auth;
FirebaseConfig config;
FirebaseData firebaseData;

// Pin Definitions
#define LORA_CS 5
#define LORA_RST 14
#define LORA_IRQ 2
#define SHT3X_SDA 21  // Define SDA pin for SHT3x
#define SHT3X_SCL 22  // Define SCL pin for SHT3x
#define BH1750_SDA 21
#define BH1750_SCL 22

// DS1302 Pins
const int IO = 26;    // DAT
const int SCLK = 27;  // CLK
const int CE = 25;    // RST

ThreeWire myWire(IO, SCLK, CE);
RtcDS1302<ThreeWire> Rtc(myWire);

// Create instances for sensors
Adafruit_SHT31 sht31 = Adafruit_SHT31();
BH1750 lightMeter;

// Function declarations
float calculateIrradiance(float lux);
void printDateTime(const RtcDateTime& dt);

void setup() {
  Serial.begin(9600);

  //firbase connection
  config.api_key = FIREBASE_API;
  config.database_url = FIREBASE_URL;
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to ");
  Serial.print(WIFI_SSID);
  while (WiFi.status() != WL_CONNECTED) {
    Serial.println(".");
    delay(500);
  }

  if (Firebase.signUp(&config, &auth, "", "")) {
    Serial.println("ok");
  } else {
    Serial.printf("%s\n", config.signer.signupError.message.c_str());
  }

  Serial.println();
  Serial.print("Connected to ");
  Serial.println(WIFI_SSID);
  Serial.print("IP Address is : ");
  Serial.println(WiFi.localIP());
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
  Serial.println();
  delay(1000);

  // Initialize I2C for SHT3x and BH1750 sensors
  Wire.begin(SHT3X_SDA, SHT3X_SCL);

  // Initialize SHT3x sensor
  if (!sht31.begin(0x44)) {  // Default I2C address for SHT3x is 0x44
    Serial.println("Could not find SHT3x sensor!");
    while (1)
      ;
  }

  // Initialize BH1750 sensor
  if (!lightMeter.begin()) {
    Serial.println("BH1750 sensor not found!");
    while (1)
      ;
  }
  Serial.println("BH1750 Sensor Initialized");

  // RTC initialization
  Rtc.Begin();
  RtcDateTime compiled = RtcDateTime(__DATE__, __TIME__);
  if (!Rtc.IsDateTimeValid()) {
    Serial.println("RTC lost confidence in the DateTime!");
    Rtc.SetDateTime(compiled);
  }

  if (Rtc.GetIsWriteProtected()) {
    Rtc.SetIsWriteProtected(false);
  }

  if (!Rtc.GetIsRunning()) {
    Rtc.SetIsRunning(true);
  }

  if (Rtc.GetDateTime() < compiled) {
    Rtc.SetDateTime(compiled);
  }

  // Initialize LoRa
  // LoRa.setPins(LORA_CS, LORA_RST, LORA_IRQ);
  // if (!LoRa.begin(455E6)) {  // Set frequency (455 MHz as an example)
  //   Serial.println("LoRa init failed!");
  //   while (1)
  //     ;
  // }
  // Serial.println("LoRa init succeeded!");
}

void loop() {
  // Read SHT3x sensor data (temperature and humidity)
  float temperature = sht31.readTemperature();    // Air temperature (Ta)
  float specificHumidity = sht31.readHumidity();  // Humidity (RH component)

  if (Firebase.RTDB.setInt(&firebaseData, "/temperature", temperature)) {
    Serial.println("Data uploaded successfully to Firebase");
  } else {
    Serial.println(firebaseData.errorReason());
    if (firebaseData.errorReason() == "token is not ready (revoked or expired)") {
      Firebase.refreshToken(&config);
    }
  }

 if (Firebase.RTDB.setInt(&firebaseData, "/humidity", specificHumidity)) {
    Serial.println("Data uploaded successfully to Firebase");
  } else {
    Serial.println(firebaseData.errorReason());
    if (firebaseData.errorReason() == "token is not ready (revoked or expired)") {
      Firebase.refreshToken(&config);
    }
  }



  // Read BH1750 sensor data (irradiance)
  float lux = lightMeter.readLightLevel();
  float irradiance = calculateIrradiance(lux);
  if (Firebase.RTDB.setInt(&firebaseData, "/irradiance", irradiance)) {
    Serial.println("Data uploaded successfully to Firebase");
  } else {
    Serial.println(firebaseData.errorReason());
    if (firebaseData.errorReason() == "token is not ready (revoked or expired)") {
      Firebase.refreshToken(&config);
    }
  }
  delay(1000);

  // Calculate Tnwb using the formula
  float Tnwb = temperature * atan(0.151977 * pow(specificHumidity + 8.313659, 0.5))
               + atan(temperature + specificHumidity)
               - atan(specificHumidity - 1.676331)
               + 0.00391838 * pow(specificHumidity, 1.5) * atan(0.023101 * specificHumidity)
               - 4.686035;
  // Calculate Tg
  float Tg = 0.009624 * irradiance + 1.102 * temperature - 0.00404 * specificHumidity - 2.2776;

  // Calculate WBGT
  float WBGT = 0.7 * Tnwb + 0.2 * Tg + 0.1 * temperature;

   if (Firebase.RTDB.setInt(&firebaseData, "/wbgt", WBGT)) {
    Serial.println("Data uploaded successfully to Firebase");
  } else {
    Serial.println(firebaseData.errorReason());
    if (firebaseData.errorReason() == "token is not ready (revoked or expired)") {
      Firebase.refreshToken(&config);
    }
  }

  // Evaluate thermal stress based on WBGT thresholds
  String thermalStressLevel;
  if (WBGT < 18) {
    thermalStressLevel = "No Heat";
  } else if (WBGT == 18) {
    thermalStressLevel = "Low Heat";
  } else if (WBGT < 23) {
    thermalStressLevel = "Moderate Heat";
  } else if (WBGT < 28) {
    thermalStressLevel = "High Heat";
  } else if (WBGT < 30) {
    thermalStressLevel = "Severe Heat Heat";
  } else {
    thermalStressLevel = "Critical Risk";
  }

  RtcDateTime now = Rtc.GetDateTime();
  printDateTime(now);

  // Print values to Serial Monitor
  Serial.print("Temperature: ");
  Serial.println(temperature);
  Serial.print("Specific Humidity: ");
  Serial.println(specificHumidity);
  Serial.print("Irradiance (W/m²): ");
  Serial.println(irradiance);
  Serial.print("WBGT: ");
  Serial.println(WBGT);
  Serial.print("Thermal Stress Level: ");
  Serial.println(thermalStressLevel);
  Serial.println(lux);

  // // Send data via LoRa
  // String payload = "Time: " + String(now.Month()) + "/" + String(now.Day()) + "/" + String(now.Year()) + " Temp: " + String(temperature) + " Irradiance: " + String(irradiance) + " WBGT: " + String(WBGT) + " Stress: " + thermalStressLevel;
  // LoRa.beginPacket();
  // LoRa.print(payload);
  // LoRa.endPacket();

  // // Alert system for heat stress detection
  // if (WBGT >= 30) {
  //   Serial.println("Critical Heat Risk! Immediate action required.");
  //   // Optionally include alarms (buzzer/LED)
  // }

  // delay(1000);  // Update every second
}

// Function to calculate solar irradiance based on BH1750 lux readings
float calculateIrradiance(float lux) {
  // Convert LUX to solar irradiance (W/m²) using a standard factor
  return lux * 0.0079;
}

void printDateTime(const RtcDateTime& dt) {
  char datestring[20];
  snprintf_P(datestring,
             sizeof(datestring),
             PSTR("%02u/%02u/%04u %02u:%02u:%02u"),
             dt.Month(),
             dt.Day(),
             dt.Year(),
             dt.Hour(),
             dt.Minute(),
             dt.Second());
  Serial.print("Time: ");
  Serial.println(datestring);
}
