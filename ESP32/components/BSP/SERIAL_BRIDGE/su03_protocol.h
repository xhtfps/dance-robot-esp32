#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t state;
    uint8_t message_id;
} su03_parser_t;

void su03_parser_init(su03_parser_t *parser);
bool su03_parser_push(su03_parser_t *parser, uint8_t byte,
                      uint8_t *message_id_out);
size_t su03_build_message_frame(uint8_t message_id, uint8_t out[5]);

#ifdef __cplusplus
}
#endif
