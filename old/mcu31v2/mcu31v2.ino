#include <ArduinoJson.h>
#include <avr/wdt.h>
#include "MemoryFree.h"

// -------------------- General variables --------------------
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

// -------------------- Flowmeter variables --------------------
const int flowmeterPin = A8;
const double flowConstant = 0.0033;   // L per pulse（依實測校正）
volatile int flowPulse = 0;
volatile bool flowPulseOccurred = false;
bool measuringMode = false;
double coldWaterUsage = 0;
double warmWaterUsage = 0;
double currentUsage;
bool isMeasuring = false;
bool canPrint = true;

// -------------------- Min-Time guard (for cold/warm) --------------------
unsigned long dispenseStartMs = 0;    // 本次出水開始時間
unsigned long minTimeMs = 0;          // 應需時間（毫秒）；0=不啟用
bool timeGuardEnabled = false;        // 是否啟用時間下限
bool stoppedByFlow = false;           // 這次是否由 flow 停
bool stoppedByTime = false;           // 這次是否由 time 停（代表先到量、等時間）
bool dispenseStopped = false;         // 是否已正常停水

// 先後判斷（誰先達成）
bool volumeReachedOnce = false;
bool timeReachedOnce = false;
unsigned long volumeReachedMs = 0;
unsigned long timeReachedMs = 0;

// -------------------- Memory / Alive --------------------
unsigned long lastMemoryPrintTime = 0;
const unsigned long memoryPrintInterval = 30 * 1000; // 30s
unsigned long lastmcuAliveTime = 0;
const unsigned long mcuAliveInterval = 1 * 1000;     // 1s

// -------------------- Watchdog --------------------
void setupWatchdog() {
  cli();
  wdt_reset();
  MCUSR &= ~(1<<WDRF);
  WDTCSR |= (1<<WDCE) | (1<<WDE);
  WDTCSR = (1<<WDE) | (1<<WDP3) | (1<<WDP0); // ~8s
  sei();
}

// -------------------- Setup --------------------
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
  PCICR  |= (1 << PCIE2);    // A8..A15 pin-change
  PCMSK2 |= (1 << PCINT16);  // A8

  Serial.begin(9600);
  memset(inputString, 0, MAX_INPUT_LENGTH);
  Serial.println("ready");
}

// -------------------- Pump control --------------------
void pump_water(int state, int water_temp, double desiredFlow = -1) {
  digitalWrite(watervalve, state == 1 ? HIGH : LOW);

  if (state == 1) {
    digitalWrite(waterpump, HIGH);
    if (water_temp == 1) {
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

    if (currentTemp == 1) {
      Serial.print("warm water usage:");
      Serial.print(currentUsage, 3);
      Serial.print("\n");
    } else {
      Serial.print("cold water usage:");
      Serial.print(currentUsage, 3);
      Serial.print("\n");
    }

    flowPulse = 0;    // reset for next run
    measuringMode = false;
  }
}

// -------------------- Utils --------------------
void checkMemory() {
  static int lowestMemory = freeMemory();
  int currentMemory = freeMemory();
  if (currentMemory < lowestMemory) {
    lowestMemory = currentMemory;
    Serial.print("New lowest memory: ");
    Serial.print(lowestMemory);
    Serial.println(" bytes");
  }
  if (currentMemory < 100) {
    Serial.println("WARNING: Very low memory!");
  }
}

void checkSerialBuffers() {
  if (Serial.available() > 60) {
    Serial.println("WARNING: RX buffer near capacity");
    while (Serial.available() > 0) Serial.read();
  }
  if (Serial.availableForWrite() < 32) {
    Serial.flush();
  }
}

void nonBlockingPrint(const char* message) {
  static char printBuffer[64];
  static int printIndex = 0;
  if (message != NULL) {
    strncpy(printBuffer, message, sizeof(printBuffer) - 1);
    printBuffer[sizeof(printBuffer) - 1] = '\0';
    printIndex = 0;
  }
  while (printIndex < (int)strlen(printBuffer) && Serial.availableForWrite() > 0) {
    Serial.write(printBuffer[printIndex++]);
  }
  if (printIndex >= (int)strlen(printBuffer)) {
    printBuffer[0] = '\0';
    printIndex = 0;
  }
}

// -------------------- Flowmeter ISR --------------------
ISR(PCINT2_vect) {
  int currentFlowState = digitalRead(flowmeterPin);
  static int lastFlowState = LOW;
  if (lastFlowState == LOW && currentFlowState == HIGH) {
    flowPulse++;
    flowPulseOccurred = true;
  }
  lastFlowState = currentFlowState;
}

// -------------------- Serial collect --------------------
void serialEvent() {
  while (Serial.available()) {
    char inChar = (char)Serial.read();
    if (inputLength < MAX_INPUT_LENGTH - 1) inputString[inputLength++] = inChar;
    if (inChar == '#') stringComplete = true;
  }
}

// -------------------- Slot helpers --------------------
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
  unsigned long now = millis();
  for (int i = 0; i < slotCount; i++) {
    if (slotStartTimes[i] != 0 && now - slotStartTimes[i] < (unsigned)durations[i]) {
      return true;
    }
  }
  return false;
}

