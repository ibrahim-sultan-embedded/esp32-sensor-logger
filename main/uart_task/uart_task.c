/**
 * @file uart_task.c
 * @brief UART output and command-line interface (CLI).
 *
 * Two responsibilities:
 *   1. Consume alert_queue and print formatted messages to UART.
 *   2. Poll UART RX for user commands and execute them.
 *
 * Uses uart_mutex (binary semaphore) to prevent interleaved output
 * between alert printing and CLI responses.
 */

#include "uart_task.h"
#include "state_machine.h"
#include "esp_log.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "UART_CLI";

/* UART configuration */
#define CLI_UART_NUM      UART_NUM_0
#define CLI_UART_BAUD     115200
#define CLI_BUF_SIZE      256
#define CMD_MAX_LEN       32

/* ── UART init ───────────────────────────────────────────── */

static void uart_init(void)
{
    uart_config_t cfg = {
        .baud_rate  = CLI_UART_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
    };
    uart_driver_install(CLI_UART_NUM, CLI_BUF_SIZE * 2, 0, 0, NULL, 0);
    uart_param_config(CLI_UART_NUM, &cfg);
    ESP_LOGI(TAG, "UART0 initialized at %d baud", CLI_UART_BAUD);
}

/* ── Thread-safe print ───────────────────────────────────── */

static void cli_print(const char *str)
{
    if (xSemaphoreTake(uart_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        uart_write_bytes(CLI_UART_NUM, str, strlen(str));
        xSemaphoreGive(uart_mutex);
    }
}

/* ── Alert formatting ────────────────────────────────────── */

static const char *alert_prefix(alert_level_t level)
{
    switch (level) {
        case ALERT_NONE:         return "[INFO]  ";
        case ALERT_WARNING:      return "[WARN]  ";
        case ALERT_CRITICAL:     return "[CRIT]  ";
        case ALERT_SENSOR_ERROR: return "[ERROR] ";
        default:                 return "[?]     ";
    }
}

static void print_alert(const alert_event_t *ev)
{
    char line[128];
    snprintf(line, sizeof(line), "%s %s\r\n",
             alert_prefix(ev->level), ev->message);
    cli_print(line);
}

/* ── CLI command handler ─────────────────────────────────── */

static void handle_command(const char *cmd)
{
    char resp[256];

    if (strcmp(cmd, "help") == 0) {
        cli_print("\r\n=== ESP32 Sensor Logger CLI ===\r\n");
        cli_print("  status     - Current temperature & state\r\n");
        cli_print("  reset      - Reset state machine to NORMAL\r\n");
        cli_print("  thresholds - Show temperature thresholds\r\n");
        cli_print("  help       - This message\r\n");
        cli_print("===============================\r\n");

    } else if (strcmp(cmd, "status") == 0) {
        sys_state_t s = state_machine_get();
        snprintf(resp, sizeof(resp),
                 "State: %s\r\n", state_machine_name(s));
        cli_print(resp);

    } else if (strcmp(cmd, "reset") == 0) {
        state_machine_reset();
        cli_print("State machine reset to NORMAL.\r\n");

    } else if (strcmp(cmd, "thresholds") == 0) {
        snprintf(resp, sizeof(resp),
                 "WARNING  high=%.1f°C  low=%.1f°C\r\n"
                 "CRITICAL high=%.1f°C  low=%.1f°C\r\n",
                 TEMP_WARNING_HIGH_C,  TEMP_WARNING_LOW_C,
                 TEMP_CRITICAL_HIGH_C, TEMP_CRITICAL_LOW_C);
        cli_print(resp);

    } else if (strlen(cmd) == 0) {
        /* Empty line - ignore */

    } else {
        snprintf(resp, sizeof(resp),
                 "Unknown command: '%s'. Type 'help'.\r\n", cmd);
        cli_print(resp);
    }
}

/* ── FreeRTOS task ───────────────────────────────────────── */

void uart_task(void *pvParameters)
{
    ESP_LOGI(TAG, "UART task started");
    uart_init();
    cli_print("\r\nESP32 Sensor Logger ready. Type 'help'.\r\n> ");

    uint8_t  rx_buf[CLI_BUF_SIZE];
    char     cmd_buf[CMD_MAX_LEN];
    int      cmd_idx = 0;

    alert_event_t ev;

    while (1) {
        /* ── Check for new alerts (non-blocking) ─────────── */
        if (xQueueReceive(alert_queue, &ev, 0) == pdTRUE) {
            print_alert(&ev);
            cli_print("> ");  /* Reprint prompt after alert */
        }

        /* ── Check for incoming UART bytes ───────────────── */
        int len = uart_read_bytes(CLI_UART_NUM, rx_buf,
                                  sizeof(rx_buf) - 1,
                                  pdMS_TO_TICKS(20));
        if (len > 0) {
            for (int i = 0; i < len; i++) {
                char c = (char)rx_buf[i];

                /* Echo character back */
                uart_write_bytes(CLI_UART_NUM, &c, 1);

                if (c == '\r' || c == '\n') {
                    cli_print("\r\n");
                    cmd_buf[cmd_idx] = '\0';
                    handle_command(cmd_buf);
                    cmd_idx = 0;
                    cli_print("> ");
                } else if (c == '\b' || c == 0x7F) {
                    /* Backspace */
                    if (cmd_idx > 0) {
                        cmd_idx--;
                        cli_print("\b \b");
                    }
                } else if (cmd_idx < CMD_MAX_LEN - 1) {
                    cmd_buf[cmd_idx++] = c;
                }
            }
        }

        /* Yield to other tasks */
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
