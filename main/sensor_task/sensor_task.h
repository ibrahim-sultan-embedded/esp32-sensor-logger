/**
 * @file sensor_task.h
 * @brief DS18B20 temperature sensor task (1-Wire protocol, bit-banged).
 *
 * Reads temperature every SENSOR_SAMPLE_PERIOD_MS and pushes
 * sensor_data_t into sensor_queue.
 *
 * Wiring:
 *   DS18B20 DATA pin -> GPIO 4
 *   4.7kΩ pull-up resistor between DATA and 3.3V
 */

#ifndef SENSOR_TASK_H
#define SENSOR_TASK_H

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "common_types.h"

/* GPIO pin for DS18B20 data line */
#define DS18B20_GPIO_PIN   4

/* Extern references to shared FreeRTOS queues (defined in main.c) */
extern QueueHandle_t sensor_queue;

/**
 * FreeRTOS task entry point.
 * @param pvParameters unused
 */
void sensor_task(void *pvParameters);

#endif /* SENSOR_TASK_H */
