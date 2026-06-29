# ESP32-LPG-Vending-Machine

## Overview

This repository contains a demonstration firmware for an ESP32-based Gas Vending Machine controller. The firmware is designed for industrial automation applications where gas dispensing, machine monitoring, and remote data communication are required.

The project demonstrates embedded firmware development using Ethernet communication, Modbus TCP, sensor interfacing, real-time monitoring, SD card logging, and HTTP-based data exchange.

This repository is intended for educational and portfolio purposes. All proprietary customer information, production configurations, credentials, and company-specific implementation details have been removed or replaced with generic placeholders.

---

## Features

* ESP32-S3 Firmware
* Ethernet Communication (W5500)
* Modbus TCP Communication
* Digital and Analog I/O Monitoring
* Temperature Sensor Integration
* Load Cell Support (HX711)
* RTC Time Synchronization
* NTP Time Synchronization
* SD Card Data Logging
* HTTP Client Communication
* JSON Data Generation
* Machine Status Monitoring
* Fault Detection
* Event Logging
* Production Counter
* Runtime Monitoring
* Watchdog Handling
* FreeRTOS Task Management

---

## Hardware

* ESP32-S3
* W5500 Ethernet Module
* MCP23X17 I/O Expander
* DS18B20 Temperature Sensor
* HX711 Load Cell Module
* SD Card Module
* RTC Module
* Industrial Digital Inputs and Outputs

---

## Software Stack

* Arduino Framework
* ESP32 SDK
* Ethernet Library
* Modbus TCP Library
* ArduinoJson
* SPI
* Wire (I2C)
* FreeRTOS

---

## Project Structure

```
src/
include/
lib/
data/
README.md
```

---

## Main Functionalities

* Initializes all hardware peripherals.
* Establishes Ethernet communication.
* Hosts Modbus TCP services.
* Reads machine sensors.
* Processes vending machine status.
* Logs operational data to SD card.
* Synchronizes system time using NTP.
* Sends machine data to a remote HTTP server.
* Maintains production and runtime statistics.
* Detects machine faults and events.

---

## Configuration

Before compiling, update the following values according to your environment:

* Device IP Address
* Gateway Address
* Subnet Mask
* DNS Server
* Server URL
* Modbus Configuration
* Machine Parameters

Example:

```cpp
IPAddress ip(XXX, XXX, XXX, XXX);
```

---

## Disclaimer

This repository contains a simplified demonstration version of an industrial firmware project.

To protect confidential information:

* Customer-specific logic has been removed.
* Production server details have been replaced.
* Network configurations have been generalized.
* Proprietary algorithms are not included.
* Sensitive credentials have been removed.

This project is intended solely to demonstrate embedded systems development skills.

---

## Skills Demonstrated

* Embedded C/C++
* ESP32 Development
* Industrial Automation
* Embedded Networking
* Modbus TCP
* Embedded Ethernet
* Embedded File Systems
* Industrial Communication Protocols
* Sensor Integration
* Real-Time Firmware Development
* Industrial Data Logging
* Embedded HTTP Communication
* JSON Processing
* FreeRTOS

---

## License

This project is provided for educational and portfolio purposes only.
Do not use this firmware in production systems without proper validation and testing.
