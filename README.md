# LPG Smart Vending Machine – ESP32 Firmware

Embedded firmware for an automated LPG (cooking gas) cylinder vending machine, built on the **ESP32 Dev Module**. The system handles mechanical dispensing (rotating cylinder carousel, automated doors), cylinder weight verification, multi-sensor safety monitoring, and exposes full machine state and control over **Modbus TCP** to an external HMI/SCADA system.

> Note: This firmware handles the machine's mechanical control, sensing, and safety logic. Payment/dispensing-trigger logic (coin, card, or app-based activation) is handled by a separate front-end system and is outside the scope of this repository.

---

## Purpose of This Repository

This repository is shared **solely to document and demonstrate my personal hands-on experience** working on this project as part of my professional work. It is **not intended**:
- as a tutorial, reference implementation, or learning resource for others,
- for reuse, copying, or adaptation in anyone else's project, commercial or personal,
- to represent a complete or production-ready product on its own.

If you are reviewing this as a recruiter, hiring manager, or collaborator, please treat it as evidence of my individual contribution and technical experience — not as open-source or reusable code. No license is granted for reuse, redistribution, or derivative work.

---

## Overview

The machine stores LPG cylinders on a motorized rotating carousel ("shaft"). When a dispense is triggered externally (via Modbus command from the HMI), the firmware:

1. Rotates the carousel to position the correct cylinder slot (CW/CCW, with closed-loop position feedback via a rotation sensor).
2. Opens the **bottom door** to release the cylinder, and the **top door** for loading/restocking — both driven by stepper motors with limit-switch homing and obstruction-pause sensing.
3. Weighs the dispensed cylinder using an **HX711 load cell** to verify correct gas content.
4. Continuously monitors safety sensors (gas leak, smoke, vibration/tamper, motion, grid power, emergency stop) and triggers a hooter (alarm) and/or power shutdown if needed.

All machine state and controls are mapped to Modbus holding registers, allowing a remote HMI or SCADA system to monitor sensors and issue commands in real time.

---

## Hardware Components

| Component | Purpose |
|---|---|
| ESP32 Dev Module | Main controller (dual-core, FreeRTOS) |
| ENC28J60 Ethernet Module | Wired network connection for Modbus TCP |
| 2x Stepper Motors (pulse/direction) | Bottom door (BD) and top door (TD) actuation |
| MCP23X17 I/O Expander (I2C) | 16-channel digital input expansion for sensors/limit switches |
| HX711 + Load Cell | Cylinder weight measurement |
| DS18B20 (x3, OneWire) | Temperature monitoring at multiple points |
| Vibration Sensors (x3) | Tamper/impact detection |
| PIR Sensor | User presence detection (auto-lighting) |
| LPG Gas Sensor | Leak detection |
| Smoke Sensor | Fire/smoke detection |
| Rotation Encoder/Sensor | Carousel position feedback |
| Limit Switches | Door open/closed homing reference |
| Relay Outputs | Hooter/alarm, main power shutoff, user-area lighting |

---

## Architecture

**Dual-core task split (FreeRTOS):**
- **Core 0** – Continuously polls the MCP23X17 I/O expander (16 sensor/limit-switch inputs), runs vibration-tamper detection logic with debounce and timeout-based reset, and tracks carousel rotation count via interrupt-driven pulse counting.
- **Core 1 (main loop)** – Handles temperature sampling (DS18B20), load cell weighing (HX711), Modbus register updates, door motor sequencing, and safety output control (hooter, power relay, lighting).

**Hardware timer (500 µs ISR):** Drives precise stepper motor pulse generation for both doors independently of the main loop, ensuring smooth, non-blocking motion control.

**Modbus TCP Server:** The ESP32 runs as a Modbus TCP slave (port 502) over Ethernet, exposing ~45 holding registers for:
- Sensor readings (gas leak, smoke, vibration counts, temperatures, weight, limit switch states, PIR, grid power, emergency stop)
- Actuator commands (door open/close/home, carousel rotate CW/CCW with step count, hooter on/off, power shutoff, lighting)
- Diagnostics (free heap memory, per-core CPU load)

**Safety interlocks:**
- Obstruction sensing on both doors — pauses stepper motion immediately via interrupt if the door path is blocked, resuming once clear.
- Vibration/tamper detection with debounced counting and auto-reset timers to avoid false triggers from normal mechanical operation.
- Automatic restart on I2C peripheral initialization failure.
- Millis()-rollover protection on all time-based comparisons for long-term run stability.

---

## Key Libraries Used

- `EthernetENC` – Ethernet (ENC28J60) communication
- `ModbusEthernet` – Modbus TCP server implementation
- `DualPulseStepper` – Non-blocking stepper motor control with trapezoidal acceleration
- `Adafruit_MCP23X17` – I2C GPIO expansion
- `OneWire` / `DallasTemperature` – DS18B20 temperature sensing
- `HX711` – Load cell amplifier interface
- FreeRTOS (`freertos/task.h`) – Dual-core task scheduling

---

## My Role

As the firmware developer on this project, I was responsible for:
- Designing and implementing the complete ESP32 firmware: dual-core task architecture, stepper motor sequencing, sensor integration, and safety logic.
- Implementing the Modbus TCP register map and server, enabling integration with an external HMI for remote monitoring and control.
- Hardware integration and bring-up: wiring, testing, and calibrating the load cell, temperature sensors, vibration sensors, and I/O expander.
- Debugging and iterating on real hardware to resolve timing issues, motor obstruction handling, and sensor noise/debounce behavior.

---

## Status

Functional firmware deployed and tested on physical hardware as part of an industrial automation product (Sway Automation & Technologies). Payment/transaction handling is managed by a separate system and integrates with this firmware via the Modbus interface.

This repository exists as a record of my individual work and experience on this project, for portfolio and professional reference purposes only.
