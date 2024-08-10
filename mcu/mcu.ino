#include <ArduinoJson.h>

#include <ArduinoJson.h>
// #include <DallasTemperature.h>
// #include <OneWire.h>
#include <avr/wdt.h>
#include "MemoryFree.h"

// General variables
const int slotPins[] = {8,9,10,11,12,A13,7,6,5,4};
const int slotCount = sizeof(slotPins) / sizeof(slotPins[0]);
const int bevvalve = 18;
const int watervalve = 17;
const int coldvalve = A15;
const int warmvalve = A14;
const int waterpump = 19; 

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
// OneWire onewire(A1);
// DallasTemperature temperatureSensor(&onewire);
unsigned long lastTemperaturePrintTime = 0; 
unsigned long lastMemoryPrintTime = 0; 
const unsigned long memoryPrintInterval = 30 * 1000; // Print every 1000 ms (1 second)

void setup() {

    wdt_enable(WDTO_8S);

    // Clear reset flags for next time
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
    PCICR |= (1 << PCIE2);   // Enable pin change interrupt for A8 to A15
    PCMSK2 |= (1 << PCINT16);  // Enable pin change interrupt for A8 specifically

    Serial.begin(9600);
    memset(inputString, 0, MAX_INPUT_LENGTH);  // Initialize inputString with null terminators
    Serial.println("ready");
}

