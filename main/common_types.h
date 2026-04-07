/**
 * @file common_types.h
 * @brief Shared data structures passed between FreeRTOS tasks via queues.
 */

#ifndef COMMON_TYPES_H
#define COMMON_TYPES_H

#include <stdint.h>
#include <stdbool.h>

/* ── Sensor data ─────────────────────────────────────────── */

/** Raw reading from DS18B20 sensor, produced by sensor_task */
typedef struct {
    float    temperature_c;   /**< Temperature in Celsius          */
    uint32_t timestamp_ms;    /**< esp_timer tick at time of read  */
    bool     crc_ok;          /**< true = valid 1-Wire CRC         */
    uint8_t  sensor_id;       /**< For future multi-sensor support */
} sensor_data_t;

/* ── Alert / event ───────────────────────────────────────── */

/** Severity levels - matches state machine states */
typedef enum {
    ALERT_NONE     = 0,
    ALERT_WARNING  = 1,
    ALERT_CRITICAL = 2,
    ALERT_SENSOR_ERROR = 3,
} alert_level_t;

/** Event produced by processing_task, consumed by uart_task */
typedef struct {
    alert_level_t level;
    float         temperature_c;
    uint32_t      timestamp_ms;
    char          message[64];
} alert_event_t;

/* ── Thresholds ──────────────────────────────────────────── */

#define TEMP_WARNING_HIGH_C   35.0f
#define TEMP_CRITICAL_HIGH_C  45.0f
#define TEMP_WARNING_LOW_C    5.0f
#define TEMP_CRITICAL_LOW_C   0.0f

#define SENSOR_SAMPLE_PERIOD_MS   1000   /**< DS18B20 sample every 1s  */
#define MAX_CRC_ERRORS_BEFORE_FAULT 3    /**< Consecutive errors = fault */

#endif /* COMMON_TYPES_H */
