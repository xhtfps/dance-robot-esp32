ARDUINO_COMMAND_BYTES = (
    0x31, 0x33, 0x32, 0x34, 0x39,
    0x61, 0x62, 0x63, 0x42, 0x43,
    0x66, 0x64, 0x65, 0x41, 0x35,
    0x36, 0x38, 0x44, 0x45,
)
ARDUINO_RESET_COMMAND = 0x46


def is_known_arduino_command(byte):
    return byte in ARDUINO_COMMAND_BYTES or byte == ARDUINO_RESET_COMMAND


def translate_su03_action(byte):
    if 0x10 <= byte <= 0x13:
        byte = byte - 0x10 + 10
    if not 1 <= byte <= 13:
        raise ValueError("SU-03T action must be in 1..13")
    return ARDUINO_COMMAND_BYTES[byte - 1]


def translate_visual_message(message_id):
    return {
        1: 0x41,  # 大字站 -> B
        2: 0x35,  # 举双手 -> RA
        3: 0x36,  # 蹲下 -> RC
        4: 0x38,  # 弓箭步 -> RD
        5: 0x64,  # 叉腰 -> Y，与语音 12 共用
        6: 0x65,  # 敬礼 -> A，与语音 13 共用
    }.get(message_id)


def translate_music_dance(music_id):
    if 1 <= music_id <= 5:
        return 0x44  # 五首官方音乐共用 R1 舞蹈
    if music_id == 6:
        return 0x45  # 自选音乐使用 R2 舞蹈
    return None


def stage_for_command(byte):
    return {0x44: 0, 0x55: 1, 0x66: 2, 0x22: 1, 0x33: 2}.get(byte)


def build_frame(message_id):
    if not 1 <= message_id <= 14:
        raise ValueError("message_id must be in 1..14")
    return bytes((0xAA, 0x55, message_id, 0x55, 0xAA))


def parse(bytes_iterable):
    state = 0
    message_id = 0
    messages = []
    for byte in bytes_iterable:
        byte &= 0xFF
        if state == 0:
            state = 1 if byte == 0xAA else 0
        elif state == 1:
            if byte == 0x55:
                state = 2
            else:
                state = 1 if byte == 0xAA else 0
        elif state == 2:
            message_id = byte
            state = 3
        elif state == 3:
            if byte == 0x55:
                state = 4
            else:
                state = 1 if byte == 0xAA else 0
        else:
            if byte == 0xAA:
                if 1 <= message_id <= 14:
                    messages.append(message_id)
                state = 0
            else:
                state = 1 if byte == 0xAA else 0
    return messages
