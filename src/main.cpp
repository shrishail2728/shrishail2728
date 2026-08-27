// The SparkFun MAX3010x library uses a 32-byte I2C buffer. Define the same
// value before Wire.h is included so the framework and library agree.
#ifndef I2C_BUFFER_LENGTH
#define I2C_BUFFER_LENGTH 32
#endif

#include <WiFi.h>
#include "ThingSpeak.h"
#include "secrets.h"
#include <DHT.h>
#include "MAX30105.h"
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <math.h>
#include "heartRate.h"      // Maxim's heart rate algorithm
#include "spo2_algorithm.h" // Maxim's SpO2 algorithm
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

// ---- DHT22 Config ----
#define DHTPIN 4
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

// ---- DS18B20 Config ----
// Connect the data wire to GPIO 5 and use a 4.7k pull-up resistor to 3.3V.
#define DS18B20_PIN 5
OneWire oneWire(DS18B20_PIN);
DallasTemperature ds18b20(&oneWire);

// ---- MAX30102 Config ----
MAX30105 particleSensor;

// ---- LCD Config ----
LiquidCrystal_I2C lcd(0x27, 16, 2); // Change to 0x3F if needed

// ---- AD8232 ECG Config ----
#define ECG_PIN 34 // ADC1_CH6 (GPIO34)
#define LO_PLUS_PIN 32
#define LO_MINUS_PIN 33

// ---- Shared Data Structure ----
struct SensorData
{
  float bpm; // Now stores Maxim BPM
  int32_t spo2;
  int32_t ecgValue;
  float ecgBpm;
  float temperature;
  float humidity;
  float probeTemperature;
  bool spo2_valid;
  bool hr_valid;
  bool probeTemperature_valid;
};

// ---- Global Variables ----
SensorData sensorData = {0, -999, 0, 0, NAN, NAN, NAN, false, false, false};
SemaphoreHandle_t dataMutex;
SemaphoreHandle_t i2cMutex;

// ---- MAX30102 Buffers ----
#define PPG_BUFFER_SIZE 100 // Matches Maxim algorithm requirements
uint32_t irBuffer[PPG_BUFFER_SIZE];
uint32_t redBuffer[PPG_BUFFER_SIZE];
int bufferIndex = 0;
SemaphoreHandle_t bufferMutex;
bool max30102Available = false;
bool ds18b20Available = false;

// ---- AD8232 Heartbeat Variables ----
unsigned long lastEcgBeat = 0;
const int ecgThreshold = 512; // Adjust this threshold as needed
bool ecgAboveThreshold = false;

// Wi-Fi configuration
const char *ssid = HEALTHTRACK_WIFI_SSID;
const char *password = HEALTHTRACK_WIFI_PASSWORD;

// ThingSpeak channel configuration
unsigned long channelID = HEALTHTRACK_THINGSPEAK_CHANNEL_ID;
const char *apiKey = HEALTHTRACK_THINGSPEAK_API_KEY;

WiFiClient client; // Create a WiFi client to send data to ThingSpeak

// Function prototypes for FreeRTOS tasks
void max30102Task(void *pvParameters);
void dht22Task(void *pvParameters);
void ds18b20Task(void *pvParameters);
void ecgTask(void *pvParameters);
void lcdTask(void *pvParameters);
void thingSpeakTask(void *pvParameters);

// Function to initialize sensors
void initializeSensors() {
  // --- MAX30102 Setup ---
  if (!particleSensor.begin(Wire, I2C_SPEED_STANDARD)) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Sensor Error");
    Serial.println("MAX30102 not found. Check wiring.");
  } else {
    particleSensor.setup(0x3F, 4, 2, 100, 411, 16384);
    particleSensor.setPulseAmplitudeRed(0x3F);
    particleSensor.setPulseAmplitudeIR(0x3F);
    max30102Available = true;
  }

  // --- DHT22 Setup ---
  dht.begin();

  // --- DS18B20 Setup ---
  ds18b20.begin();
  ds18b20.setResolution(10);
  ds18b20.setWaitForConversion(true);
  ds18b20Available = ds18b20.getDeviceCount() > 0;
  if (!ds18b20Available) {
    Serial.println("DS18B20 not found. Check wiring and pull-up resistor.");
  }

  // --- AD8232 Setup ---
  pinMode(ECG_PIN, INPUT);
  pinMode(LO_PLUS_PIN, INPUT);
  pinMode(LO_MINUS_PIN, INPUT);
}

