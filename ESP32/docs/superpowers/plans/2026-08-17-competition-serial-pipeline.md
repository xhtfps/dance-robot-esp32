# Competition Serial, Vision, and Music Pipeline Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement the ESP32-S3 competition event pipeline with two hardware UARTs, SU-03T framed feedback, Arduino-compatible action bytes, browser pose-result relay, and INMP441 music-input plumbing.

**Architecture:** Keep UART0 for ESP-IDF logs. UART1 connects bidirectionally to SU-03T and parses/creates `AA 55 <message_id> 55 AA` feedback frames. UART2 sends one-byte Arduino commands to `mySerial2` and reserves RX for future feedback. A competition controller owns state transitions; SU-03T stage events select the active stage, the web endpoint only submits visual recognition results, and the INMP441 adapter supplies music-recognition samples.

**Tech Stack:** ESP-IDF C, FreeRTOS tasks/queues, `driver/uart.h`, existing `audio_i2s` I2S driver, `esp_http_server`, cJSON for the fixed pose JSON payload, Python `wave`/NumPy tooling for offline music-feature generation, and the existing ESP-IDF build.

## Global Constraints

- UART0 remains the console; device UARTs use hardware UART drivers, not `SoftwareSerial`.
- UART1/SU-03T and UART2/Arduino use `115200, 8N1`.
- Default candidate pins are SU-03T UART1 RX GPIO1/TX GPIO2 and Arduino UART2 RX GPIO3/TX GPIO21; preserve existing camera, LCD, audio, and BOOT GPIOs.
- SU-03T feedback messages use exactly `AA 55 <message_id> 55 AA`; message 4 must produce `AA 55 04 55 AA` with no CR/LF.
- Arduino receives one raw action-command byte at `mySerial2` RX GPIO12; ESP32 must not append CR/LF.
- The supplied Arduino code recognizes 21 input bytes, including `0x31`/`'1'` and `0x38`/`'8'`; preserve this exact table.
- Web functionality is limited to camera/pose recognition and pose-result relay; it must not configure, start, stop, reset, or directly control the competition.
- Stage changes come from SU-03T events; a web pose result can only become a visual-recognition event while the controller is in the visual stage.
- INMP441 input uses the existing I2S configuration: BCLK GPIO40, WS GPIO39, DATA GPIO38, 16 kHz, 32-bit samples.
- The five source WAV files under `D:\robot\舞蹈-表演赛指定音乐` are offline inputs only; do not copy the approximately 14.05 MB of raw WAV data into firmware.
- Preserve the existing uncommitted changes in `main.c`, `web.c`, `wifi.c`, and `camera_preview_config.h`; stage only files belonging to the implementation task.

---

## File Map

Create the following focused units:

- `components/BSP/SERIAL_BRIDGE/su03_protocol.h/.c`: SU-03T frame builder/parser.
- `components/BSP/SERIAL_BRIDGE/arduino_command_map.h/.c`: the 21-byte Arduino input contract.
- `components/BSP/SERIAL_BRIDGE/serial_bridge.h/.c`: UART1/UART2 setup, RX tasks, TX locking, and callbacks.
- `components/BSP/COMPETITION/competition_controller.h/.c`: event-driven competition state machine.
- `components/BSP/MUSIC/music_recognizer.h/.c`: INMP441 capture task and music-recognition event boundary.
- `tools/music_features.py`: validate the five WAV sources and emit compact normalized feature data.
- `tools/serial_contract.py`: host-side reference implementation of the SU-03T frame and Arduino byte set.
- `tests/test_serial_contract.py`: host-side byte/frame contract tests.
- `tests/test_competition_controller.py`: deterministic controller transition tests.

Modify only the integration points:

- `components/BSP/CMakeLists.txt`: register SERIAL_BRIDGE, COMPETITION, and MUSIC directories and their dependencies.
- `components/BSP/WEB/web.c`: add the pose-result POST endpoint and a page hook without adding competition controls.
- `main/main.c`: initialize modules and route SU-03T, web, music, and action events.

## Task 1: Lock the SU-03T and Arduino wire contracts

**Files:**
- Create: `components/BSP/SERIAL_BRIDGE/su03_protocol.h`
- Create: `components/BSP/SERIAL_BRIDGE/su03_protocol.c`
- Create: `components/BSP/SERIAL_BRIDGE/arduino_command_map.h`
- Create: `components/BSP/SERIAL_BRIDGE/arduino_command_map.c`
- Create: `tools/serial_contract.py`
- Create: `tests/test_serial_contract.py`

