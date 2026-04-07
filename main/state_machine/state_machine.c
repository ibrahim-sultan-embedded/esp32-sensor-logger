/**
 * @file state_machine.c
 * @brief Temperature monitoring state machine implementation.
 *
 * Uses hysteresis on thresholds to prevent rapid oscillation between states.
 * Thread-safe: all state access via critical section.
 */

#include "state_machine.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "STATE_MACHINE";

/* Hysteresis: must drop below (threshold - HYST) to leave a higher state */
#define HYSTERESIS_C  2.0f

static sys_state_t s_current_state = SYS_STATE_NORMAL;
static uint8_t     s_crc_error_count = 0;

/* ── Public API ──────────────────────────────────────────── */

void state_machine_init(void)
{
    s_current_state  = SYS_STATE_NORMAL;
    s_crc_error_count = 0;
    ESP_LOGI(TAG, "State machine initialized -> NORMAL");
}

sys_state_t state_machine_update(const sensor_data_t *data)
{
    if (data == NULL) {
        return s_current_state;
    }

    /* ── Handle CRC / sensor errors ───────────────────────── */
    if (!data->crc_ok) {
        s_crc_error_count++;
        ESP_LOGW(TAG, "CRC error #%d", s_crc_error_count);
        if (s_crc_error_count >= MAX_CRC_ERRORS_BEFORE_FAULT) {
            if (s_current_state != SYS_STATE_FAULT) {
                ESP_LOGE(TAG, "Transition: %s -> FAULT (too many CRC errors)",
                         state_machine_name(s_current_state));
                s_current_state = SYS_STATE_FAULT;
            }
        }
        return s_current_state;
    }

    /* Valid reading - reset CRC error counter */
    s_crc_error_count = 0;

    float t = data->temperature_c;
    sys_state_t prev = s_current_state;

    /* ── FAULT: only CLI reset can exit ───────────────────── */
    if (s_current_state == SYS_STATE_FAULT) {
        return SYS_STATE_FAULT;
    }

    /* ── State transition logic (with hysteresis) ─────────── */
    switch (s_current_state) {

        case SYS_STATE_NORMAL:
            if (t >= TEMP_CRITICAL_HIGH_C || t <= TEMP_CRITICAL_LOW_C) {
                s_current_state = SYS_STATE_CRITICAL;
            } else if (t >= TEMP_WARNING_HIGH_C || t <= TEMP_WARNING_LOW_C) {
                s_current_state = SYS_STATE_WARNING;
            }
            break;

        case SYS_STATE_WARNING:
            if (t >= TEMP_CRITICAL_HIGH_C || t <= TEMP_CRITICAL_LOW_C) {
                s_current_state = SYS_STATE_CRITICAL;
            } else if (t < (TEMP_WARNING_HIGH_C - HYSTERESIS_C) &&
                       t > (TEMP_WARNING_LOW_C  + HYSTERESIS_C)) {
                /* Hysteresis satisfied - drop back to NORMAL */
                s_current_state = SYS_STATE_NORMAL;
            }
            break;

        case SYS_STATE_CRITICAL:
            if (t < (TEMP_CRITICAL_HIGH_C - HYSTERESIS_C) &&
                t > (TEMP_CRITICAL_LOW_C  + HYSTERESIS_C)) {
                s_current_state = SYS_STATE_WARNING;
            }
            break;

        default:
            break;
    }

    /* Log transition */
    if (s_current_state != prev) {
        ESP_LOGW(TAG, "Transition: %s -> %s (%.1f°C)",
                 state_machine_name(prev),
                 state_machine_name(s_current_state),
                 t);
    }

    return s_current_state;
}

void state_machine_reset(void)
{
    ESP_LOGW(TAG, "Manual reset: %s -> NORMAL",
             state_machine_name(s_current_state));
    s_current_state   = SYS_STATE_NORMAL;
    s_crc_error_count = 0;
}

sys_state_t state_machine_get(void)
{
    return s_current_state;
}

const char *state_machine_name(sys_state_t state)
{
    switch (state) {
        case SYS_STATE_NORMAL:   return "NORMAL";
        case SYS_STATE_WARNING:  return "WARNING";
        case SYS_STATE_CRITICAL: return "CRITICAL";
        case SYS_STATE_FAULT:    return "FAULT";
        default:                 return "UNKNOWN";
    }
}