// Function to connect to Wi-Fi with timeout
bool connectToWiFi() {
  WiFi.begin(ssid, password);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) { // 20 attempts, 1 second each
    delay(1000);
    Serial.println("Connecting to WiFi...");
    attempts++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Connected to WiFi");
    return true;
  } else {
    Serial.println("Failed to connect to WiFi");
    return false;
  }
}

void setup() {
  Serial.begin(115200);
  Wire.begin();

  // Create mutexes
  dataMutex = xSemaphoreCreateMutex();
  i2cMutex = xSemaphoreCreateMutex();
  bufferMutex = xSemaphoreCreateMutex();
  if (dataMutex == NULL || i2cMutex == NULL || bufferMutex == NULL) {
    Serial.println("Failed to create synchronization objects");
    while (true) {
      delay(1000);
    }
  }

  // --- LCD Setup ---
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Initializing...");

  initializeSensors();
  lcd.clear();

  // --- Wi-Fi Setup ---
  if (!connectToWiFi()) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("WiFi Error");
    Serial.println("Starting in offline mode; sensor tasks remain active.");
  }

  // --- ThingSpeak Setup ---
  ThingSpeak.begin(client);

  // --- Create FreeRTOS Tasks ---
  xTaskCreatePinnedToCore(max30102Task, "MAX30102 Task", 4096, NULL, 2, NULL, 1);
  xTaskCreatePinnedToCore(dht22Task, "DHT22 Task", 2048, NULL, 1, NULL, 0);
  xTaskCreatePinnedToCore(ds18b20Task, "DS18B20 Task", 2048, NULL, 1, NULL, 0);
  xTaskCreatePinnedToCore(ecgTask, "ECG Task", 2048, NULL, 1, NULL, 0);
  xTaskCreatePinnedToCore(lcdTask, "LCD Task", 2048, NULL, 1, NULL, 0);
  xTaskCreatePinnedToCore(thingSpeakTask, "ThingSpeak Task", 4096, NULL, 1, NULL, 0);
}

void max30102Task(void *pvParameters)
{
  while (1)
  {
    if (!max30102Available)
    {
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }

    // --- MAX30102 Sensor Readings ---
    uint32_t irValue = 0;
    uint32_t redValue = 0;
    if (xSemaphoreTake(i2cMutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
      irValue = particleSensor.getIR();
      redValue = particleSensor.getRed();
      xSemaphoreGive(i2cMutex);
    }
    else
    {
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }

    if (irValue < 5000)
    {
      Serial.println("No finger detected (IR < 5000)");
      if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(100)) == pdTRUE)
      {
        sensorData.bpm = 0;
        sensorData.spo2 = -999;
        sensorData.spo2_valid = false;
        sensorData.hr_valid = false;
        xSemaphoreGive(dataMutex);
      }
      if (xSemaphoreTake(bufferMutex, pdMS_TO_TICKS(100)) == pdTRUE)
      {
        bufferIndex = 0;
        xSemaphoreGive(bufferMutex);
      }
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }

    int32_t spo2 = -999;
    int8_t spo2Valid = 0;
    int32_t heartRate = -999;
    int8_t heartRateValid = 0;
    bool bufferReady = false;

    if (xSemaphoreTake(bufferMutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
      irBuffer[bufferIndex] = irValue;
      redBuffer[bufferIndex] = redValue;
      bufferIndex++;
      bufferReady = bufferIndex >= PPG_BUFFER_SIZE;

      if (bufferReady)
      {
        maxim_heart_rate_and_oxygen_saturation(
            irBuffer,
            PPG_BUFFER_SIZE,
            redBuffer,
            &spo2,
            &spo2Valid,
            &heartRate,
            &heartRateValid);
        bufferIndex = 0;
      }
      xSemaphoreGive(bufferMutex);
    }

    if (bufferReady)
    {
      if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(100)) == pdTRUE)
      {
        sensorData.spo2 = spo2;
        sensorData.spo2_valid = spo2Valid != 0;
        sensorData.hr_valid = heartRateValid != 0;
        if (heartRateValid)
        {
          sensorData.bpm = heartRate; // Store Maxim BPM
          Serial.println("=== Heart Rate ===");
          Serial.print("Maxim BPM: ");
          Serial.println(heartRate);
        }
        else
        {
          sensorData.bpm = 0; // Clear BPM if invalid
          Serial.println("Invalid Maxim BPM");
        }
        Serial.println("=== SpO2 ===");
        if (spo2Valid)
        {
          Serial.print("SpO2 (%): ");
          Serial.println(spo2);
        }
        else
        {
          Serial.println("Invalid SpO2 from Maxim algorithm");
        }
        xSemaphoreGive(dataMutex);
      }
    }

    vTaskDelay(pdMS_TO_TICKS(10)); // 100Hz sampling (10ms delay)
  }
}