// -------------------- Water command parse --------------------
void handleWaterFlow(JsonDocument& doc, double& desiredFlow, int& waterTemp) {
  // reset flags
  timeGuardEnabled   = false;
  minTimeMs          = 0;
  stoppedByFlow      = false;
  stoppedByTime      = false;
  dispenseStopped    = false;
  volumeReachedOnce  = false;
  timeReachedOnce    = false;
  volumeReachedMs    = 0;
  timeReachedMs      = 0;

  if (doc.containsKey("cold")) {
    desiredFlow = String((const char*)doc["cold"]).toFloat();
    pump_water(1, 0, desiredFlow);
    waterTemp = 0;
    Serial.print("Cold water flow requested: ");
  } else if (doc.containsKey("warm")) {
    desiredFlow = String((const char*)doc["warm"]).toFloat();
    pump_water(1, 1, desiredFlow);
    waterTemp = 1;
    Serial.print("Warm water flow requested: ");
  } else {
    Serial.println("No water flow requested");
    return;
  }

  if (doc.containsKey("time")) {
    minTimeMs = (unsigned long)doc["time"].as<unsigned long>();
    timeGuardEnabled = (minTimeMs > 0);
  }
  dispenseStartMs = millis();

  Serial.println(desiredFlow);
}

// -------------------- Activate slots from JSON --------------------
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

