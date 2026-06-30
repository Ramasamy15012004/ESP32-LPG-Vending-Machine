/*
  ===========================================================================
  LPG Vending Machine Controller — Demo / Portfolio Firmware
  ===========================================================================
  This is a SANITIZED, DEMONSTRATIVE version of firmware I developed for an
  industrial LPG cylinder vending machine, rewritten for portfolio purposes.

  Network addresses, the Modbus register map, and inline comments have been
  generalized/renamed. This file demonstrates the architecture and control
  techniques used in the original project (dual-core task split, Modbus TCP
  server, non-blocking stepper motor sequencing, load-cell weighing, and
  multi-sensor safety logic) — it is NOT the original client deployment.

  Shared for personal/professional reference only. Not licensed for reuse.
  ===========================================================================
*/

#include <SPI.h>
#include <EthernetENC.h>        // Ethernet library v2 required
#include <ModbusEthernet.h>
#include <DualPulseStepper.h>
#include <Wire.h>
#include <Adafruit_MCP23X17.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include "HX711.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"

// ---------------------------------------------------------------------------
// Network / Modbus configuration (placeholder values — not real deployment)
// ---------------------------------------------------------------------------
byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };   // placeholder MAC
IPAddress ip(xxx, xxx, xx, XX);                          // placeholder IP
ModbusEthernet mb;

// ---------------------------------------------------------------------------
// Stepper motor configuration — Bottom Door & Top Door actuators
// ---------------------------------------------------------------------------
DualPulseStepper bottomDoorMotor(32, 33);
DualPulseStepper topDoorMotor(26, 27);
hw_timer_t *motorTimer = NULL;

// ---------------------------------------------------------------------------
// I/O Expander pin map (16-channel digital input, via I2C)
// ---------------------------------------------------------------------------
#define PIN_BOTTOM_DOOR_LIMIT   4
#define PIN_TOP_DOOR_LIMIT      5
#define PIN_VIBRATION_1         15
#define PIN_VIBRATION_2         14
#define PIN_VIBRATION_3         13
#define PIN_ROTATION_SENSOR     3
#define PIN_ROTATION_HOME       2
#define PIN_PIR_MOTION          7
#define PIN_GAS_LEAK            12
#define PIN_SMOKE               10
#define PIN_GRID_POWER          8
#define PIN_EMERGENCY_STOP      1

Adafruit_MCP23X17 ioExpander;
uint16_t ioState;

bool bottomDoorLimit, topDoorLimit;
bool vib1, vib2, vib3;
bool rotationSensor, rotationHome;
bool pirMotion, gasLeak, smoke, gridPower, emergencyStop;

// ---------------------------------------------------------------------------
// Carousel rotation control
// ---------------------------------------------------------------------------
#define CAROUSEL_CW_PIN   16
#define CAROUSEL_CCW_PIN  17

volatile uint32_t cwStepCount = 0;
volatile uint32_t ccwStepCount = 0;
uint32_t targetStepCount = 0;
bool carouselRotating = false;
bool rotateClockwise = true;
bool lastRotationState = true;
uint32_t lastRotationSenseMs = 0;

// ---------------------------------------------------------------------------
// Door motion state flags
// ---------------------------------------------------------------------------
volatile bool bottomDoorOpening = false, bottomDoorClosing = false, bottomDoorHoming = false;
volatile bool topDoorOpening = false, topDoorClosing = false, topDoorHoming = false;

// ---------------------------------------------------------------------------
// Sensors: temperature, weight, vibration/tamper detection
// ---------------------------------------------------------------------------
#define ONE_WIRE_BUS 4
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature tempSensors(&oneWire);
float temp1C = 0, temp2C = 0, temp3C = 0;

const int LOADCELL_DOUT_PIN = 34;
const int LOADCELL_SCK_PIN  = 2;
HX711 scale;
float cylinderWeight = 0;

uint8_t vib1Count = 0, vib2Count = 0, vib3Count = 0;
uint32_t vibResetWindowStart = 0;
bool vibResetWindowActive = false;
bool vibrationAlertActive = false;
uint32_t vibrationAlertStart = 0;

bool prevVib1 = true, prevVib2 = true, prevVib3 = true;
uint32_t nowMillis = 0, nowMicros = 0;

