#include <ArduinoJson.h>
#include <DallasTemperature.h>
#include <OneWire.h>

// General variables
const int slotPins[] = {8,9,10,11,12,13,7,6,5,4};
const int bevvalve = 18;
const int watervalve = 17;
const int coldvalve = 21;
const int warmvalve = 20;
const int waterpump = 19; 
String inputString = "";
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
OneWire onewire(A1);
DallasTemperature temperatureSensor(&onewire);
unsigned long lastTemperaturePrintTime = 0; 

void setup() {
    Serial.begin(9600);
    inputString.reserve(200);
    Serial.println("ready");

    for (int i = 0; (unsigned)i < sizeof(slotPins) / sizeof(slotPins[0]); i++) {
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

void loop() {
if (!canPrint) return;
if (stringComplete) {
    inputString.replace("#", "");
    inputString.trim();
    Serial.println(inputString);

    // Processing commands
    if (inputString == "startcoldwater") {
        pump_water(1, 0);
    } else if (inputString == "startwarmwater") {
        pump_water(1, 1);
    } else if (inputString == "stop") {
        pump_water(0, 0);
        for (int i = 0; (unsigned)i < sizeof(slotPins) / sizeof(slotPins[0]); i++) {
            digitalWrite(slotPins[i], LOW);
        }
        tempstart = true;
    } else {
        boolean slotCommandProcessed = false;

        // Check for start slot commands
        for (int i = 0; (unsigned)i < sizeof(slotPins) / sizeof(slotPins[0]); i++) {
            if (inputString == "startslot" + String(i + 1)) {
                digitalWrite(slotPins[i], HIGH);
                slotCommandProcessed = true;
                break;
            }
        }

        // Check for stop slot commands
        for (int i = 0; (unsigned)i < sizeof(slotPins) / sizeof(slotPins[0]); i++) {
            if (inputString == "stopslot" + String(i + 1)) {
                digitalWrite(slotPins[i], LOW);
                slotCommandProcessed = true;
                tempstart = true;
                break;
            }
        }

        // If no slot commands processed, check other actions
        if (!slotCommandProcessed) {
            execute_actions(inputString);
        }
    }

    inputString = "";
    stringComplete = false;
}


    if (millis() - lastTemperaturePrintTime >= 3000) {
        lastTemperaturePrintTime = millis();
        checkTemperature();
    }
}

void checkTemperature() {
        temperatureSensor.requestTemperatures();
        float tempC = temperatureSensor.getTempCByIndex(0);
        if (tempC != DEVICE_DISCONNECTED_C) {
        Serial.print("Temp:" + String(tempC) + "\n"); 

        } else {
            Serial.println("No Temp");
        }
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
    canPrint = false;
    while (Serial.available()) {
        char inChar = (char)Serial.read();
        inputString += inChar;
        if (inChar == '#') {
            stringComplete = true;
        }
        tempstart = false;
    }
    canPrint = true;
}


void execute_actions(const String& actions) {
    double durations[10] = {0};
    double desiredFlow = 0;
    int water_temp = 0;

    Serial.println("start");

    StaticJsonDocument<200> doc;
    DeserializationError error = deserializeJson(doc, actions);

    // Check for deserialization error
    if (error) {
        Serial.println("Failed to parse JSON!");
        return;
    }

    if (doc.containsKey("cold")) {
        desiredFlow = doc["cold"];
        pump_water(1, 0, desiredFlow);
        currentTemp = 1;
    } else if (doc.containsKey("warm")) {
        desiredFlow = doc["warm"];
        pump_water(1, 1, desiredFlow);
        currentTemp = 0;
    }

    unsigned long slotStartTimes[10] = {0};
    bool isWaterPumpStarted = (desiredFlow > 0);

    for (int i = 0; i < 10; i++) {
        String slotKey = "slot" + String(i + 1);
        if (doc.containsKey(slotKey)) {
            durations[i] = doc[slotKey];
            digitalWrite(slotPins[i], HIGH);
            slotStartTimes[i] = millis();
            if (!isWaterPumpStarted) {
                isWaterPumpStarted = true;
            }
            delay(500);
        }
    }

    bool isSlotsRunning = true;
    double accumulatedFlow = 0;

    while (isSlotsRunning || accumulatedFlow < desiredFlow) {
        accumulatedFlow = flowPulse * flowConstant;
        if (accumulatedFlow >= desiredFlow) {
            pump_water(0, water_temp);
        }

        unsigned long currentTime = millis();
        isSlotsRunning = false;

        for (int i = 0; i < 10; i++) {
            if (slotStartTimes[i] != 0 && currentTime - slotStartTimes[i] >= (unsigned)durations[i]) {
                digitalWrite(slotPins[i], LOW);
                slotStartTimes[i] = 0;
            } else if (slotStartTimes[i] != 0) {
                isSlotsRunning = true;
            }
        }

        delay(10);
    }

    Serial.println("end");
}
