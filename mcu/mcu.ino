// #include <ArduinoJson.h>
// #include <DallasTemperature.h>
// #include <OneWire.h>
// //
// // General 
// // Remember to adjust the following slot pin, valve pin, and pump pin based on the actual connections on PCB
// // Aquivio 2.0 PCB and Aquivio 3.0 PCB have different pin connections, so be aware
// const int slotPins[] = {8,9,10,11,12,13,7,6,5,4};
// const int bevvalve = 18;
// const int watervalve = 17;
// const int coldvalve = 21;
// const int warmvalve = 20;
// const int waterpump = 19; 

// // Json Document
// String inputString = "";
// boolean stringComplete = false;

// // Flowmeter
// const int flowmeterPin = A8; // This must be connected to an interrupt pin 
// const int flowConstant = 0.17; // Adjust the flow constant accordingly based on current working pump, this can value varies as you change to a different pump
// volatile int flowPulse = 0;
// bool measuringMode = false;
// double coldWaterUsage = 0;
// double warmWaterUsage = 0;
// float currentUsage;
// boolean isMeasuring = false;

// // Thermometer
// OneWire onewire(A1);
// DallasTemperature temperatureSensor(&onewire);
// unsigned long lastTemperaturePrintTime = 0; 

// // Forward declaring functions
// void checkTemperature();
// void pulseGenerated();
// void printTotalWaterUsage();
// void printWaterUsage(String waterTemp);
// void serialEvent();
// void pump_water(int state, int water_temp);
// void execute_actions(const String& actions);

// void setup() {
//   Serial.begin(9600);
//   inputString.reserve(200); // Allocates a 200 bytes buffer in memory for manipulating string
//   Serial.println("ready");

//   // Set all the pins as outputs and initialize them as LOW
//   for (int i = 0; (unsigned)i < sizeof(slotPins) / sizeof(slotPins[0]); i++) {
//     pinMode(slotPins[i], OUTPUT); 
//     digitalWrite(slotPins[i], LOW);
//   }
//   pinMode(bevvalve, OUTPUT);
//   digitalWrite(bevvalve, LOW);
//   pinMode(watervalve, OUTPUT);
//   digitalWrite(watervalve, LOW);
//   pinMode(coldvalve, OUTPUT);
//   digitalWrite(coldvalve, LOW);
//   pinMode(warmvalve, OUTPUT);
//   digitalWrite(warmvalve, LOW);
//   pinMode(waterpump, OUTPUT);
//   digitalWrite(waterpump, LOW);

//   // Current mode : pin change interrupt @ A8
//   pinMode(flowmeterPin, INPUT_PULLUP);
//   // attachInterrupt(digitalPinToInterrupt(flowmeterPin), pulseGenerated, RISING); // Uncomment this when using external interrupt pin
//   // The following two lines of code calibrate the interrupt control register and pin change mask register
//   // Pin Position @ Port 2 Pin 1
//   PCICR |= 0b00100000; // Comment this out when using external interrupt pin 
//   PCMSK2 |= 0b00000001; // Comment this out when using external interrupt pin
//   // PCIFR |= 0b00000100;
// }

// void loop() {
//   if (stringComplete) {
//     inputString.trim();
//     Serial.println(inputString); // remove all whitespaces and newline character

//     // Check if we received a command to start the cold water pump
//     if (inputString == "startcoldwater") {
//       pump_water(1, 0);  // Start the water pump with cold water
//     }
//     // Check if we received a command to start the warm water pump
//     else if (inputString == "startwarmwater") {
//       pump_water(1, 1);  // Start the water pump with warm water
//     }
//     // Check if we received a command to stop the water pump
//     else if (inputString == "stop") {
//       pump_water(0, 0);  // Stop the water pump, temperature doesn't matter
//       // Stop all the slots
//       for (int i = 0; (unsigned)i < sizeof(slotPins) / sizeof(slotPins[0]); i++) {
//         digitalWrite(slotPins[i], LOW);
//       }
//     }
//     else {
//       // Check if we received a command to start a slot
//       boolean slotStarted = false;
//       for (int i = 0; (unsigned)i < sizeof(slotPins) / sizeof(slotPins[0]); i++) {
//         if (inputString == "startslot" + String(i + 1)) {
//           digitalWrite(slotPins[i], HIGH);
//           slotStarted = true;
//           break;
//         }
//       }