void dht22Task(void *pvParameters)
{
  while (1)
  {
    float temperature = dht.readTemperature();
    float humidity = dht.readHumidity();

    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
      sensorData.temperature = temperature;
      sensorData.humidity = humidity;
      xSemaphoreGive(dataMutex);
    }

    vTaskDelay(pdMS_TO_TICKS(2000)); // Read every 2 seconds
  }
}

void ds18b20Task(void *pvParameters)
{
  while (1)
  {
    if (!ds18b20Available)
    {
      if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(100)) == pdTRUE)
      {
        sensorData.probeTemperature = NAN;
        sensorData.probeTemperature_valid = false;
        xSemaphoreGive(dataMutex);
      }
      vTaskDelay(pdMS_TO_TICKS(2000));
      continue;
    }

    ds18b20.requestTemperatures();
    float probeTemperature = ds18b20.getTempCByIndex(0);
    bool valid = probeTemperature != DEVICE_DISCONNECTED_C &&
                 !isnan(probeTemperature) &&
                 probeTemperature >= -55.0f &&
                 probeTemperature <= 125.0f;

    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
      sensorData.probeTemperature = valid ? probeTemperature : NAN;
      sensorData.probeTemperature_valid = valid;
      xSemaphoreGive(dataMutex);
    }

    if (valid)
    {
      Serial.print("DS18B20 temperature: ");
      Serial.print(probeTemperature, 2);
      Serial.println(" C");
    }
    else
    {
      Serial.println("Invalid DS18B20 reading");
    }

    vTaskDelay(pdMS_TO_TICKS(2000));
  }
}

void ecgTask(void *pvParameters)
{
  while (1)
  {
    int ecgValue = analogRead(ECG_PIN);
    unsigned long now = millis();

    bool leadsOff = digitalRead(LO_PLUS_PIN) == HIGH || digitalRead(LO_MINUS_PIN) == HIGH;
    if (leadsOff)
    {
      if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(100)) == pdTRUE)
      {
        sensorData.ecgValue = 0;
        sensorData.ecgBpm = 0;
        xSemaphoreGive(dataMutex);
      }
      lastEcgBeat = 0;
      ecgAboveThreshold = false;
      Serial.println(0);
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }

    bool aboveThreshold = ecgValue > ecgThreshold;
    if (aboveThreshold && !ecgAboveThreshold &&
        (lastEcgBeat == 0 || now - lastEcgBeat > 300))
    {
      float newEcgBpm = lastEcgBeat == 0 ? 0 : 60000.0f / (now - lastEcgBeat);
      lastEcgBeat = now;

      if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(100)) == pdTRUE)
      {
        sensorData.ecgValue = ecgValue;
        sensorData.ecgBpm = newEcgBpm;
        xSemaphoreGive(dataMutex);
      }
    }
    else if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
      sensorData.ecgValue = ecgValue;
      xSemaphoreGive(dataMutex);
    }

    ecgAboveThreshold = aboveThreshold;

    // Always print raw ECG value for plotting.
    Serial.println(ecgValue);

    vTaskDelay(pdMS_TO_TICKS(10)); // 100Hz sampling
  }
}

