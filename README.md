# ESP32 Multi-Sensor Logger with Alert System

Real-time embedded system built on ESP32 + FreeRTOS, demonstrating professional firmware architecture including multi-task design, inter-task communication, state machines, and hardware sensor integration.

---

## Hardware

| Component | Details |
|-----------|---------|
| MCU | ESP32-WROOM-32 |
| Sensor | DS18B20 (1-Wire, digital temperature) |
| Interface | UART CLI (115200 baud, USB-Serial) |

**Wiring:**
```
ESP32 GPIO4 ──┬── DS18B20 DATA
              │
             4.7kΩ
              │
ESP32 3.3V ───┘
DS18B20 GND → ESP32 GND
DS18B20 VDD → ESP32 3.3V
```

---

## Software Architecture

```
┌─────────────────────────────────────────────────────┐
│                   FreeRTOS Tasks                     │
│                                                      │
│  [sensor_task]  ──sensor_queue──>  [processing_task] │
│   Priority: 3        Queue(10)      Priority: 2      │
│   Period: 1s                              │          │
│                                    alert_queue       │
│                                    Queue(5)          │
│                                           │          │
│                                    [uart_task]       │
│                                     Priority: 1      │
└─────────────────────────────────────────────────────┘

Synchronization: uart_mutex (binary semaphore) protects
                 UART0 from concurrent access.
```

### Tasks

| Task | Priority | Stack | Responsibility |
|------|----------|-------|----------------|
| `sensor_task` | 3 (highest) | 4KB | DS18B20 sampling via 1-Wire, CRC validation |
| `processing_task` | 2 | 4KB | Rolling average, state machine, alert generation |
| `uart_task` | 1 | 4KB | Alert display, CLI command handling |

### State Machine

```
          ┌──────────┐
     ┌───>│  NORMAL  │<───┐  (T < 35°C)
     │    └────┬─────┘    │
     │         │ T≥35°C   │ T<33°C (hysteresis)
     │    ┌────▼─────┐    │
     │    │ WARNING  │────┘
     │    └────┬─────┘
     │         │ T≥45°C
     │    ┌────▼─────┐
     └────│ CRITICAL │  T<43°C (hysteresis)
          └────┬─────┘
               │ 3x CRC errors
          ┌────▼─────┐
          │  FAULT   │  → CLI 'reset' to recover
          └──────────┘
```

Hysteresis (±2°C) prevents rapid oscillation between states at threshold boundaries.

---

## DS18B20 / 1-Wire Implementation

The 1-Wire driver is implemented manually (bit-banged GPIO) without external libraries, demonstrating protocol-level understanding:

- **Reset + Presence pulse** detection
- **Write bit/byte** (open-drain signaling)
- **Read bit/byte** with correct timing windows
- **CRC-8** (Dallas/Maxim polynomial 0x31) validation of scratchpad
- **12-bit resolution** → 0.0625°C LSB

---

## UART CLI

Connect at **115200 baud, 8N1** (e.g. `idf.py monitor`).

```
> help

=== ESP32 Sensor Logger CLI ===
  status     - Current temperature & state
  reset      - Reset state machine to NORMAL
  thresholds - Show temperature thresholds
  help       - This message
===============================

> status
State: WARNING

> thresholds
WARNING  high=35.0°C  low=5.0°C
CRITICAL high=45.0°C  low=0.0°C

> reset
State machine reset to NORMAL.
```

Sample output:
```
[INFO]  Status: T=27.3°C  avg=27.1°C  n=10
[WARN]  State: NORMAL->WARNING  T=35.6°C  avg=34.2°C
[CRIT]  State: WARNING->CRITICAL  T=46.1°C  avg=41.3°C
[ERROR] Sensor error (CRC fail) at t=12345ms
```

---

## Build & Flash

```bash
# Install ESP-IDF (v5.x recommended)
git clone https://github.com/espressif/esp-idf.git
cd esp-idf && ./install.sh && source export.sh

# Clone and build
git clone https://github.com/ibrahim-sultan-embedded/esp32-sensor-logger
cd esp32-sensor-logger
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

---

## Key Concepts Demonstrated

- **FreeRTOS multi-task architecture** with defined priorities
- **Inter-task communication** via `xQueueSend` / `xQueueReceive`
- **Mutex synchronization** preventing UART output corruption
- **Precise periodic sampling** using `vTaskDelayUntil`
- **Bit-banged 1-Wire protocol** with datasheet-accurate timing
- **CRC-8 validation** and fault detection
- **State machine with hysteresis** for reliable threshold detection
- **Layered code structure**: driver → task → application

---

## Author

Ibrahim Sultan — [LinkedIn](https://linkedin.com/in/IbrahimSultan25) · [GitHub](https://github.com/ibrahim-sultan-embedded)
