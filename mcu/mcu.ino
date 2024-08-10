#include <ArduinoJson.h>
#include <avr/wdt.h>
#include "MemoryFree.h"

// Constants
const int MAX_SLOTS = 10;
const unsigned long MAX_DURATION = 3600000; // 1 hour in milliseconds
const double MAX_FLOW = 1000.0; // Maximum allowed flow in ml
const int MAX_INPUT_LENGTH = 200;
const unsigned long MEMORY_PRINT_INTERVAL = 30000; // 30 seconds

// Pin Definitions
const int bevvalve = 18;
const int watervalve = 17;
const int coldvalve = A15;
const int warmvalve = A14;
const int waterpump = 19;
const int flowmeterPin = A8;

// Enums
enum class WaterTemperature {
    COLD = 0,
    WARM = 1
};

enum class ExecuteActionError {
    NONE,
    JSON_PARSE_FAILED,
    INVALID_FLOW_VALUE,
    INVALID_SLOT_NUMBER,
    INVALID_DURATION
};

// Structures
struct Slot {
    int pin;
    unsigned long startTime;
    unsigned long duration;
    bool isActive;
};

// Global variables
Slot slots[MAX_SLOTS] = {
    {8, 0, 0, false},
    {9, 0, 0, false},
    {10, 0, 0, false},
    {11, 0, 0, false},
    {12, 0, 0, false},
    {A13, 0, 0, false},
    {7, 0, 0, false},
    {6, 0, 0, false},
    {5, 0, 0, false},
    {4, 0, 0, false}
};

char inputString[MAX_INPUT_LENGTH];
unsigned int inputLength = 0;
boolean stringComplete = false;
int currentTemp = 0;

volatile int flowPulse = 0;
const double flowConstant = 0.0037;
bool measuringMode = false;
double currentUsage = 0;

unsigned long lastMemoryPrintTime = 0;

// Function prototypes
void activateSlot(int slotNumber, unsigned long duration);
void deactivateSlot(int slotNumber);
bool areAnySlotsRunning();
void pumpWater(bool start, WaterTemperature temp, double desiredFlow);
double getAccumulatedFlow();
void logError(const char* message);
ExecuteActionError execute_actions(const char* actions);
void handleExecuteActionError(ExecuteActionError error);

void setup() {
    wdt_enable(WDTO_8S);
    MCUSR = 0;

    for (int i = 0; i < MAX_SLOTS; i++) {
        pinMode(slots[i].pin, OUTPUT);
        digitalWrite(slots[i].pin, LOW);
    }

    pinMode(bevvalve, OUTPUT);
    pinMode(watervalve, OUTPUT);
    pinMode(coldvalve, OUTPUT);
    pinMode(warmvalve, OUTPUT);
    pinMode(waterpump, OUTPUT);
    digitalWrite(bevvalve, LOW);
    digitalWrite(watervalve, LOW);
    digitalWrite(coldvalve, LOW);
    digitalWrite(warmvalve, LOW);
    digitalWrite(waterpump, LOW);

    pinMode(flowmeterPin, INPUT_PULLUP);
    PCICR |= (1 << PCIE2);
    PCMSK2 |= (1 << PCINT16);

    Serial.begin(9600);
    memset(inputString, 0, MAX_INPUT_LENGTH);
    Serial.println("ready");
}

void loop() {
    wdt_reset();

    unsigned long currentMillis = millis();
    if (currentMillis - lastMemoryPrintTime >= MEMORY_PRINT_INTERVAL) {
        Serial.print("Free memory: ");
        Serial.println(freeMemory());
        lastMemoryPrintTime = currentMillis;
    }

    if (stringComplete) {
        if (inputLength > 0 && inputString[inputLength - 1] == '#') {
            inputString[inputLength - 1] = '\0';
            if (strcmp(inputString, "ping") != 0) {
                Serial.println(inputString);
            }

            if (strcmp(inputString, "ping") == 0) {
                Serial.println("pong");
            } else {
                ExecuteActionError result = execute_actions(inputString);
                handleExecuteActionError(result);
            }

            inputLength = 0;
            stringComplete = false;
        }
    }
}