void pump_water(int state, int water_temp, double desiredFlow = -1) {
    digitalWrite(watervalve, state == 1 ? HIGH : LOW);

    if (state == 1) {
        digitalWrite(waterpump, HIGH);
        if(water_temp == 1) {
            digitalWrite(warmvalve, HIGH);
            digitalWrite(coldvalve, LOW);
            currentTemp = 1;
        } else {
            digitalWrite(warmvalve, LOW);
            digitalWrite(coldvalve, HIGH);
            currentTemp = 0;
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

        if(currentTemp == 1 ){        
          Serial.print("warm water usage:"); 
          Serial.print(currentUsage,3);
          Serial.print("\n");
        }else{
          Serial.print("cold water usage:"); 
          Serial.print(currentUsage,3);
          Serial.print("\n");
        }

        flowPulse = 0;  // Reset the flow pulse count for the next measurement
        measuringMode = false;
    }
}

void checkTemperature() {
    // temperatureSensor.requestTemperatures();
    // float tempC = temperatureSensor.getTempCByIndex(0);
    // if (tempC != DEVICE_DISCONNECTED_C) {
    //     Serial.println("Temp: " + String(tempC));
    // } else {
    //     Serial.println("No Temp");
    // }
}

volatile unsigned long lastInterruptTime = 0;

volatile int lastFlowState = LOW; // 新增一個全局變量來跟踪上一次的狀態

ISR(PCINT2_vect) {
    int currentFlowState = digitalRead(flowmeterPin); // 讀取當前的pin狀態
    if (lastFlowState == LOW && currentFlowState == HIGH) {
        flowPulse++;
    }
    lastFlowState = currentFlowState; // 更新最後一次的狀態
}

void serialEvent() {
    while (Serial.available()) {
        char inChar = (char)Serial.read();
        
        if (inputLength < MAX_INPUT_LENGTH - 1) {  // Save space for null terminator
            inputString[inputLength++] = inChar;
        }

        if (inChar == '#') {
            stringComplete = true;
        }
    }
}

void execute_actions(const char* actions) {
    double durations[slotCount] = {0};
    double desiredFlow = 0;
    int water_temp = 0;

    Serial.println("execute_actions: start");

    StaticJsonDocument<200> doc;
    DeserializationError error = deserializeJson(doc, actions);

    // Check for deserialization error
    if (error) {
        Serial.println("execute_actions: Failed to parse JSON!");
        return;
    }

    Serial.println("execute_actions: JSON parsed successfully");

    if (doc.containsKey("cold")) {
        desiredFlow = doc["cold"];
        pump_water(1, 0, desiredFlow);
        currentTemp = 1;
        Serial.println("execute_actions: Cold water pump started");
    } else if (doc.containsKey("warm")) {
        desiredFlow = doc["warm"];
        pump_water(1, 1, desiredFlow);
        currentTemp = 0;
        Serial.println("execute_actions: Warm water pump started");
    }

    unsigned long slotStartTimes[10] = {0};
    bool isWaterPumpStarted = (desiredFlow > 0);

    // Sort slots in descending order
    int sortedSlots[10];
    for (int i = 0; i < 10; i++) {
        sortedSlots[i] = 10 - i; // 10, 9, 8, ..., 1
    }

    for (int i = 0; i < 10; i++) {
        String slotKey = "slot" + String(sortedSlots[i]);
        if (doc.containsKey(slotKey)) {
            durations[sortedSlots[i] - 1] = doc[slotKey];
            digitalWrite(slotPins[sortedSlots[i] - 1], HIGH);
            slotStartTimes[sortedSlots[i] - 1] = millis();
            if (!isWaterPumpStarted) {
                isWaterPumpStarted = true;
            }
            Serial.print("execute_actions: Slot ");
            Serial.print(sortedSlots[i]);
            Serial.println(" activated");
            delay(500);
        }
    }

    bool isSlotsRunning = true;
    double accumulatedFlow = 0;
    unsigned long lastLogTime = 0;

    while (isSlotsRunning || accumulatedFlow < desiredFlow) {
        accumulatedFlow = flowPulse * flowConstant;
        if (accumulatedFlow >= desiredFlow && desiredFlow > 0) {
            pump_water(0, water_temp);
            Serial.println("execute_actions: Desired flow reached, pump stopped");
        }

        unsigned long currentTime = millis();
        isSlotsRunning = false;

        for (int i = 0; i < 10; i++) {
            if (slotStartTimes[i] != 0 && currentTime - slotStartTimes[i] >= (unsigned)durations[i]) {
                digitalWrite(slotPins[i], LOW);
                slotStartTimes[i] = 0;
                Serial.print("execute_actions: Slot ");
                Serial.print(i + 1);
                Serial.println(" deactivated");
            } else if (slotStartTimes[i] != 0) {
                isSlotsRunning = true;
            }
        }

        delay(10);
        wdt_reset();

        // Log progress every second
        if (currentTime - lastLogTime >= 1000) {
            Serial.print("execute_actions: Accumulated flow: ");
            Serial.print(accumulatedFlow);
            Serial.print(" / ");
            Serial.println(desiredFlow);
            lastLogTime = currentTime;
        }
    }

    Serial.println("execute_actions: end");
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
        if (inputLength > 0 && inputString[inputLength - 1] == '#') {
            inputString[inputLength - 1] = '\0';  // Replace '#' with null terminator
            if (strcmp(inputString, "ping") != 0) {
                Serial.println(inputString);
            }

            // Processing commands
            if (strcmp(inputString, "ping") == 0) {
                Serial.println("pong\n");
            } else if (strcmp(inputString, "startcoldwater") == 0) {
                pump_water(1, 0);
            } else if (strcmp(inputString, "startwarmwater") == 0) {
                pump_water(1, 1);
            } else if (strcmp(inputString, "stop") == 0) {
                pump_water(0, 0);
                for (int i = 0; i < slotCount; i++) {
                    digitalWrite(slotPins[i], LOW);
                }
            } else {
                boolean slotCommandProcessed = false;

                // Check for start slot commands
                for (int i = 0; i < slotCount; i++) {
                    char command[15];
                    sprintf(command, "startslot%d", i + 1);
                    if (strcmp(inputString, command) == 0) {
                        digitalWrite(slotPins[i], HIGH);
                        slotCommandProcessed = true;
                        break;
                    }
                }

                // Check for stop slot commands
                for (int i = 0; i < slotCount; i++) {
                    char command[15];
                    sprintf(command, "stopslot%d", i + 1);
                    if (strcmp(inputString, command) == 0) {
                        digitalWrite(slotPins[i], LOW);
                        slotCommandProcessed = true;
                        break;
                    }
                }

                // If no slot commands processed, check other actions
                if (!slotCommandProcessed) {
                    execute_actions(inputString);
                }
            }

            inputLength = 0;  // Reset input length
            stringComplete = false;
        }
        
    }

    // if (millis() - lastTemperaturePrintTime >= 300000) {
    //     lastTemperaturePrintTime = millis();
    //     checkTemperature();
    // }
}
