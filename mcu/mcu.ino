#include "ArduinoJson.h"
#include <avr/wdt.h>
#include "MemoryFree.h"

// General variables
const int slotPins[] = {8,9,10,11,12,3,7,6,5,4};
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
volatile bool flowPulseOccurred = false;
bool measuringMode = false;
double coldWaterUsage = 0;
double warmWaterUsage = 0;
double currentUsage;
bool isMeasuring = false;
bool canPrint = true;

// Memory variables
unsigned long lastMemoryPrintTime = 0; 
const unsigned long memoryPrintInterval = 30 * 1000; // Print every 30 seconds
unsigned long lastmcuAliveTime = 0; 
const unsigned long mcuAliveInterval = 1 * 1000; // Print every 1000 ms (1 second)


void setupWatchdog() {
    cli();  // Disable interrupts
    wdt_reset();  // Reset Watchdog Timer
    MCUSR &= ~(1<<WDRF);  // Clear WDRF in MCUSR
    WDTCSR |= (1<<WDCE) | (1<<WDE);  // Set WDCE (Watchdog Change Enable)
    WDTCSR = (1<<WDE) | (1<<WDP3) | (1<<WDP0);  // Set WDE, and 8 seconds delay
    sei();  // Enable interrupts
}

void setup() {
    setupWatchdog();

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

void checkMemory() {
    static int lowestMemory = freeMemory();
    int currentMemory = freeMemory();
    
    if (currentMemory < lowestMemory) {
        lowestMemory = currentMemory;
        Serial.print("New lowest memory: ");
        Serial.print(lowestMemory);
        Serial.println(" bytes");
    }
    
    if (currentMemory < 100) {  // Arbitrary low memory threshold
        Serial.println("WARNING: Very low memory!");
    }
}

void checkSerialBuffers() {
    // Check RX buffer
    if (Serial.available() > 60) {  // Assuming a 64-byte RX buffer
        Serial.println("WARNING: RX buffer near capacity");
        while (Serial.available() > 0) {
            Serial.read();  // Clear the buffer
        }
    }

    // Check TX buffer
    if (Serial.availableForWrite() < 32) {  // Less than half of TX buffer available
        // Serial.println("WARNING: TX buffer getting full");
        Serial.flush();  // Wait for all data to be transmitted
    }
}

void nonBlockingPrint(const char* message) {
    static char printBuffer[64];  // Adjust size as needed
    static int printIndex = 0;
    
    if (message != NULL) {
        // New message to print
        strncpy(printBuffer, message, sizeof(printBuffer) - 1);
        printBuffer[sizeof(printBuffer) - 1] = '\0';  // Ensure null-termination
        printIndex = 0;
    }
    
    while (printIndex < strlen(printBuffer) && Serial.availableForWrite() > 0) {
        Serial.write(printBuffer[printIndex++]);
    }
    
    if (printIndex >= strlen(printBuffer)) {
        printBuffer[0] = '\0';  // Clear buffer after fully printed
        printIndex = 0;
    }
}

ISR(PCINT2_vect) {
    int currentFlowState = digitalRead(flowmeterPin);
    static int lastFlowState = LOW;
    if (lastFlowState == LOW && currentFlowState == HIGH) {
        flowPulse++;
        flowPulseOccurred = true;
    }
    lastFlowState = currentFlowState;
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

void activateSlot(int slotNumber, unsigned long duration) {
    digitalWrite(slotPins[slotNumber], HIGH);
    Serial.print("Activated slot ");
    Serial.print(slotNumber + 1);
    Serial.print(" for duration: ");
    Serial.println(duration);
}

void deactivateSlot(int slotNumber) {
    digitalWrite(slotPins[slotNumber], LOW);
    Serial.print("Deactivated slot ");
    Serial.println(slotNumber + 1);
}

bool areAnySlotsActive(const unsigned long* slotStartTimes, const double* durations) {
    unsigned long currentTime = millis();
    for (int i = 0; i < slotCount; i++) {
        if (slotStartTimes[i] != 0 && currentTime - slotStartTimes[i] < (unsigned)durations[i]) {
            return true;
        }
    }
    return false;
}

void handleWaterFlow(JsonDocument& doc, double& desiredFlow, int& waterTemp) {
    if (doc.containsKey("cold")) {
        desiredFlow = doc["cold"];
        pump_water(1, 0, desiredFlow);
        waterTemp = 0;
        Serial.print("Cold water flow requested: ");
    } else if (doc.containsKey("warm")) {
        desiredFlow = doc["warm"];
        pump_water(1, 1, desiredFlow);
        waterTemp = 1;
        Serial.print("Warm water flow requested: ");
    } else {
        Serial.println("No water flow requested");
        return;
    }
    Serial.println(desiredFlow);
}

void activateRequestedSlots(JsonDocument& doc, unsigned long* slotStartTimes, double* durations) {
    for (int i = slotCount; i > 0; i--) {
        String slotKey = "slot" + String(i);
        if (doc.containsKey(slotKey)) {
            durations[i-1] = doc[slotKey];
            slotStartTimes[i-1] = millis();
            activateSlot(i-1, durations[i-1]);
            delay(500);
        }
    }
}

void execute_actions(const char* actions) {
    Serial.println("start");
    Serial.print("Received actions: ");
    Serial.println(actions);

    StaticJsonDocument<200> doc;
    DeserializationError error = deserializeJson(doc, actions);

    if (error) {
        Serial.print("Failed to parse JSON: ");
        Serial.println(error.c_str());
        return;
    }

    double desiredFlow = 0;
    int waterTemp = 0;
    handleWaterFlow(doc, desiredFlow, waterTemp);

    unsigned long slotStartTimes[slotCount] = {0};
    double durations[slotCount] = {0};
    activateRequestedSlots(doc, slotStartTimes, durations);

    double accumulatedFlow = 0;
    unsigned long lastProgressLog = millis();
    unsigned long startTime = millis();
    const unsigned long TIMEOUT = 120 * 1000;  // 2 minutes timeout

    while ((areAnySlotsActive(slotStartTimes, durations) || accumulatedFlow < desiredFlow) && (millis() - startTime < TIMEOUT)) {
        accumulatedFlow = flowPulse * flowConstant;

        if (accumulatedFlow >= desiredFlow && desiredFlow > 0) {
            Serial.println("Desired flow reached, stopping water pump");
            pump_water(0, waterTemp);
        }

        for (int i = 0; i < slotCount; i++) {
            if (slotStartTimes[i] != 0 && millis() - slotStartTimes[i] >= (unsigned)durations[i]) {
                deactivateSlot(i);
                slotStartTimes[i] = 0;
            }
        }

        if (millis() - lastProgressLog >= 1000) {
            nonBlockingPrint("Progress - Accumulated flow: ");
            nonBlockingPrint(String(accumulatedFlow).c_str());
            nonBlockingPrint(" / Desired flow: ");
            nonBlockingPrint(String(desiredFlow).c_str());
            nonBlockingPrint("\n");
            lastProgressLog = millis();
        }

        checkMemory();
        checkSerialBuffers();
        delay(10);
        wdt_reset();

        if (millis() - startTime >= TIMEOUT) {
            Serial.println("ERROR: Execution timeout reached");
            break;
        }
    }

    // Ensure everything is turned off
    pump_water(0, waterTemp);
    for (int i = 0; i < slotCount; i++) {
        deactivateSlot(i);
    }

    Serial.println("end");
}

void loop() {
    wdt_reset();

    if (!canPrint) return;

    unsigned long currentMillis = millis();
    if (currentMillis - lastMemoryPrintTime >= memoryPrintInterval) {
        checkMemory();
        lastMemoryPrintTime = currentMillis;
    }
    
    if (currentMillis - lastmcuAliveTime >= mcuAliveInterval) {
        Serial.print("mcuAlive \n");
        lastmcuAliveTime = currentMillis;
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

    if (flowPulseOccurred) {
        // Handle flow pulse if needed
        flowPulseOccurred = false;
    }

    nonBlockingPrint(NULL);  // Continue any non-blocking print operations
    checkSerialBuffers();
}