# Hardware Integration Overview

This document outlines the hardware abstraction layer (HAL) architecture used to integrate power electronics and industrial devices within the Energy Management System (EMS). It provides a summary of supported hardware, communication protocols, architectural design, and configuration structure.

---

## Supported Hardware

### PV Inverters

* **SMA Sunny Boy/Tripower**
  Protocols: Modbus TCP, SunSpec
* **Fronius Symo/Prismo**
  Protocols: Modbus TCP, Solar API
* **Victron MultiPlus/Quattro**
  Interfaces: VE.Can, VE.Direct
* **Generic Inverters**
  Protocols: Modbus RTU/TCP with custom register maps

### Battery Systems

* **Victron BMV / SmartShunt**
  Interfaces: VE.Can, VE.Direct
* **Daly BMS**
  Interfaces: CAN bus, UART
* **REC BMS**
  Interface: CAN bus
* **Generic BMS**
  Protocols: Modbus RTU/TCP

### Relay / IO Modules

* **WAGO 750 Series**
  Protocols: Modbus RTU/TCP
* **Phoenix Contact**
  Protocols: Modbus RTU/TCP
* **Schneider Electric**
  Protocols: Modbus RTU/TCP
* **Generic Relays**
  Protocols: Modbus RTU/TCP

### Energy Meters

* **Janitza UMG 604/96**
  Protocols: Modbus RTU/TCP
* **Eastron SDM Series**
  Protocol: Modbus RTU
* **Schneider PM Series**
  Protocols: Modbus RTU/TCP
* **Generic Meters**
  Protocols: Modbus RTU/TCP

---

## Communication Protocols

### Modbus RTU

* RS-485 multi-drop
* 9600–115200 baud
* 8-N-1 or 8-E-1
* Up to 247 devices per network

### Modbus TCP

* Ethernet-based
* Standard port 502
* Supports multiple concurrent connections
* Compatible with industrial network switches

### CAN Bus

* CAN 2.0A/B
* 125 kbps to 1 Mbps
* CANopen profiles
* Multi-master topology

---

## Integration Architecture

```
┌─────────────────────────────────────────┐
│         Energy Management System        │
├─────────────────────────────────────────┤
│           HAL Integration Layer         │
├──────┬──────┬───────┬────────┬──────────┤
│ PV   │Batt  │Relay  │ Meter  │  CAN     │
│ HAL  │ HAL  │ HAL   │ HAL    │  Bus     │
├──────┴──────┴───────┴────────┴──────────┤
│   Modbus RTU   │   Modbus TCP   │  CAN  │
└────────────────┴────────────────┴───────┘
```

---

## Key Features

* **Modular Design**
  Each hardware class is implemented as an independent HAL module.

* **Protocol Abstraction**
  Unified API regardless of underlying communication protocol.

* **Automatic Discovery**
  Scanning and auto-configuration for devices on supported buses.

* **Error Handling**
  Centralized diagnostics, fault reporting, and recovery logic.

* **Thread Safety**
  Multi-threaded operation with proper locking and concurrency control.

* **Callback System**
  Asynchronous event notifications for state changes and errors.

* **Statistics Collection**
  Metrics for communication performance and device health.

* **Hot-Swap Support**
  Dynamic addition and removal of devices without system restart.

---

## Example Configuration

```json
{
    "hardware": {
        "pv_inverters": [
            {
                "type": "sma",
                "interface": "modbus_tcp",
                "ip_address": "192.168.1.100",
                "port": 502,
                "unit_id": 3,
                "max_power": 5000
            }
        ],
        "battery_systems": [
            {
                "type": "victron",
                "interface": "can",
                "can_interface": "can0",
                "speed": 250000,
                "node_id": 1,
                "capacity": 9600
            }
        ],
        "relay_modules": [
            {
                "type": "wago",
                "interface": "modbus_rtu",
                "port": "/dev/ttyUSB0",
                "baud_rate": 38400,
                "unit_id": 1,
                "channels": 8
            }
        ],
        "energy_meters": [
            {
                "type": "janitza",
                "interface": "modbus_rtu",
                "port": "/dev/ttyUSB1",
                "baud_rate": 19200,
                "unit_id": 2,
                "phases": 3
            }
        ]
    }
}
```

---

## Summary

This HAL framework provides a structured, extensible, and reliable foundation for integrating real-world hardware into the Energy Management System. It supports the most widely used industrial protocols and equipment in residential solar, commercial installations, and agricultural settings.
