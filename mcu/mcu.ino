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

// Flowmeter variables
const int flowmeterPin = A8;
const int flowConstant = 0.17;
volatile int flowPulse = 0;
bool measuringMode = false;
double coldWaterUsage = 0;
double warmWaterUsage = 0;
float currentUsage;
boolean isMeasuring = false;

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

void loop() {
    if (stringComplete) {
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
            printTotalWaterUsage(); // 印出總水量當泵停止
        } else {
            boolean slotStarted = false;
            for (int i = 0; (unsigned)i < sizeof(slotPins) / sizeof(slotPins[0]); i++) {
                if (inputString == "startslot" + String(i + 1)) {
                    digitalWrite(slotPins[i], HIGH);
                    slotStarted = true;
                    break;
                }
            }
            if (!slotStarted) {
                execute_actions(inputString);
            }
        }
        inputString = "";
        stringComplete = false;
    }

    if (millis() - lastTemperaturePrintTime >= 5000) {
        lastTemperaturePrintTime = millis();
        checkTemperature();
    }
}

void checkTemperature() {
    temperatureSensor.requestTemperatures();
    float tempC = temperatureSensor.getTempCByIndex(0);
    if (tempC != DEVICE_DISCONNECTED_C) {
        Serial.print("Current Temperature: ");
        Serial.print(tempC);
        Serial.print(char(176));
        Serial.print("C  |  ");
        Serial.print((tempC * 9.0) / 5.0 + 32.0);
        Serial.print(char(176));
        Serial.println("F");
    } else {
        Serial.println("Temperature Sensor Disconnected...");
    }
}

ISR (PCINT2_vect) {
    if (measuringMode) {
        flowPulse++;
    }
}

void printTotalWaterUsage() {
    Serial.print("Total Water Usage: ");
    Serial.print(warmWaterUsage + coldWaterUsage);
    Serial.println("ml");
    Serial.print("Warm Water Usage: ");
    Serial.print(warmWaterUsage);
    Serial.println("ml");
    Serial.print("Cold Water Usage: ");
    Serial.print(coldWaterUsage);
    Serial.println("ml");
}

void printWaterUsage(String waterTemp) {
    Serial.print(waterTemp + " Water Usage: ");
    Serial.println(currentUsage);
}

void serialEvent() {
    while (Serial.available()) {
        char inChar = (char)Serial.read();
        inputString += inChar;
        if (inChar == '\n') {
            stringComplete = true;
        }
    }
}

void pump_water(int state, int water_temp) {
    digitalWrite(watervalve, state == 1 ? HIGH : LOW);

    if (state == 1) {
        // 在開始泵動作之前，不要重置flowPulse
        digitalWrite(waterpump, HIGH);
        
        if(water_temp == 1) {
            digitalWrite(warmvalve, HIGH);
            digitalWrite(coldvalve, LOW);
        } else {
            digitalWrite(warmvalve, LOW);
            digitalWrite(coldvalve, HIGH);
        }
        measuringMode = true;
        isMeasuring = true;
    } else {
        digitalWrite(waterpump, LOW);
        digitalWrite(warmvalve, LOW);
        digitalWrite(coldvalve, LOW);
        
        currentUsage = flowPulse * flowConstant;
        if (water_temp == 1) {
            warmWaterUsage += currentUsage;
        } else {
            coldWaterUsage += currentUsage;
        }
        
        // 在總水使用量已經被計算和保存後，再重置flowPulse
        flowPulse = 0;
        measuringMode = false;
        isMeasuring = false;
    }
}


void execute_actions(const String& actions) {
    int durations[10] = {0};
    double desiredFlow = 0;

    int water_temp = 0;
    Serial.println("start");

    StaticJsonDocument<200> doc;
    deserializeJson(doc, actions);

    unsigned long slotStartTimes[10] = {0};
    bool isWaterPumpStarted = false;
    double accumulatedFlow = 0;

    for (int i = 0; i < 10; i++) {
        if (durations[i] > 0) {
            digitalWrite(slotPins[i], HIGH);
            slotStartTimes[i] = millis();
            if (!isWaterPumpStarted) {
                pump_water(1, water_temp);
                isWaterPumpStarted = true;
            }
            delay(500);
        }
    }

    bool isSlotsRunning = true;

    while (isSlotsRunning || accumulatedFlow < desiredFlow) {
        unsigned long currentTime = millis();

        for (int i = 0; i < 10; i++) {
            if (slotStartTimes[i] != 0 && currentTime - slotStartTimes[i] >= (unsigned)durations[i]) {
                digitalWrite(slotPins[i], LOW);
                slotStartTimes[i] = 0;
            }
        }

        accumulatedFlow = flowPulse * flowConstant;
        if (accumulatedFlow >= desiredFlow) {
            pump_water(0, water_temp);
        }

        isSlotsRunning = false;
        for (int i = 0; i < 10; i++) {
            if (slotStartTimes[i] != 0) {
                isSlotsRunning = true;
                break;
            }
        }

        delay(10);
    }

    Serial.println("end");
}
