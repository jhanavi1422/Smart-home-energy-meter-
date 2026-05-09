#define BLYNK_TEMPLATE_ID "TMPL27Pdxhh84"
#define BLYNK_TEMPLATE_NAME "SMART HOME"
#define BLYNK_AUTH_TOKEN "vaQWhNGXXMSx9tmMxRB1KLjZh6UmLpXC"

// Comment this out to disable prints and save space
#define BLYNK_PRINT Serial

#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include "EmonLib.h"  // Include the EmonLib library for energy meter functionality
#include <LiquidCrystal_I2C.h>  // Include the LiquidCrystal_I2C library for LCD
#include <EEPROM.h>  // Include the EEPROM library
#include <SD.h>  // Include the SD card library

// Wi-Fi credentials
const char *ssid = "Wokwi-GUEST";
const char *password = "";

// Virtual pins for controlling the relay and sending energy data
#define RELAY_VPIN V5
#define VRMS_VPIN V0  // Virtual pin for sending Vrms (root mean square voltage)
#define IRMS_VPIN V1  // Virtual pin for sending Irms (root mean square current)
#define POWER_VPIN V2  // Virtual pin for sending apparent power
#define KWH_VPIN V3  // Virtual pin for sending kilowatt-hours (kWh)
#define COST_VPIN V4  // Virtual pin for sending the cost of energy consumption

// Pin assignment for relay and sensors
const int RELAY_PIN = 13;
const int VOLTAGE_PIN = 35;  // Voltage sensor pin
const int CURRENT_PIN = 34;  // Current sensor pin
const int SD_CS_PIN = 5;  // Chip select pin for SD card

// Relay state variable
int relayState = 0;

// EmonLib energy monitor instance
EnergyMonitor emon;

// Blynk Timer
BlynkTimer timer;

// LCD settings: 16 columns and 2 rows
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Kilowatt-hours variable and SD card file
float kWh = 0.0;
unsigned long lastMillis = millis();
File energyFile;

// Energy cost rate (MAD per kWh)
const float costPerKWh = 2.0;

// BLYNK_WRITE function to handle relay control from the Blynk app
BLYNK_WRITE(RELAY_VPIN) {
    relayState = param.asInt();
    digitalWrite(RELAY_PIN, relayState);
    // Debugging: Print relay state
    Serial.printf("Relay state: %d\n", relayState);
}

void setup() {
    // Debug console
    Serial.begin(115200);
    
    // Initialize LCD
    lcd.init();
    lcd.backlight();
    
    // Display startup message
    lcd.setCursor(0, 0);
    lcd.print("SMART SOCKET");
    lcd.setCursor(0, 1);
    lcd.print("Starting...");

    // Set relay pin mode as output
    pinMode(RELAY_PIN, OUTPUT);
    
    // Turn off relay initially
    digitalWrite(RELAY_PIN, LOW);
    
    // Set up the energy monitor
    emon.voltage(VOLTAGE_PIN, 230.0, 1.7);  // Voltage sensor: pin, calibration, phase shift
    emon.current(CURRENT_PIN, 5);  // Current sensor: pin, calibration
    
    // Initialize SD card
    initSDCard();

    // Connect to Wi-Fi
    connectToWiFi();
    
    // Initialize Blynk
    Blynk.begin(BLYNK_AUTH_TOKEN, ssid, password);
    
    // Set timer for sending energy data every 5 seconds
    timer.setInterval(5000L, sendEnergyDataToBlynk);
}

void loop() {
    Blynk.run();
    timer.run();
}

void connectToWiFi() {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Connecting to");
    lcd.setCursor(0, 1);
    lcd.print("WiFi...");
    
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print("...");
    }

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("WiFi Connected");
    delay(2000);  // Show message for 2 seconds
    lcd.clear();
}

void sendEnergyDataToBlynk() {
    // Calculate all values using EmonLib (e.g., Vrms, Irms, power)
    emon.calcVI(20, 2000);

    // Calculate energy consumed in kWh
    unsigned long currentMillis = millis();
    kWh += emon.apparentPower * (currentMillis - lastMillis) / 3600000000.0;
    lastMillis = currentMillis;

    // Calculate the cost of energy consumed
    float energyCost = kWh * costPerKWh;

    if (WiFi.status() == WL_CONNECTED) {
        // Send energy data to Blynk
        Blynk.virtualWrite(VRMS_VPIN, emon.Vrms);
        Blynk.virtualWrite(IRMS_VPIN, emon.Irms);
        Blynk.virtualWrite(POWER_VPIN, emon.apparentPower);
        Blynk.virtualWrite(KWH_VPIN, kWh);
        Blynk.virtualWrite(COST_VPIN, energyCost);

        // Debugging: Print measured values
        Serial.printf("Vrms: %.2fV, Irms: %.2fA, Power: %.2fW, kWh: %.5f, Cost: %.2f MAD\n", emon.Vrms, emon.Irms, emon.apparentPower, kWh, energyCost);
    } else {
        // Debugging: Print measured values (without Blynk)
        Serial.printf("[WiFi Disconnected] Vrms: %.2fV, Irms: %.2fA, Power: %.2fW, kWh: %.5f, Cost: %.2f MAD\n", emon.Vrms, emon.Irms, emon.apparentPower, kWh, energyCost);
    }

    // Update the LCD with the new values
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("V:");
    lcd.print(emon.Vrms, 2);
    lcd.print("V");

    lcd.setCursor(8, 0);
    lcd.print("P:");
    lcd.print(emon.apparentPower, 2);
    lcd.print("W");

    lcd.setCursor(0, 1);
    lcd.print("I:");
    lcd.print(emon.Irms, 2);
    lcd.print("A");

    lcd.setCursor(8, 1);
    lcd.print("kWh:");
    lcd.print(kWh, 4);

    // Save kWh data to SD card
    saveEnergyDataToSDCard();
}

void initSDCard() {
    if (!SD.begin(SD_CS_PIN)) {
        Serial.println("SD card initialization failed!");
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("SD Init Failed");
        while (1);  // Hang if SD initialization fails
    }
    Serial.println("SD card initialized.");
}

void saveEnergyDataToSDCard() {
    // Open or create the energy log file on the SD card
    energyFile = SD.open("/energy_log.txt", FILE_WRITE);
    
    if (energyFile) {
        energyFile.printf("Vrms: %.2fV, Irms: %.2fA, Power: %.2fW, kWh: %.5f, Cost: %.2f MAD\n", emon.Vrms, emon.Irms, emon.apparentPower, kWh, kWh * costPerKWh);
        energyFile.close();  // Close the file after writing
        Serial.println("Energy data saved to SD.");
    } else {
        Serial.println("Error opening energy_log.txt");
    }
}

void readEnergyDataFromSDCard() {
    // Open the energy log file on the SD card for reading
    energyFile = SD.open("/energy_log.txt");
    
    if (energyFile) {
        Serial.println("Energy log data:");
        while (energyFile.available()) {
            Serial.write(energyFile.read());
        }
        energyFile.close();
    } else {
        Serial.println("Error reading energy_log.txt");
    }
}
