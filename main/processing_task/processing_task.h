/**
 * @file processing_task.h
 * @brief Consumes sensor_queue, runs state machine, produces alert_queue.
 *
 * Also maintains a rolling average of the last N readings
 * for smoothing / logging purposes.
 */

#ifndef PROCESSING_TASK_H
#define PROCESSING_TASK_H

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "common_types.h"

#define ROLLING_AVG_WINDOW  5   /**< Number of samples for rolling average */

extern QueueHandle_t sensor_queue;
extern QueueHandle_t alert_queue;

void processing_task(void *pvParameters);

#endif /* PROCESSING_TASK_H */
