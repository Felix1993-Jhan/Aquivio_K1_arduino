#include <ArduinoJson.h>
#include <avr/wdt.h>
#include "MemoryFree.h"

// General variables
const int slotPins[] = {8, 9, 10, 11, 12, 3, 7, 6, 5, 4};
const int slotCount = sizeof(slotPins) / sizeof(slotPins[0]);
const int bevvalve = 18;
const int watervalve = 17;
const int coldvalve = A15;
const int warmvalve = A14;
const int waterpump = A13;

#define MAX_INPUT_LENGTH 200
char inputString[MAX_INPUT_LENGTH];
unsigned int inputLength = 0;
boolean stringComplete = false;
int currentTemp = 0;

// Flowmeter variables
const int flowmeterPin = A8;
const double flowConstant = 0.0037;
volatile int flowPulse = 0;
bool measuringMode = false;
double coldWaterUsage = 0;
double warmWaterUsage = 0;
double currentUsage;
bool isMeasuring = false;
bool canPrint = true;

// Thermometer variables
unsigned long lastTemperaturePrintTime = 0;
unsigned long lastMemoryPrintTime = 0;
const unsigned long memoryPrintInterval = 30 * 1000; // Print every 30 seconds

struct SlotAction {
    int pin;
    double duration;
};

void setup() {
    wdt_enable(WDTO_8S);
    MCUSR = 0;

    for (int i = 0; i < slotCount; i++) {
        pinMode(slotPins[i], OUTPUT);
        digitalWrite(slotPins[i], LOW);
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

void pump_water(int state, int water_temp, double desiredFlow = -1) {
    digitalWrite(watervalve, state ? HIGH : LOW);

    if (state) {
        digitalWrite(waterpump, HIGH);
        if (water_temp == 1) {
            digitalWrite(warmvalve, HIGH);
            digitalWrite(coldvalve, LOW);
        } else {
            digitalWrite(warmvalve, LOW);
            digitalWrite(coldvalve, HIGH);
        }
        measuringMode = true;
    } else {
        digitalWrite(waterpump, LOW);
        digitalWrite(warmvalve, LOW);
        digitalWrite(coldvalve, LOW);

        currentUsage = flowPulse * flowConstant;
        Serial.print("pulse:");
        Serial.print(flowPulse);
        Serial.print("\n");

        if (currentTemp == 1) {
            Serial.print("warm water usage:");
            Serial.print(currentUsage, 3);
            Serial.print("\n");
        } else {
            Serial.print("cold water usage:");
            Serial.print(currentUsage, 3);
            Serial.print("\n");
        }

        flowPulse = 0;
        measuringMode = false;
    }
}

ISR(PCINT2_vect) {
    int currentFlowState = digitalRead(flowmeterPin);
    if (lastFlowState == LOW && currentFlowState == HIGH) {
        flowPulse++;
    }
    lastFlowState = currentFlowState;
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

void execute_actions(const char* actions) {
    StaticJsonDocument<200> doc;
    DeserializationError error = deserializeJson(doc, actions);

    if (error) {
        Serial.println("Failed to parse JSON!");
        return;
    }

    SlotAction slots[slotCount];
    for (int i = 0; i < slotCount; i++) {
        slots[i].pin = slotPins[i];
        String slotKey = "slot" + String(i + 1);
        if (doc.containsKey(slotKey)) {
            slots[i].duration = doc[slotKey];
        } else {
            slots[i].duration = 0;
        }
    }

    // Sort slots by duration in descending order
    std::sort(slots, slots + slotCount, [](const SlotAction& a, const SlotAction& b) {
        return a.duration > b.duration;
    });

    // Execute slot actions
    for (int i = 0; i < slotCount; i++) {
        if (slots[i].duration > 0) {
            digitalWrite(slots[i].pin, HIGH);
            delay(slots[i].duration);
            digitalWrite(slots[i].pin, LOW);
        }
    }

    Serial.println("end");
}

void loop() {
    wdt_reset();

    if (!canPrint) return;

    unsigned long currentMillis = millis();
    if (currentMillis - lastMemoryPrintTime >= memoryPrintInterval) {
        Serial.print("Free memory: ");
        Serial.println(freeMemory());
        lastMemoryPrintTime = currentMillis;
    }

    if (stringComplete) {
        inputString[inputLength - 1] = '\0'; // Replace '#' with null terminator

        if (strcmp(inputString, "ping") == 0) {
            Serial.println("pong");
        } else {
            execute_actions(inputString);
        }

        inputLength = 0;  // Reset input length
        stringComplete = false;
    }
}
