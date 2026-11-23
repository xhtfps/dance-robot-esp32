#include "arduino_command_map.h"

#include <stddef.h>

static const uint8_t s_commands[] = {
    0x31U, 0x33U, 0x32U, 0x34U, 0x39U, 0x61U, 0x62U,
    0x63U, 0x42U, 0x43U, 0x66U, 0x64U, 0x65U, 0x41U,
    0x35U, 0x36U, 0x38U, 0x44U, 0x45U,
};

bool arduino_command_is_known(uint8_t byte)
{
    if (byte == 0x46U) {
        return true;
    }
    for (size_t i = 0; i < sizeof(s_commands); ++i) {
        if (s_commands[i] == byte) {
            return true;
        }
    }
    return false;
}

bool arduino_command_from_su03_action(uint8_t su03_byte, uint8_t *arduino_byte)
{
    if (arduino_byte == NULL) {
        return false;
    }
    if (su03_byte >= 0x10U && su03_byte <= 0x13U) {
        su03_byte = (uint8_t)(su03_byte - 0x10U + 10U);
    }
    if (su03_byte < 1U || su03_byte > 13U) {
        return false;
    }
    *arduino_byte = s_commands[su03_byte - 1U];
    return true;
}

bool arduino_command_from_visual_message(uint8_t message_id,
                                         uint8_t *arduino_byte)
{
    if (arduino_byte == NULL) {
        return false;
    }

    switch (message_id) {
    case 1U:  // 大字站
        *arduino_byte = 0x41U; // B
        return true;
    case 2U:  // 举双手
        *arduino_byte = 0x35U; // RA
        return true;
    case 3U:  // 蹲下
        *arduino_byte = 0x36U; // RC
        return true;
    case 4U:  // 弓箭步
        *arduino_byte = 0x38U; // RD
        return true;
    case 5U:  // 叉腰
        *arduino_byte = 0x64U; // Y，与语音 12 共用
        return true;
    case 6U:  // 敬礼
        *arduino_byte = 0x65U; // A，与语音 13 共用
        return true;
    default:
        return false;
    }
}

uint32_t arduino_command_ack_timeout_ms(uint8_t byte)
{
    if (byte == 0x44U) {
        return 85000U;
    }
    if (byte == 0x45U) {
        return 115000U;
    }
    return arduino_command_is_known(byte) ? 20000U : 0U;
}
