#pragma once

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*serial_bridge_su03_byte_cb)(uint8_t byte);
typedef void (*serial_bridge_su03_message_cb)(uint8_t message_id);
typedef void (*serial_bridge_arduino_byte_cb)(uint8_t byte);

typedef struct {
    serial_bridge_su03_byte_cb on_su03_byte;
    serial_bridge_su03_message_cb on_su03_message;
    serial_bridge_arduino_byte_cb on_arduino_byte;
} serial_bridge_callbacks_t;

esp_err_t serial_bridge_init(const serial_bridge_callbacks_t *callbacks);
esp_err_t serial_bridge_send_to_arduino(uint8_t byte);
esp_err_t serial_bridge_send_to_su03_message(uint8_t message_id);

#ifdef __cplusplus
}
#endif