void lcdTask(void *pvParameters)
{
  static int displayState = 0;
  while (1)
  {
    SensorData currentData;
    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
      currentData = sensorData;
      xSemaphoreGive(dataMutex);
    }
    else
    {
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    if (xSemaphoreTake(i2cMutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
      lcd.clear();
      lcd.setCursor(0, 0);
      switch (displayState)
      {
        case 0:
          if (currentData.hr_valid && currentData.bpm > 0)
          {
            lcd.print("BPM: ");
            lcd.print((int)currentData.bpm);
          }
          else
          {
            lcd.print("BPM: N/A");
          }
          break;
        case 1:
          if (currentData.spo2_valid && currentData.spo2 >= 0 && currentData.spo2 <= 100)
          {
            lcd.print("SpO2: ");
            lcd.print(currentData.spo2);
            lcd.print("%");
          }
          else
          {
            lcd.print("SpO2: N/A");
          }
          break;
        case 2:
          if (!isnan(currentData.temperature))
          {
            lcd.print("Temp: ");
            lcd.print(currentData.temperature, 1);
            lcd.print((char)223);
            lcd.print("C");
          }
          else
          {
            lcd.print("Temp: N/A");
          }
          break;
        case 3:
          if (!isnan(currentData.humidity))
          {
            lcd.print("Hum: ");
            lcd.print(currentData.humidity, 0);
            lcd.print("%");
          }
          else
          {
            lcd.print("Hum: N/A");
          }
          break;
        case 4:
          lcd.print("ECG: ");
          lcd.print(currentData.ecgValue);
          break;
        case 5:
          if (currentData.probeTemperature_valid && !isnan(currentData.probeTemperature))
          {
            lcd.print("Probe: ");
            lcd.print(currentData.probeTemperature, 1);
            lcd.print((char)223);
            lcd.print("C");
          }
          else
          {
            lcd.print("Probe: N/A");
          }
          break;
      }
      xSemaphoreGive(i2cMutex);
    }

    displayState = (displayState + 1) % 6;
    vTaskDelay(pdMS_TO_TICKS(2000));
  }
}

void thingSpeakTask(void *pvParameters)
{
  while (1)
  {
    SensorData currentData;
    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
      currentData = sensorData;
      xSemaphoreGive(dataMutex);
    }
    else
    {
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    if (WiFi.status() != WL_CONNECTED)
    {
      Serial.println("WiFi disconnected; attempting reconnect");
      connectToWiFi();
    }

    if (WiFi.status() == WL_CONNECTED)
    {
      // Debug all parameters before sending
      Serial.println("=== ThingSpeak Update ===");
      Serial.print("Maxim BPM before send: ");
      Serial.println(currentData.bpm);
      Serial.print("SpO2 before send: ");
      Serial.print(currentData.spo2);
      Serial.print(", Valid: ");
      Serial.println(currentData.spo2_valid);
      Serial.print("ECG Value: ");
      Serial.println(currentData.ecgValue);
      Serial.print("Temperature: ");
      Serial.println(currentData.temperature);
      Serial.print("Humidity: ");
      Serial.println(currentData.humidity);
      Serial.print("Probe Temperature: ");
      Serial.println(currentData.probeTemperature);

      if (currentData.hr_valid && currentData.bpm > 0)
      {
        ThingSpeak.setField(1, currentData.bpm); // Maxim BPM
      }
      if (currentData.spo2_valid && currentData.spo2 >= 0 && currentData.spo2 <= 100)
      {
        ThingSpeak.setField(2, currentData.spo2);
        Serial.print("Sending SpO2 to ThingSpeak: ");
        Serial.println(currentData.spo2);
      }
      else
      {
        Serial.println("Skipping SpO2 send: Invalid or out-of-range value");
      }
      ThingSpeak.setField(3, currentData.ecgValue);
      if (!isnan(currentData.temperature))
      {
        ThingSpeak.setField(4, currentData.temperature);
      }
      if (!isnan(currentData.humidity))
      {
        ThingSpeak.setField(5, currentData.humidity);
      }
      if (currentData.probeTemperature_valid && !isnan(currentData.probeTemperature))
      {
        ThingSpeak.setField(6, currentData.probeTemperature);
      }

      int responseCode = ThingSpeak.writeFields(channelID, apiKey);
      if (responseCode == 200)
      {
        Serial.println("Data sent to ThingSpeak successfully!");
      }
      else
      {
        Serial.print("Error sending data to ThingSpeak: ");
        Serial.println(responseCode);
      }
    }
    else
    {
      Serial.println("ThingSpeak update skipped: WiFi unavailable");
    }

    vTaskDelay(pdMS_TO_TICKS(20000)); // Update every 20 seconds
  }
}

void loop()
{
  // Empty loop; all work is done in FreeRTOS tasks
  vTaskDelay(portMAX_DELAY); // Suspend loop task
}