**Interfaces:**

```c
typedef struct {
    uint8_t state;
    uint8_t message_id;
} su03_parser_t;

void su03_parser_init(su03_parser_t *parser);
bool su03_parser_push(su03_parser_t *parser, uint8_t byte,
                      uint8_t *message_id_out);
size_t su03_build_message_frame(uint8_t message_id, uint8_t out[5]);
bool arduino_command_is_known(uint8_t byte);
```

- [ ] Write `tools/serial_contract.py` with `build_frame(message_id)`, `parse(bytes_iterable)`, and the constant `ARDUINO_COMMAND_BYTES`; initially make each helper raise `NotImplementedError`.
- [ ] Write Python tests importing that reference module for the exact frame and mapping contract:

```python
def test_message_4_frame():
    assert build_frame(4) == bytes.fromhex("AA 55 04 55 AA")

def test_split_frame_and_resynchronization():
    assert parse([0xAA, 0x55, 0x04, 0x55, 0xAA]) == [4]
    assert parse([0xAA, 0x55, 0x04, 0x00, 0xAA,
                  0xAA, 0x55, 0x07, 0x55, 0xAA]) == [7]

def test_arduino_command_contract_has_21_known_bytes():
    assert len(ARDUINO_COMMAND_BYTES) == 21
    assert 0x31 in ARDUINO_COMMAND_BYTES
    assert 0x38 in ARDUINO_COMMAND_BYTES
    assert 0x00 not in ARDUINO_COMMAND_BYTES
```

- [ ] Run `rtk python tests/test_serial_contract.py` and verify it fails with `NotImplementedError` from the reference helpers.
- [ ] Implement the parser as a four-state byte machine: wait for `0xAA`, require `0x55`, accept one message ID, then require `0x55` and `0xAA`; reset to header search on any mismatch.
- [ ] Implement `su03_build_message_frame()` to write exactly five bytes and reject message IDs outside `1..7` with a zero return length.
- [ ] Implement `arduino_command_is_known()` for exactly the 21 bytes from the supplied Arduino code: `31,33,32,34,39,61,62,63,42,43,46,66,64,65,41,35,37,36,38,44,45` in hexadecimal.
- [ ] Replace the Python reference stubs with the exact five-byte parser/builder and 21-byte set used by the C implementation, then rerun the tests.
- [ ] Run `rtk python tests/test_serial_contract.py` and verify it passes.
- [ ] Commit only the protocol and contract-test files with `test: lock serial wire contracts`.

## Task 2: Add the dual hardware UART bridge

**Files:**
- Create: `components/BSP/SERIAL_BRIDGE/serial_bridge.h`
- Create: `components/BSP/SERIAL_BRIDGE/serial_bridge.c`
- Modify: `components/BSP/CMakeLists.txt`

**Interfaces:**

```c
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
```

- [ ] Add the SERIAL_BRIDGE source/include directory to the existing BSP component and add the UART driver dependency without changing unrelated component registration.
- [ ] Configure `UART_NUM_1` as SU-03T at RX GPIO1/TX GPIO2 and `UART_NUM_2` as Arduino at RX GPIO3/TX GPIO21, both with 4096-byte RX buffers and `115200, 8N1`.
- [ ] Create one FreeRTOS RX task per UART. The SU-03 task must call the raw-byte callback for every byte, feed the SU-03 frame parser, and call the message callback only after a valid five-byte frame. The Arduino task must only report/log incoming bytes because the supplied Arduino code does not transmit on `mySerial2`.
- [ ] Protect both TX paths with a mutex. `serial_bridge_send_to_arduino()` must call `uart_write_bytes(UART_NUM_2, &byte, 1)` with exactly one byte. `serial_bridge_send_to_su03_message()` must call the frame builder and write exactly five bytes to UART1.
- [ ] Return `ESP_ERR_INVALID_STATE` before initialization, `ESP_ERR_INVALID_ARG` for null callbacks or invalid message IDs, and the UART driver error for failed writes.
- [ ] Build the project with `rtk idf.py build`; expected result is a successful compile with no new warnings from SERIAL_BRIDGE.
- [ ] Commit the bridge with `feat: add dual hardware uart bridge`.

## Task 3: Implement the competition controller

**Files:**
- Create: `components/BSP/COMPETITION/competition_controller.h`
- Create: `components/BSP/COMPETITION/competition_controller.c`
- Create: `tests/test_competition_controller.py`
- Modify: `components/BSP/CMakeLists.txt`