// ---------------------------------------------------------------------------
// Modbus Holding Register Map (demo numbering — not the original layout)
// ---------------------------------------------------------------------------
// 0  : Hooter / alarm ON-OFF command
// 1  : Carousel rotate CW (write target step count)
// 2  : Carousel rotate CCW (write target step count)
// 3  : Main power shutoff relay command
// 4  : User-area light command
// 10 : Bottom door OPEN command
// 11 : Bottom door CLOSE command
// 12 : Top door OPEN command
// 13 : Top door CLOSE command
// 14 : Bottom door HOME command
// 15 : Top door HOME command
// 20 : Gas leak sensor (read-only)
// 21 : Smoke sensor (read-only)
// 22-24 : Vibration counters (read-only)
// 25-27 : Temperature readings x100 (read-only)
// 30 : Door open/closed status flags
// 31 : Carousel position (read-only)
// 32 : Cylinder weight, grams x1000 (read-only)
// 33 : Tare command (write 1 to zero the scale)
// 40 : Free heap (KB, diagnostic)
// 41-42 : Per-core CPU load %, diagnostic

void IRAM_ATTR onMotorTimer() {
  nowMicros = micros();
  if (bottomDoorOpening || bottomDoorClosing || bottomDoorHoming) {
    bottomDoorMotor.update(nowMicros);
  }
  if (topDoorOpening || topDoorClosing || topDoorHoming) {
    topDoorMotor.update(nowMicros);
  }
}

// Door obstruction sensing — pauses motion immediately if the door's path
// sensor trips mid-travel, resumes once clear.
void IRAM_ATTR onBottomDoorObstruction() {
  if (bottomDoorMotor.isRunning()) {
    if (digitalRead(35) == HIGH) {
      bottomDoorMotor.pause();
      mb.Hreg(50, 1);
    } else {
      bottomDoorMotor.resume();
      mb.Hreg(50, 0);
    }
  }
}

void IRAM_ATTR onTopDoorObstruction() {
  if (topDoorMotor.isRunning()) {
    if (digitalRead(36) == HIGH) {
      topDoorMotor.pause();
      mb.Hreg(51, 1);
    } else {
      topDoorMotor.resume();
      mb.Hreg(51, 0);
    }
  }
}

// ---------------------------------------------------------------------------
// Core 0 task — sensor polling, vibration/tamper detection, rotation counting
// ---------------------------------------------------------------------------
void sensorPollingTask(void *parameter) {
  for (;;) {
    delay(1);
    nowMillis = millis();

    ioState = ioExpander.readGPIOAB();
    bottomDoorLimit = ioState & (1 << PIN_BOTTOM_DOOR_LIMIT);
    topDoorLimit    = ioState & (1 << PIN_TOP_DOOR_LIMIT);
    vib1            = ioState & (1 << PIN_VIBRATION_1);
    vib2            = ioState & (1 << PIN_VIBRATION_2);
    vib3            = ioState & (1 << PIN_VIBRATION_3);
    rotationSensor  = ioState & (1 << PIN_ROTATION_SENSOR);
    rotationHome    = ioState & (1 << PIN_ROTATION_HOME);
    pirMotion       = ioState & (1 << PIN_PIR_MOTION);
    gasLeak         = ioState & (1 << PIN_GAS_LEAK);
    smoke           = ioState & (1 << PIN_SMOKE);
    gridPower       = ioState & (1 << PIN_GRID_POWER);
    emergencyStop   = ioState & (1 << PIN_EMERGENCY_STOP);

    // Debounced vibration edge counting (tamper / impact detection)
    if (prevVib1 && !vib1) vib1Count++;
    if (prevVib2 && !vib2) vib2Count++;
    if (prevVib3 && !vib3) vib3Count++;
    prevVib1 = vib1; prevVib2 = vib2; prevVib3 = vib3;

    if (!vibResetWindowActive && !vibrationAlertActive &&
        (vib1Count || vib2Count || vib3Count)) {
      vibResetWindowActive = true;
      vibResetWindowStart = nowMillis;
    } else if (vibResetWindowActive && !vibrationAlertActive &&
               (nowMillis - vibResetWindowStart) > 30000) {
      vib1Count = vib2Count = vib3Count = 0;
      vibResetWindowActive = false;
    }

    if (!vibrationAlertActive &&
        (vib1Count > 5 || vib2Count > 5 || vib3Count > 5)) {
      vibrationAlertActive = true;
      vibrationAlertStart = nowMillis;
    } else if (vibrationAlertActive &&
               (nowMillis - vibrationAlertStart) > 30000) {
      vib1Count = vib2Count = vib3Count = 0;
      vibrationAlertActive = false;
      vibResetWindowActive = false;
    }

    // Carousel rotation step counting (closed-loop position feedback)
    if (!lastRotationState && rotationSensor && carouselRotating) {
      lastRotationState = rotationSensor;
      lastRotationSenseMs = nowMillis;
      if (rotateClockwise) {
        cwStepCount++;
        mb.Hreg(31, cwStepCount);
        if (cwStepCount >= targetStepCount) stopCarousel();
      } else {
        ccwStepCount++;
        mb.Hreg(31, ccwStepCount);
        if (ccwStepCount >= targetStepCount) stopCarousel();
      }
    }
    if (lastRotationState && carouselRotating &&
        (nowMillis - lastRotationSenseMs) > 1500) {
      lastRotationState = rotationSensor;
    }
  }
}

