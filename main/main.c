/**
 * @file main.c
 * @brief ESP32 Multi-Sensor Logger with Alert System
 *
 * Real-time embedded system using FreeRTOS.
 * Architecture: 3 tasks communicating via Queues + Semaphores.
 *
 * Task diagram:
 *   [sensor_task] --> sensor_queue --> [processing_task] --> alert_queue --> [uart_task]
 *
 * Author: Ibrahim Sultan
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"

#include "sensor_task.h"
#include "processing_task.h"
#include "uart_task.h"
#include "state_machine.h"

static const char *TAG = "MAIN";

/* Shared FreeRTOS objects - defined here, extern in other files */
QueueHandle_t sensor_queue   = NULL;
QueueHandle_t alert_queue    = NULL;
SemaphoreHandle_t uart_mutex = NULL;

void app_main(void)
{
    ESP_LOGI(TAG, "=== ESP32 Sensor Logger v1.0 ===");
    ESP_LOGI(TAG, "Initializing FreeRTOS objects...");

    /* Create queues */
    sensor_queue = xQueueCreate(10, sizeof(sensor_data_t));
    alert_queue  = xQueueCreate(5,  sizeof(alert_event_t));

    if (sensor_queue == NULL || alert_queue == NULL) {
        ESP_LOGE(TAG, "FATAL: Queue creation failed. Halting.");
        while (1) { vTaskDelay(pdMS_TO_TICKS(1000)); }
    }

    /* Create mutex for UART access */
    uart_mutex = xSemaphoreCreateMutex();
    if (uart_mutex == NULL) {
        ESP_LOGE(TAG, "FATAL: Mutex creation failed. Halting.");
        while (1) { vTaskDelay(pdMS_TO_TICKS(1000)); }
    }

    /* Initialize state machine */
    state_machine_init();

    /* Create tasks
     * Priority: sensor(3) > processing(2) > uart(1)
     * Sensor task has highest priority - must never miss a sample.
     */
    xTaskCreate(sensor_task,     "sensor",     4096, NULL, 3, NULL);
    xTaskCreate(processing_task, "processing", 4096, NULL, 2, NULL);
    xTaskCreate(uart_task,       "uart_cli",   4096, NULL, 1, NULL);

    ESP_LOGI(TAG, "All tasks created. System running.");
}