ExecuteActionError execute_actions(const char* actions) {
    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, actions);

    if (error) {
        logError("Failed to parse JSON");
        return ExecuteActionError::JSON_PARSE_FAILED;
    }

    double desiredFlow = 0;
    WaterTemperature waterTemp = WaterTemperature::COLD;

    if (doc.containsKey("cold")) {
        desiredFlow = doc["cold"].as<double>();
        waterTemp = WaterTemperature::COLD;
    } else if (doc.containsKey("warm")) {
        desiredFlow = doc["warm"].as<double>();
        waterTemp = WaterTemperature::WARM;
    }

    if (desiredFlow < 0 || desiredFlow > MAX_FLOW) {
        logError("Invalid flow value");
        return ExecuteActionError::INVALID_FLOW_VALUE;
    }

    pumpWater(true, waterTemp, desiredFlow);

    for (int i = 0; i < MAX_SLOTS; i++) {
        String slotKey = "slot" + String(i + 1);
        if (doc.containsKey(slotKey)) {
            unsigned long duration = doc[slotKey].as<unsigned long>();
            if (duration > MAX_DURATION) {
                logError("Invalid duration for slot");
                return ExecuteActionError::INVALID_DURATION;
            }
            activateSlot(i, duration);
        }
    }

    unsigned long startTime = millis();
    double accumulatedFlow = 0;

    while (areAnySlotsRunning() || accumulatedFlow < desiredFlow) {
        unsigned long currentTime = millis();
        accumulatedFlow = getAccumulatedFlow();

        for (int i = 0; i < MAX_SLOTS; i++) {
            if (slots[i].isActive) {
                unsigned long elapsedTime = currentTime - slots[i].startTime;
                if (elapsedTime >= slots[i].duration) {
                    deactivateSlot(i);
                }
            }
        }

        if (accumulatedFlow >= desiredFlow) {
            pumpWater(false, waterTemp, 0);
        }

        if (currentTime - startTime >= 1000) {
            Serial.print("Accumulated flow: ");
            Serial.print(accumulatedFlow);
            Serial.print(" / ");
            Serial.println(desiredFlow);
            startTime = currentTime;
        }

        delay(10);
        wdt_reset();
    }

    return ExecuteActionError::NONE;
}

void activateSlot(int slotNumber, unsigned long duration) {
    if (slotNumber < 0 || slotNumber >= MAX_SLOTS) {
        logError("Invalid slot number");
        return;
    }
    digitalWrite(slots[slotNumber].pin, HIGH);
    slots[slotNumber].startTime = millis();
    slots[slotNumber].duration = duration;
    slots[slotNumber].isActive = true;
    Serial.print("Slot ");
    Serial.print(slotNumber + 1);
    Serial.println(" activated");
}

void deactivateSlot(int slotNumber) {
    if (slotNumber < 0 || slotNumber >= MAX_SLOTS) {
        logError("Invalid slot number");
        return;
    }
    digitalWrite(slots[slotNumber].pin, LOW);
    slots[slotNumber].isActive = false;
    Serial.print("Slot ");
    Serial.print(slotNumber + 1);
    Serial.println(" deactivated");
}

bool areAnySlotsRunning() {
    for (int i = 0; i < MAX_SLOTS; i++) {
        if (slots[i].isActive) {
            return true;
        }
    }
    return false;
}

void pumpWater(bool start, WaterTemperature temp, double desiredFlow) {
    digitalWrite(watervalve, start ? HIGH : LOW);
    digitalWrite(waterpump, start ? HIGH : LOW);
    
    if (start) {
        if (temp == WaterTemperature::WARM) {
            digitalWrite(warmvalve, HIGH);
            digitalWrite(coldvalve, LOW);
            currentTemp = 1;
        } else {
            digitalWrite(warmvalve, LOW);
            digitalWrite(coldvalve, HIGH);
            currentTemp = 0;
        }
        measuringMode = true;
        Serial.print("Starting water pump. Temperature: ");
        Serial.print(temp == WaterTemperature::COLD ? "Cold" : "Warm");
        Serial.print(", Desired flow: ");
        Serial.println(desiredFlow);
    } else {
        digitalWrite(warmvalve, LOW);
        digitalWrite(coldvalve, LOW);
        measuringMode = false;
        Serial.println("Stopping water pump");
    }
}

double getAccumulatedFlow() {
    currentUsage = flowPulse * flowConstant;
    return currentUsage;
}

void logError(const char* message) {
    Serial.print("ERROR: ");
    Serial.println(message);
}

void handleExecuteActionError(ExecuteActionError error) {
    switch(error) {
        case ExecuteActionError::JSON_PARSE_FAILED:
            Serial.println("JSON parsing failed");
            break;
        case ExecuteActionError::INVALID_FLOW_VALUE:
            Serial.println("Invalid flow value specified");
            break;
        case ExecuteActionError::INVALID_SLOT_NUMBER:
            Serial.println("Invalid slot number specified");
            break;
        case ExecuteActionError::INVALID_DURATION:
            Serial.println("Invalid duration specified");
            break;
        case ExecuteActionError::NONE:
            Serial.println("Action executed successfully");
            break;
    }
}

void serialEvent() {
    while (Serial.available()) {
        char inChar = (char)Serial.read();
        
        if (inputLength < MAX_INPUT_LENGTH - 1) {
            inputString[inputLength++] = inChar;
        }

        if (inChar == '#') {
            stringComplete = true;
        }
    }
}

ISR(PCINT2_vect) {
    int currentFlowState = digitalRead(flowmeterPin);
    static int lastFlowState = LOW;
    if (lastFlowState == LOW && currentFlowState == HIGH) {
        flowPulse++;
    }
    lastFlowState = currentFlowState;
}