**Interfaces:**

```c
typedef enum {
    COMPETITION_IDLE,
    COMPETITION_READY_FOR_STAGE,
    COMPETITION_RUNNING_RECOGNITION,
    COMPETITION_DISPATCH_ACTION,
    COMPETITION_WAIT_ACTION_DONE,
    COMPETITION_MUSIC_RECOGNITION,
    COMPETITION_MUSIC_PERFORMANCE,
    COMPETITION_COMPLETE,
    COMPETITION_ABORTED,
    COMPETITION_FAULT,
} competition_state_t;

typedef struct {
    competition_state_t state;
    uint8_t current_stage;
    uint8_t current_item;
    uint32_t total_elapsed_ms;
    uint32_t stage_elapsed_ms;
    bool stage_seen[3];
} competition_status_t;

esp_err_t competition_init(void);
esp_err_t competition_on_prepare_command(void);
esp_err_t competition_on_stage_command(uint8_t stage_event_id);
esp_err_t competition_on_voice_recognized(uint8_t id);
esp_err_t competition_on_visual_recognized(uint8_t id);
esp_err_t competition_on_music_recognized(uint8_t id);
esp_err_t competition_on_action_done(bool success);
esp_err_t competition_on_music_performance_done(void);
esp_err_t competition_get_status(competition_status_t *out);
```

- [ ] Write deterministic Python transition vectors for prepare-before-stage, arbitrary stage order, duplicate-stage rejection, visual-event rejection outside the visual stage, and completion after all three stages; the test model must implement only the legal transition table and must not call hardware APIs.
- [ ] Run `rtk python tests/test_competition_controller.py` and verify the transition vectors pass before wiring the C controller to UART or web code.
- [ ] Implement a single controller state protected by a FreeRTOS mutex; use `esp_timer_get_time()` for total/stage timing and never use a blocking delay inside event handlers.
- [ ] Make the stage command adapter explicit: the controller receives a normalized stage ID, while SU-03T raw-byte interpretation stays in the integration layer.
- [ ] Accept visual IDs only in the visual stage and return a rejected result without sending an action when the stage or item does not match.
- [ ] Enforce 15-minute total and 5-minute per-stage limits; transition to `COMPETITION_ABORTED` on timeout and stop dispatch callbacks.
- [ ] Expose status for logs and the pose POST response; do not add web controls.
- [ ] Run the transition tests and `rtk idf.py build`; then verify the same transition cases through the controller status logs before committing with `feat: add competition event state machine`.

## Task 4: Add the pose-result relay endpoint

**Files:**
- Modify: `components/BSP/WEB/web.c`
- Modify: `components/BSP/CMakeLists.txt`

**HTTP contract:**

```http
POST /api/vision/pose
Content-Type: application/json

{"pose_id":4,"confidence":0.93,"timestamp":1234}
```

Response shape:

```json
{"ok":true,"accepted":true,"state":"RUNNING_RECOGNITION"}
```

- [ ] Add a request-body limit of 256 bytes and read the body in a loop until `content_len` bytes are received; return HTTP 413 for a larger body and HTTP 400 for malformed JSON or missing fields.
- [ ] Parse `pose_id` as an unsigned 8-bit value, require `0.0 <= confidence <= 1.0`, and reject values below the controller’s configured confidence threshold with `accepted=false`.
- [ ] Call `competition_on_visual_recognized(pose_id)` only after the payload passes validation; never call `serial_bridge_send_to_arduino()` from `web.c`.
- [ ] Keep `/`, `/snapshot`, and port-81 `/stream` working. Add no start, stop, reset, configuration, stage-selection, or action-key controls to the HTML page.
- [ ] Add a browser-side `submitPoseResult(poseId, confidence, timestamp)` hook that POSTs the JSON payload; the recognition model remains a client-side integration point because no browser model asset exists in this repository.
- [ ] Verify manually with a connected ESP32 using `Invoke-RestMethod` requests for valid JSON, malformed JSON, low confidence, and a visual result sent during a non-visual stage.
- [ ] Run `rtk idf.py build` and commit with `feat: relay browser pose results to controller`.

## Task 5: Connect SU-03T events, Arduino actions, and main startup

**Files:**
- Modify: `main/main.c`
- Modify: `components/BSP/CMakeLists.txt`

**Routing rules:**

```c
static void on_su03_byte(uint8_t byte)
{
    if (arduino_command_is_known(byte)) {
        serial_bridge_send_to_arduino(byte);
    }
    route_su03_stage_or_voice_event(byte);
}

static void on_visual_result(uint8_t pose_id)
{
    competition_on_visual_recognized(pose_id);
}
```