//       // If we didn't receive a specific command to start a slot, treat the input as a JSON string
//       if (!slotStarted) {
//         execute_actions(inputString);
//       }
//     }
//     inputString = "";
//     stringComplete = false;
//   }

//   if (millis() - lastTemperaturePrintTime >= 5000) {
//     lastTemperaturePrintTime = millis();        
//     checkTemperature();
//   }
// }

// void checkTemperature() {
//   temperatureSensor.requestTemperatures();

//   float tempC = temperatureSensor.getTempCByIndex(0);
//   if(tempC != DEVICE_DISCONNECTED_C)
//   {
//     Serial.print(tempC,2);
//   }
//   else
//   {
//     Serial.println("Temperature Sensor Disconnected...");
//   }
// }

// // Only uncomment this when using external interrupt
// // Called when external interrupt is triggered
// // void pulseGenerated() {
// //   sei();
// //   if (measuringMode) {
// //     flowPulse++;
// //   }
// //   cli();
// // }

// // Comment this function out when using pin change interrupt
// // This is an ISR function that will be call everytime pin change interrupt is activated
// ISR (PCINT2_vect) {
//   sei();
//   if (measuringMode) {
//     flowPulse++;
//   }
//   cli();
// }

// // Call this function anytime to check current water usage
// // Including total, warm, and cold water usage
// void printTotalWaterUsage() {
//   Serial.print("Total Water Usage: ");
//   Serial.print(warmWaterUsage + coldWaterUsage);
//   Serial.println("ml");
//   Serial.print("Warm Water Usage: ");
//   Serial.print(warmWaterUsage);
//   Serial.println("ml");
//   Serial.print("Cold Water Usage: ");
//   Serial.print(coldWaterUsage);
//   Serial.println("ml");
// }

// // This function is called everytime after a dispense to show the amount of water used
// void printWaterUsage(String waterTemp) {
//   Serial.print(waterTemp + " Water Usage during This Dispense: ");
//   Serial.print(currentUsage);
//   Serial.println("ml");
// }

// // this function is automatically at the end of the loop when there is serial data available in the buffer
// // Data is coming in the hardware serial RX
// // Sample data input : {"cold":8000,"slot3":2000,"slot5":1000,"slot6":1000,"slot7":1000,"slot8":1000}
// void serialEvent() {
//   while (Serial.available()) {
//     char inChar = (char)Serial.read();
//     inputString += inChar;
//     if (inChar == '\n') {
//       stringComplete = true;
//     }
//   }
// }

// void pump_water(int state, int water_temp) {
//   digitalWrite(watervalve, state == 1 ? HIGH : LOW); // 控制 watervalve 根據 state 的值
//   digitalWrite(waterpump, state == 1 ? HIGH : LOW);
  
//   // 根據 state 和 water_temp 控制 warmvalve
//   if (state == 1) {
//     digitalWrite(warmvalve, water_temp == 1 ? HIGH : LOW);
//     measuringMode = true;
//     isMeasuring = true;
//   } else {
//     digitalWrite(warmvalve, LOW);
//     measuringMode = false;
//     isMeasuring = false;
//     currentUsage = flowPulse * flowConstant;
//     warmWaterUsage += currentUsage;
//     printWaterUsage("Warm");
//   }
  
