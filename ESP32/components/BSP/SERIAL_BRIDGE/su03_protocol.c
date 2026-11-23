#include "su03_protocol.h"

#include <string.h>

enum {
    SU03_WAIT_START = 0,
    SU03_WAIT_HEADER_TAIL,
    SU03_WAIT_MESSAGE_ID,
    SU03_WAIT_FRAME_TAIL_HEAD,
    SU03_WAIT_FRAME_TAIL_END,
};

static void reset_to_header_search(su03_parser_t *parser, uint8_t byte)
{
    parser->state = byte == 0xAA ? SU03_WAIT_HEADER_TAIL : SU03_WAIT_START;
    parser->message_id = 0;
}

void su03_parser_init(su03_parser_t *parser)
{
    if (parser == NULL) {
        return;
    }
    parser->state = SU03_WAIT_START;
    parser->message_id = 0;
}

bool su03_parser_push(su03_parser_t *parser, uint8_t byte,
                      uint8_t *message_id_out)
{
    if (parser == NULL || message_id_out == NULL) {
        return false;
    }

    switch (parser->state) {
    case SU03_WAIT_START:
        if (byte == 0xAA) {
            parser->state = SU03_WAIT_HEADER_TAIL;
        }
        break;

    case SU03_WAIT_HEADER_TAIL:
        if (byte == 0x55) {
            parser->state = SU03_WAIT_MESSAGE_ID;
        } else {
            reset_to_header_search(parser, byte);
        }
        break;

    case SU03_WAIT_MESSAGE_ID:
        parser->message_id = byte;
        parser->state = SU03_WAIT_FRAME_TAIL_HEAD;
        break;

    case SU03_WAIT_FRAME_TAIL_HEAD:
        if (byte == 0x55) {
            parser->state = SU03_WAIT_FRAME_TAIL_END;
        } else {
            reset_to_header_search(parser, byte);
        }
        break;

    case SU03_WAIT_FRAME_TAIL_END:
        if (byte == 0xAA && parser->message_id >= 1U &&
            parser->message_id <= 14U) {
            *message_id_out = parser->message_id;
            su03_parser_init(parser);
            return true;
        }
        reset_to_header_search(parser, byte);
        break;

    default:
        su03_parser_init(parser);
        break;
    }
    return false;
}

size_t su03_build_message_frame(uint8_t message_id, uint8_t out[5])
{
    if (out == NULL || message_id < 1U || message_id > 14U) {
        return 0U;
    }

    const uint8_t frame[5] = {0xAA, 0x55, message_id, 0x55, 0xAA};
    memcpy(out, frame, sizeof(frame));
    return sizeof(frame);
}