void startCarousel(bool clockwise, uint32_t steps) {
  if (steps == 0) return;
  rotateClockwise = clockwise;
  carouselRotating = true;
  if (clockwise) cwStepCount = 0; else ccwStepCount = 0;
  targetStepCount = steps;
  digitalWrite(CAROUSEL_CW_PIN, clockwise ? HIGH : LOW);
  digitalWrite(CAROUSEL_CCW_PIN, clockwise ? LOW : HIGH);
  lastRotationSenseMs = nowMillis;
}

void stopCarousel() {
  if (!carouselRotating) return;
  digitalWrite(CAROUSEL_CW_PIN, LOW);
  digitalWrite(CAROUSEL_CCW_PIN, LOW);
  carouselRotating = false;
  mb.Hreg(1, 0);
  mb.Hreg(2, 0);
}

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);

  pinMode(35, INPUT_PULLUP);   // bottom door obstruction sensor
  pinMode(36, INPUT_PULLUP);   // top door obstruction sensor
  pinMode(25, OUTPUT);         // hooter/alarm
  pinMode(16, OUTPUT);         // carousel CW
  pinMode(17, OUTPUT);         // carousel CCW
  pinMode(12, OUTPUT);         // main power relay
  pinMode(13, OUTPUT);         // user-area light

  Ethernet.init(5);
  Ethernet.begin(mac, ip);
  delay(1000);
  mb.server();
  for (int i = 0; i <= 60; i++) mb.addReg(HREG(i));

  bottomDoorMotor.begin();
  topDoorMotor.begin();

  tempSensors.begin();

  scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);
  scale.set_scale(16566.56f);   // calibration factor for the load cell used
  scale.tare();

  motorTimer = timerBegin(1000000);
  timerAttachInterrupt(motorTimer, &onMotorTimer);
  timerAlarm(motorTimer, 500, true, 0);  // 500us tick — drives stepper pulse generation

  Wire.begin(21, 22);
  Wire.setClock(100000);
  if (!ioExpander.begin_I2C(0x20)) {
    Serial.println("I/O expander init failed — restarting");
    delay(500);
    esp_restart();
  }
  for (int p = 0; p <= 15; p++) ioExpander.pinMode(p, INPUT_PULLUP);

  xTaskCreatePinnedToCore(sensorPollingTask, "SensorPolling", 8192, NULL, 1, NULL, 0);

  attachInterrupt(35, onBottomDoorObstruction, CHANGE);
  attachInterrupt(36, onTopDoorObstruction, CHANGE);

  Serial.println("System initialized");
}