- [ ] Initialize `competition_controller` and `serial_bridge` before starting the existing Wi-Fi/camera path, so UART input is available even when camera startup fails.
- [ ] Register callbacks that preserve every SU-03T raw byte in logs, forward only known Arduino action bytes, and route stage/voice/music command bytes to normalized controller events.
- [ ] Implement the confirmed SU-03T message IDs 1–7 as `serial_bridge_send_to_su03_message(id)` calls for visual/music/action status reporting. Keep the screenshot-confirmed `task3=0xCC` mapping and log unknown `task1/task2` payloads until hardware capture confirms their exact bytes; do not silently guess them.
- [ ] Ensure a web pose result can produce a controller event but cannot send an Arduino byte unless the controller’s action-dispatch callback authorizes it.
- [ ] Preserve the existing PC/local vision mode behavior and do not alter the user’s current dirty changes outside the new integration lines.
- [ ] Run `rtk idf.py build`, inspect the startup log for both UART initialization messages, and commit with `feat: route multimodal events through controller`.

## Task 6: Connect INMP441 capture and the five music references

**Files:**
- Create: `components/BSP/MUSIC/music_recognizer.h`
- Create: `components/BSP/MUSIC/music_recognizer.c`
- Create: `tools/music_features.py`
- Modify: `components/BSP/CMakeLists.txt`

**Interfaces:**

```c
esp_err_t music_recognizer_init(void);
esp_err_t music_recognizer_start(void);
esp_err_t music_recognizer_stop(void);
esp_err_t music_recognizer_get_last(uint8_t *music_id, float *confidence);
```

- [ ] Make `tools/music_features.py` read only `D:\robot\舞蹈-表演赛指定音乐\1.wav` through `5.wav`, verify PCM, stereo, 16-bit, 44.1/48 kHz, and duration 14–17 seconds, and fail with a named file if any check fails.
- [ ] Downmix to mono, resample to 16 kHz, normalize amplitude, compute fixed-size spectral/onset features, quantize them, and write a generated feature header under `components/BSP/MUSIC/generated_music_features.h`; never copy the original WAV files into the firmware.
- [ ] Add a Python asset test that asserts five IDs exist, all feature vectors have equal length, and each source file identifies most strongly with its own feature vector.
- [ ] Implement the recognizer task around the existing `audio_i2s_read_pcm16()` API. Only run it in `MUSIC_RECOGNITION`; publish `competition_on_music_recognized(id)` after the confidence threshold is met, otherwise keep the result as “unrecognized”.
- [ ] Keep the first runtime matcher bounded: fixed PCM buffer, fixed feature vector, no dynamic allocation inside the audio task, and clean stop when the controller leaves the music stage.
- [ ] Run `rtk python tools/music_features.py --source "D:\robot\舞蹈-表演赛指定音乐"` and the asset tests, then run `rtk idf.py build`.
- [ ] Commit with `feat: add INMP441 music recognition input`.

## Task 7: Verify the integrated hardware path

**Files:**
- Modify only files shown by failing verification, with each fix kept in the owning task’s commit.
- Create: `docs/hardware/competition-serial-smoke-test.md`

- [ ] Run `rtk git diff --check` and confirm only the four pre-existing dirty files plus the new implementation files are modified.
- [ ] Run `rtk idf.py build` from `D:\robot\ESP32` and record the final binary/build result.
- [ ] Flash and monitor the ESP32, then verify UART1 logs a raw SU-03T byte when a known voice command is spoken.
- [ ] Verify a known Arduino command reaches UART2 as exactly one byte and that the Arduino prints the expected `cmd` decimal/hex value.
- [ ] Call `serial_bridge_send_to_su03_message(4)` through the bench test path and verify the SU-03T RX sees `AA 55 04 55 AA`.
- [ ] Send a malformed SU-03T frame followed by `AA 55 07 55 AA` and verify only message 7 is reported.
- [ ] Connect the computer to `ESP32-Pose`, open `http://192.168.4.1/`, POST one pose result, and verify `accepted` changes only during the visual stage.
- [ ] Start music capture and verify the INMP441 task consumes PCM blocks without starving the camera or UART tasks.
- [ ] Record pin assignments, baud settings, exact observed bytes, and known limitations in `docs/hardware/competition-serial-smoke-test.md`.
- [ ] Run `rtk git status --short --branch` and report the implementation commits without staging the user’s unrelated changes.