// -------------------- Main executor --------------------
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
  const unsigned long TIMEOUT = 120 * 1000; // 2 min safety

  // 以 for(;;) 執行，並在迴圈內決定是否繼續或收尾，避免邊界跳出
  for (;;) {
    // Timeout 防護
    if (millis() - startTime >= TIMEOUT) {
      Serial.println("ERROR: Execution timeout reached");
      break;
    }

    accumulatedFlow = flowPulse * flowConstant;

    // 條件評估
    bool anySlotActive = areAnySlotsActive(slotStartTimes, durations);

    bool reachVolume = (desiredFlow > 0) && (accumulatedFlow >= desiredFlow);
    if (reachVolume && !volumeReachedOnce) { volumeReachedOnce = true; volumeReachedMs = millis(); }

    unsigned long elapsed = millis() - dispenseStartMs;
    bool reachTime = (!timeGuardEnabled) || (elapsed >= minTimeMs);
    if (reachTime && !timeReachedOnce) { timeReachedOnce = true; timeReachedMs = millis(); }

    // 正常停水條件：同時成立（到量 AND 時間）
    if (!dispenseStopped && reachVolume && reachTime) {
      if (timeGuardEnabled) {
        // 先到量後等時間 → by time；先時間後到量 → by flow
        if (volumeReachedMs > 0 && timeReachedMs > 0) {
          stoppedByTime = (volumeReachedMs < timeReachedMs);
          stoppedByFlow = (timeReachedMs <= volumeReachedMs);
        } else if (volumeReachedMs > 0) {
          stoppedByFlow = true;
        } else {
          stoppedByFlow = true;
        }
      } else {
        stoppedByFlow = true;
      }

      Serial.print("Stopping water pump (volume & time satisfied). ");
      if (timeGuardEnabled) { Serial.print("min time = "); Serial.print(minTimeMs); Serial.print(" ms. "); }
      Serial.println();

      pump_water(0, waterTemp);
      desiredFlow = 0;       // 防重入
      dispenseStopped = true;
    }

    // slot 倒數
    for (int i = 0; i < slotCount; i++) {
      if (slotStartTimes[i] != 0 && millis() - slotStartTimes[i] >= (unsigned)durations[i]) {
        deactivateSlot(i);
        slotStartTimes[i] = 0;
      }
    }

    // 進度輸出
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

    // 判斷是否要繼續：任何 slot 在跑，或出水尚未同時滿足（到量 AND 時間）
    bool needMoreWater =
      anySlotActive ||
      (
        (desiredFlow > 0 && accumulatedFlow < desiredFlow) ||
        (timeGuardEnabled && (elapsed < minTimeMs))
      );

    if (!needMoreWater) break;  // 條件滿足 → 收尾
  }

  // 保險關
  pump_water(0, waterTemp);
  for (int i = 0; i < slotCount; i++) deactivateSlot(i);

  // finish 回報
  if (dispenseStopped) {
    if (stoppedByTime) {
      Serial.println("finish: stopped by minimum time constraint");
    } else if (stoppedByFlow) {
      Serial.println("finish: stopped by flow pulse threshold");
    } else {
      Serial.println("finish: stopped by (volume & time) unspecified");
    }
  } else {
    Serial.println("finish: stopped by system timeout or external stop");
  }

  Serial.println("end");
}

// -------------------- Loop --------------------
void loop() {
  wdt_reset();
  if (!canPrint) return;

  unsigned long now = millis();
  if (now - lastMemoryPrintTime >= memoryPrintInterval) {
    checkMemory();
    lastMemoryPrintTime = now;
  }
  if (now - lastmcuAliveTime >= mcuAliveInterval) {
    Serial.print("mcuAlive \n");
    lastmcuAliveTime = now;
  }

  if (stringComplete) {
    if (inputLength > 0 && inputString[inputLength - 1] == '#') {
      inputString[inputLength - 1] = '\0';  // replace '#' with '\0'
      if (strcmp(inputString, "ping") != 0) Serial.println(inputString);

      if (strcmp(inputString, "ping") == 0) {
        Serial.println("pong\n");
      } else if (strcmp(inputString, "startcoldwater") == 0) {
        pump_water(1, 0);
      } else if (strcmp(inputString, "startwarmwater") == 0) {
        pump_water(1, 1);
      } else if (strcmp(inputString, "stop") == 0) {
        pump_water(0, 0);
        for (int i = 0; i < slotCount; i++) digitalWrite(slotPins[i], LOW);
      } else {
        boolean slotCommandProcessed = false;

        // start slot
        for (int i = 0; i < slotCount; i++) {
          char command[15];
          sprintf(command, "startslot%d", i + 1);
          if (strcmp(inputString, command) == 0) {
            digitalWrite(slotPins[i], HIGH);
            slotCommandProcessed = true;
            break;
          }
        }

        // stop slot
        for (int i = 0; i < slotCount; i++) {
          char command[15];
          sprintf(command, "stopslot%d", i + 1);
          if (strcmp(inputString, command) == 0) {
            digitalWrite(slotPins[i], LOW);
            slotCommandProcessed = true;
            break;
          }
        }

        // JSON actions
        if (!slotCommandProcessed) {
          execute_actions(inputString);
        }
      }

      inputLength = 0;   // reset input buffer
      stringComplete = false;
    }
  }

  if (flowPulseOccurred) flowPulseOccurred = false;
  nonBlockingPrint(NULL);
  checkSerialBuffers();
}