// ---------------------------------------------------------------------------
// Main loop (Core 1) — sensing, Modbus sync, actuator control
// ---------------------------------------------------------------------------
void loop() {
  nowMillis = millis();

  // Temperature + weight sampling only when no motion is in progress,
  // to avoid mechanical noise affecting the load cell reading.
  bool anyMotion = carouselRotating || bottomDoorOpening || bottomDoorClosing ||
                    bottomDoorHoming || topDoorOpening || topDoorClosing || topDoorHoming;
  if (!anyMotion) {
    tempSensors.requestTemperatures();
    float t;
    if ((t = tempSensors.getTempCByIndex(0)) >= 15.0) temp1C = t;
    if ((t = tempSensors.getTempCByIndex(1)) >= 15.0) temp2C = t;
    if ((t = tempSensors.getTempCByIndex(2)) >= 15.0) temp3C = t;
    cylinderWeight = scale.get_units(10);
  }

  // Push sensor state to Modbus registers
  mb.Hreg(20, !gasLeak);
  mb.Hreg(21, smoke);
  mb.Hreg(22, vib1Count);
  mb.Hreg(23, vib2Count);
  mb.Hreg(24, vib3Count);
  mb.Hreg(25, (int)(temp1C * 100));
  mb.Hreg(26, (int)(temp2C * 100));
  mb.Hreg(27, (int)(temp3C * 100));
  mb.Hreg(32, abs((int)(cylinderWeight * 1000)));

  if (mb.Hreg(33)) {           // tare command from HMI
    scale.tare();
    delay(500);
    mb.Hreg(33, 0);
  }

  // Hooter / alarm — manual command OR automatic vibration alert
  digitalWrite(25, (mb.Hreg(0) == 1 || vibrationAlertActive) ? HIGH : LOW);

  // Carousel rotate commands
  if (mb.Hreg(1) != 0 && !carouselRotating) {
    startCarousel(true, mb.Hreg(1));
  }
  if (mb.Hreg(2) != 0 && !carouselRotating) {
    startCarousel(false, mb.Hreg(2));
  }

  // Main power shutoff relay
  digitalWrite(12, (mb.Hreg(3) == 1) ? HIGH : LOW);

  // User-area light — manual command OR PIR motion trigger
  digitalWrite(13, (mb.Hreg(4) == 1 || pirMotion) ? HIGH : LOW);

  // Bottom door open/close/home commands
  if (!bottomDoorLimit && mb.Hreg(10) == 1 && !bottomDoorOpening) {
    bottomDoorMotor.move(13000, 450, 1500, 1000);
    bottomDoorOpening = true;
  } else if (mb.Hreg(11) == 1 && !bottomDoorClosing) {
    bottomDoorMotor.move(-13000, 450, 1500, 1000);
    bottomDoorClosing = true;
  } else if (mb.Hreg(14) == 1 && !bottomDoorHoming) {
    bottomDoorHoming = true;
    if (!bottomDoorLimit) {
      bottomDoorMotor.move(800, 1000, 2000, 10);
      while (bottomDoorMotor.isRunning()) delay(1);
    }
    bottomDoorMotor.move(-14000, 4000, 4000, 100);
  }

  // Top door open/close/home commands (mirrors bottom door logic)
  if (!topDoorLimit && mb.Hreg(12) == 1 && !topDoorOpening) {
    topDoorMotor.move(13000, 450, 1500, 1000);
    topDoorOpening = true;
  } else if (mb.Hreg(13) == 1 && !topDoorClosing) {
    topDoorMotor.move(-13000, 450, 1500, 1000);
    topDoorClosing = true;
  } else if (mb.Hreg(15) == 1 && !topDoorHoming) {
    topDoorHoming = true;
    if (!topDoorLimit) {
      topDoorMotor.move(800, 1000, 2000, 10);
      while (topDoorMotor.isRunning()) delay(1);
    }
    topDoorMotor.move(-14000, 4000, 4000, 100);
  }

  // Clear door-in-progress flags and report status once motion completes
  if (bottomDoorOpening || bottomDoorClosing || bottomDoorHoming) {
    if (!bottomDoorMotor.isRunning()) {
      mb.Hreg(30, bottomDoorOpening ? 1 : 0);
      bottomDoorOpening = bottomDoorClosing = bottomDoorHoming = false;
      mb.Hreg(10, 0); mb.Hreg(11, 0); mb.Hreg(14, 0);
    }
  }
  if (topDoorOpening || topDoorClosing || topDoorHoming) {
    if (!topDoorMotor.isRunning()) {
      mb.Hreg(30, topDoorOpening ? 1 : 0);
      topDoorOpening = topDoorClosing = topDoorHoming = false;
      mb.Hreg(12, 0); mb.Hreg(13, 0); mb.Hreg(15, 0);
    }
  }

  // Door obstruction safety check (secondary, polled in addition to ISR)
  if (!digitalRead(35) && !bottomDoorLimit && (bottomDoorClosing || bottomDoorHoming)) {
    bottomDoorMotor.stop();
  }
  if (!digitalRead(36) && !topDoorLimit && (topDoorClosing || topDoorHoming)) {
    topDoorMotor.stop();
  }

  mb.task();
  reportDiagnostics();
  delay(200);
}

// ---------------------------------------------------------------------------
// Diagnostics — free heap and per-core CPU load, exposed via Modbus
// ---------------------------------------------------------------------------
void reportDiagnostics() {
  mb.Hreg(40, esp_get_free_heap_size() / 1024);
  // Per-core load % calculation omitted in this demo version for brevity —
  // original implementation samples FreeRTOS task runtime stats via
  // uxTaskGetSystemState() and reports Core 0 / Core 1 utilization here.
}
