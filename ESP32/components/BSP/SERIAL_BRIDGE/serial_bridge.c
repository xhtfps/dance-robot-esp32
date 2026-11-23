#include "serial_bridge.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "su03_protocol.h"

#define SU03_UART             UART_NUM_1
#define SU03_UART_RX_GPIO     GPIO_NUM_1
#define SU03_UART_TX_GPIO     GPIO_NUM_2
#define ARDUINO_UART          UART_NUM_2
#define ARDUINO_UART_RX_GPIO  GPIO_NUM_20
#define ARDUINO_UART_TX_GPIO  GPIO_NUM_21
#define SERIAL_BAUD_RATE      115200
#define SERIAL_RX_BUFFER_SIZE 4096
#define SERIAL_RX_CHUNK_SIZE  64
#define SERIAL_TASK_STACK     3072
#define SERIAL_TASK_PRIORITY  5

static const char *TAG = "SERIAL_BRIDGE";
static serial_bridge_callbacks_t s_callbacks;
static SemaphoreHandle_t s_tx_mutex;
static bool s_initialized;

static esp_err_t configure_uart(uart_port_t port, int rx_gpio, int tx_gpio)
{
    const uart_config_t config = {
        .baud_rate = SERIAL_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t err = uart_param_config(port, &config);
    if (err != ESP_OK) {
        return err;
    }
    err = uart_set_pin(port, tx_gpio, rx_gpio, UART_PIN_NO_CHANGE,
                       UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        return err;
    }
    return uart_driver_install(port, SERIAL_RX_BUFFER_SIZE, 0, 0, NULL, 0);
}

static void su03_rx_task(void *context)
{
    (void)context;
    uint8_t buffer[SERIAL_RX_CHUNK_SIZE];
    su03_parser_t parser;
    su03_parser_init(&parser);

    while (true) {
        const int count = uart_read_bytes(SU03_UART, buffer, sizeof(buffer),
                                          pdMS_TO_TICKS(100));
        for (int i = 0; i < count; ++i) {
            const uint8_t byte = buffer[i];
            if (s_callbacks.on_su03_byte != NULL) {
                s_callbacks.on_su03_byte(byte);
            }

            uint8_t message_id = 0;
            if (su03_parser_push(&parser, byte, &message_id) &&
                s_callbacks.on_su03_message != NULL) {
                s_callbacks.on_su03_message(message_id);
            }
        }
    }
}

static void arduino_rx_task(void *context)
{
    (void)context;
    uint8_t buffer[SERIAL_RX_CHUNK_SIZE];

    while (true) {
        const int count = uart_read_bytes(ARDUINO_UART, buffer, sizeof(buffer),
                                          pdMS_TO_TICKS(100));
        for (int i = 0; i < count; ++i) {
            if (s_callbacks.on_arduino_byte != NULL) {
                s_callbacks.on_arduino_byte(buffer[i]);
            }
        }
    }
}

static esp_err_t write_bytes(uart_port_t port, const uint8_t *bytes, size_t count)
{
    if (!s_initialized || bytes == NULL || count == 0U) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(s_tx_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    const int written = uart_write_bytes(port, (const char *)bytes, count);
    xSemaphoreGive(s_tx_mutex);
    if (written < 0 || (size_t)written != count) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t serial_bridge_init(const serial_bridge_callbacks_t *callbacks)
{
    if (callbacks == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    memcpy(&s_callbacks, callbacks, sizeof(s_callbacks));
    s_tx_mutex = xSemaphoreCreateMutex();
    if (s_tx_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = configure_uart(SU03_UART, SU03_UART_RX_GPIO,
                                   SU03_UART_TX_GPIO);
    if (err == ESP_OK) {
        err = configure_uart(ARDUINO_UART, ARDUINO_UART_RX_GPIO,
                             ARDUINO_UART_TX_GPIO);
    }
    if (err != ESP_OK) {
        uart_driver_delete(SU03_UART);
        uart_driver_delete(ARDUINO_UART);
        vSemaphoreDelete(s_tx_mutex);
        s_tx_mutex = NULL;
        return err;
    }

    s_initialized = true;
    if (xTaskCreate(su03_rx_task, "su03_rx", SERIAL_TASK_STACK, NULL,
                   SERIAL_TASK_PRIORITY, NULL) != pdPASS ||
        xTaskCreate(arduino_rx_task, "arduino_rx", SERIAL_TASK_STACK, NULL,
                   SERIAL_TASK_PRIORITY, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Cannot create UART RX task");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "UART1 SU-03T: RX=%d TX=%d baud=%d",
             SU03_UART_RX_GPIO, SU03_UART_TX_GPIO, SERIAL_BAUD_RATE);
    ESP_LOGI(TAG, "UART2 Arduino: RX=%d TX=%d baud=%d",
             ARDUINO_UART_RX_GPIO, ARDUINO_UART_TX_GPIO, SERIAL_BAUD_RATE);
    return ESP_OK;
}

esp_err_t serial_bridge_send_to_arduino(uint8_t byte)
{
    return write_bytes(ARDUINO_UART, &byte, 1U);
}

esp_err_t serial_bridge_send_to_su03_message(uint8_t message_id)
{
    uint8_t frame[5];
    const size_t frame_size = su03_build_message_frame(message_id, frame);
    if (frame_size == 0U) {
        return ESP_ERR_INVALID_ARG;
    }
    return write_bytes(SU03_UART, frame, frame_size);
}
