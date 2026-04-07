/**
 * @file processing_task.c
 * @brief Data processing, rolling average, and alert generation.
 *
 * Consumes sensor_data_t from sensor_queue.
 * Updates the state machine.
 * Pushes alert_event_t to alert_queue on state changes or periodically.
 */

#include "processing_task.h"
#include "state_machine.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "PROCESSING";

/* ── Rolling average buffer ──────────────────────────────── */

typedef struct {
    float    buf[ROLLING_AVG_WINDOW];
    uint8_t  idx;
    uint8_t  count;
} rolling_avg_t;

static void rolling_avg_init(rolling_avg_t *ra)
{
    memset(ra, 0, sizeof(*ra));
}

static void rolling_avg_push(rolling_avg_t *ra, float val)
{
    ra->buf[ra->idx] = val;
    ra->idx = (ra->idx + 1) % ROLLING_AVG_WINDOW;
    if (ra->count < ROLLING_AVG_WINDOW) ra->count++;
}

static float rolling_avg_get(const rolling_avg_t *ra)
{
    if (ra->count == 0) return 0.0f;
    float sum = 0.0f;
    for (uint8_t i = 0; i < ra->count; i++) sum += ra->buf[i];
    return sum / ra->count;
}

/* ── Alert helpers ───────────────────────────────────────── */

static void send_alert(alert_level_t level, float temp,
                       uint32_t ts, const char *msg)
{
    alert_event_t ev = {
        .level         = level,
        .temperature_c = temp,
        .timestamp_ms  = ts,
    };
    snprintf(ev.message, sizeof(ev.message), "%s", msg);

    /* Non-blocking: if alert_queue is full, log and continue */
    if (xQueueSend(alert_queue, &ev, 0) != pdTRUE) {
        ESP_LOGW(TAG, "alert_queue full - alert dropped: %s", msg);
    }
}

/* ── FreeRTOS task ───────────────────────────────────────── */

void processing_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Processing task started");

    rolling_avg_t avg;
    rolling_avg_init(&avg);

    sys_state_t prev_state = SYS_STATE_NORMAL;
    uint32_t    sample_count = 0;

    sensor_data_t data;

    while (1) {
        /* Block until a sensor reading arrives */
        if (xQueueReceive(sensor_queue, &data, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        sample_count++;

        /* Update state machine */
        sys_state_t new_state = state_machine_update(&data);

        if (!data.crc_ok) {
            char msg[64];
            snprintf(msg, sizeof(msg), "Sensor error (CRC fail) at t=%lums",
                     (unsigned long)data.timestamp_ms);
            send_alert(ALERT_SENSOR_ERROR, 0.0f, data.timestamp_ms, msg);
            continue;
        }

        /* Update rolling average with valid reading */
        rolling_avg_push(&avg, data.temperature_c);
        float avg_temp = rolling_avg_get(&avg);

        /* Log every sample at DEBUG level */
        ESP_LOGD(TAG, "[#%lu] %.2f°C  avg=%.2f°C  state=%s",
                 (unsigned long)sample_count,
                 data.temperature_c, avg_temp,
                 state_machine_name(new_state));

        /* Send alert on state transition */
        if (new_state != prev_state) {
            char msg[64];
            snprintf(msg, sizeof(msg),
                     "State: %s->%s  T=%.1f°C  avg=%.1f°C",
                     state_machine_name(prev_state),
                     state_machine_name(new_state),
                     data.temperature_c, avg_temp);

            alert_level_t lv = (alert_level_t)new_state; /* enum values match */
            send_alert(lv, data.temperature_c, data.timestamp_ms, msg);
            prev_state = new_state;
        }

        /* Periodic status report every 10 samples regardless of state */
        if (sample_count % 10 == 0) {
            char msg[64];
            snprintf(msg, sizeof(msg),
                     "Status: T=%.1f°C  avg=%.1f°C  n=%lu",
                     data.temperature_c, avg_temp,
                     (unsigned long)sample_count);
            send_alert(ALERT_NONE, data.temperature_c,
                       data.timestamp_ms, msg);
        }
    }
}
