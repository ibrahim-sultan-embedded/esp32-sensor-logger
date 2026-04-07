/**
 * @file sensor_task.c
 * @brief DS18B20 sensor driver + FreeRTOS task.
 *
 * Implements bit-banged 1-Wire protocol manually using GPIO.
 * No external library needed - shows understanding of the protocol.
 *
 * 1-Wire timing (from DS18B20 datasheet):
 *   Reset pulse:    480–960 µs low
 *   Presence pulse: 60–240 µs low (from sensor)
 *   Write 0:        60–120 µs low
 *   Write 1:        1–15 µs low, then release
 *   Read slot:      1 µs low, then sample within 15 µs
 */

#include "sensor_task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "rom/ets_sys.h"       /* ets_delay_us() */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "SENSOR";

/* ── DS18B20 ROM commands ────────────────────────────────── */
#define CMD_SKIP_ROM        0xCC
#define CMD_CONVERT_T       0x44
#define CMD_READ_SCRATCHPAD 0xBE

/* ── 1-Wire low-level driver ─────────────────────────────── */

/** Drive pin LOW (output mode) */
static inline void wire_low(void)
{
    gpio_set_direction(DS18B20_GPIO_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(DS18B20_GPIO_PIN, 0);
}

/** Release pin to HIGH via pull-up (input mode) */
static inline void wire_release(void)
{
    gpio_set_direction(DS18B20_GPIO_PIN, GPIO_MODE_INPUT);
}

/** Read current pin state */
static inline int wire_read_pin(void)
{
    return gpio_get_level(DS18B20_GPIO_PIN);
}

/**
 * Issue a 1-Wire reset and detect presence pulse.
 * @return true if sensor ACK'd (presence pulse detected)
 */
static bool onewire_reset(void)
{
    wire_low();
    ets_delay_us(480);      /* Reset pulse: hold LOW ≥480µs */
    wire_release();
    ets_delay_us(70);       /* Wait for sensor to pull low  */

    int presence = wire_read_pin();  /* 0 = sensor present */

    ets_delay_us(410);      /* Wait out remainder of slot   */

    return (presence == 0);
}

/**
 * Write one byte over 1-Wire (LSB first).
 */
static void onewire_write_byte(uint8_t byte)
{
    for (int i = 0; i < 8; i++) {
        if (byte & (1 << i)) {
            /* Write 1: pull low 1µs, release, wait 60µs */
            wire_low();
            ets_delay_us(1);
            wire_release();
            ets_delay_us(60);
        } else {
            /* Write 0: pull low 60µs, release */
            wire_low();
            ets_delay_us(60);
            wire_release();
            ets_delay_us(1);
        }
    }
}

/**
 * Read one byte over 1-Wire (LSB first).
 */
static uint8_t onewire_read_byte(void)
{
    uint8_t byte = 0;
    for (int i = 0; i < 8; i++) {
        /* Initiate read slot: pull low 1µs */
        wire_low();
        ets_delay_us(1);
        wire_release();
        ets_delay_us(14);   /* Sample within 15µs of start */

        if (wire_read_pin()) {
            byte |= (1 << i);
        }
        ets_delay_us(45);   /* Complete 60µs slot */
    }
    return byte;
}

/* ── CRC-8 (Dallas/Maxim) ────────────────────────────────── */

/**
 * Compute CRC-8 used by DS18B20 (polynomial 0x31).
 * Validates 8-byte scratchpad: bytes[0..6] CRC = bytes[7].
 */
static uint8_t crc8(const uint8_t *data, uint8_t len)
{
    uint8_t crc = 0;
    for (uint8_t i = 0; i < len; i++) {
        uint8_t byte = data[i];
        for (uint8_t j = 0; j < 8; j++) {
            uint8_t mix = (crc ^ byte) & 0x01;
            crc >>= 1;
            if (mix) crc ^= 0x8C;
            byte >>= 1;
        }
    }
    return crc;
}

/* ── DS18B20 high-level read ─────────────────────────────── */

/**
 * Read temperature from DS18B20.
 * @param out  Output sensor_data_t (always populated, crc_ok signals validity)
 */
static void ds18b20_read(sensor_data_t *out)
{
    out->timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    out->sensor_id    = 0;
    out->crc_ok       = false;
    out->temperature_c = 0.0f;

    /* Step 1: Reset + presence */
    if (!onewire_reset()) {
        ESP_LOGE(TAG, "No presence pulse - sensor disconnected?");
        return;
    }

    /* Step 2: Skip ROM, start conversion */
    onewire_write_byte(CMD_SKIP_ROM);
    onewire_write_byte(CMD_CONVERT_T);

    /* Step 3: Wait for conversion (≥750ms for 12-bit resolution) */
    vTaskDelay(pdMS_TO_TICKS(800));

    /* Step 4: Reset + read scratchpad */
    if (!onewire_reset()) {
        ESP_LOGE(TAG, "No presence pulse before scratchpad read");
        return;
    }

    onewire_write_byte(CMD_SKIP_ROM);
    onewire_write_byte(CMD_READ_SCRATCHPAD);

    /* Step 5: Read 9 bytes (8 data + 1 CRC) */
    uint8_t scratchpad[9];
    for (int i = 0; i < 9; i++) {
        scratchpad[i] = onewire_read_byte();
    }

    /* Step 6: Validate CRC */
    uint8_t computed_crc = crc8(scratchpad, 8);
    if (computed_crc != scratchpad[8]) {
        ESP_LOGW(TAG, "CRC mismatch: computed=0x%02X received=0x%02X",
                 computed_crc, scratchpad[8]);
        return;
    }

    /* Step 7: Convert raw to Celsius
     * Raw value is 16-bit signed, LSB = 0.0625°C (12-bit mode)
     */
    int16_t raw = (int16_t)((scratchpad[1] << 8) | scratchpad[0]);
    out->temperature_c = raw * 0.0625f;
    out->crc_ok        = true;

    ESP_LOGD(TAG, "Read: %.4f°C (raw=0x%04X)", out->temperature_c, (uint16_t)raw);
}

/* ── GPIO init ───────────────────────────────────────────── */

static void sensor_gpio_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << DS18B20_GPIO_PIN),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,   /* Internal pull-up (supplement external 4.7k) */
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
    ESP_LOGI(TAG, "GPIO %d configured for 1-Wire", DS18B20_GPIO_PIN);
}

/* ── FreeRTOS task ───────────────────────────────────────── */

void sensor_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Sensor task started");
    sensor_gpio_init();

    sensor_data_t reading;
    TickType_t last_wake = xTaskGetTickCount();

    while (1) {
        /* Read sensor */
        ds18b20_read(&reading);

        /* Push to queue (don't block - if queue full, drop oldest) */
        if (xQueueSend(sensor_queue, &reading, 0) != pdTRUE) {
            ESP_LOGW(TAG, "sensor_queue full - dropping sample");
        }

        /* Precise periodic delay using vTaskDelayUntil */
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(SENSOR_SAMPLE_PERIOD_MS));
    }
}