//   // 根據 state 和 water_temp 控制 coldvalve
//   if (state == 1) {
//     digitalWrite(coldvalve, water_temp == 0 ? HIGH : LOW);
//     measuringMode = true;
//     isMeasuring = true;
//   } else {
//     digitalWrite(coldvalve, LOW);
//     currentUsage = flowPulse * flowConstant;
//     coldWaterUsage += currentUsage;
//     printWaterUsage("Cold");
//     if (isMeasuring == false) {
//       measuringMode = false;
//       isMeasuring = false;
//     }
//   }
// }

// void execute_actions(const String& actions) {
//     int durations[10] = {0};
//     double desiredFlow = 0;  // 變數用來保存期望的流量(ml)
//     int water_temp = 0;
//     Serial.println("start");

//     StaticJsonDocument<200> doc;
//     deserializeJson(doc, actions);

//     for (JsonPair pair : doc.as<JsonObject>()) {
//         String key = pair.key().c_str();
//         int value = pair.value().as<int>();

//         if (key == "cold") {
//             desiredFlow = value;  // 將期望的流量設置為 JSON 中的值
//             water_temp = 0;
//         } else if (key == "warm") {
//             desiredFlow = value;  // 將期望的流量設置為 JSON 中的值
//             water_temp = 1;
//         } else if (key.startsWith("slot")) {
//             int slot_number = key.substring(4).toInt() - 1;
//             if (slot_number >= 0 && slot_number < 10) {
//                 durations[slot_number] = value;
//             }
//         }
//     }

//     unsigned long slotStartTimes[10] = {0};
    
//     // Start the water pump as soon as the first slot is activated
//     bool isWaterPumpStarted = false;
//     double accumulatedFlow = 0;  // 變數用來保存累積的流量

//     for (int i = 0; i < 10; i++) {
//         if (durations[i] > 0) {
//             digitalWrite(slotPins[i], HIGH);
//             slotStartTimes[i] = millis();
//             if (!isWaterPumpStarted) {
//                 pump_water(1, water_temp); // Start the water pump
//                 isWaterPumpStarted = true;
//                 // 重置flowPulse為0，開始計量流量
//                 flowPulse = 0;
//             }
//             delay(500);
//         }
//     }

//     bool isSlotsRunning = true;

//     while (isSlotsRunning || accumulatedFlow < desiredFlow) {
//         unsigned long currentTime = millis();

//         // 檢查每個slot的狀態
//         for (int i = 0; i < 10; i++) {
//             if (slotStartTimes[i] != 0 && currentTime - slotStartTimes[i] >= (unsigned)durations[i]) {
//                 digitalWrite(slotPins[i], LOW);
//                 slotStartTimes[i] = 0;
//             }
//         }

//         // 檢查流量
//         accumulatedFlow = flowPulse * flowConstant;
//         if (accumulatedFlow >= desiredFlow) {
//             pump_water(0, water_temp); // Stop the water pump
//         }

//         // 檢查是否還有任何 slot 在運行
//         isSlotsRunning = false;
//         for (int i = 0; i < 10; i++) {
//             if (slotStartTimes[i] != 0) {
//                 isSlotsRunning = true;
//                 break;
//             }
//         }

//         delay(10);  // 避免while迴圈阻塞
//     }

//     Serial.println("end");

// }


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

void checkTemperature();
void pulseGenerated();
void printTotalWaterUsage();
void printWaterUsage(String waterTemp);
void serialEvent();
void pump_water(int state, int water_temp);
void execute_actions(const String& actions);

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
    PCICR |= 0b00100000;
    PCMSK2 |= 0b00000001;
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

// Your other functions like checkTemperature(), pulseGenerated() etc remain unchanged...

ISR (PCINT2_vect) {
    if (measuringMode) {
        flowPulse++;
    }
}

void pump_water(int state, int water_temp) {
    digitalWrite(watervalve, state == 1 ? HIGH : LOW);

    if (state == 1) {
        flowPulse = 0;
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
            printWaterUsage("Warm");
        } else {
            coldWaterUsage += currentUsage;
            printWaterUsage("Cold");
        }
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
``
