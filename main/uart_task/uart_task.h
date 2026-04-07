/**
 * @file uart_task.h
 * @brief UART CLI: prints alerts + accepts user commands.
 *
 * Commands:
 *   status   - Print current temperature and system state
 *   reset    - Reset state machine to NORMAL (clear FAULT)
 *   thresholds - Print current temperature thresholds
 *   help     - List commands
 */

#ifndef UART_TASK_H
#define UART_TASK_H

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "common_types.h"

extern QueueHandle_t  alert_queue;
extern SemaphoreHandle_t uart_mutex;

void uart_task(void *pvParameters);

#endif /* UART_TASK_H */