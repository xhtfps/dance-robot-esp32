#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool arduino_command_is_known(uint8_t byte);
bool arduino_command_from_su03_action(uint8_t su03_byte, uint8_t *arduino_byte);
bool arduino_command_from_visual_message(uint8_t message_id,
                                         uint8_t *arduino_byte);
uint32_t arduino_command_ack_timeout_ms(uint8_t byte);

#ifdef __cplusplus
}
#endif
