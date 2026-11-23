# Competition Serial Smoke Test

## Current firmware settings

| Link | ESP32 UART | ESP32 pins | Format |
|---|---|---|---|
| SU-03T | UART1 | RX GPIO1, TX GPIO2 | 115200, 8N1 |
| Arduino `mySerial2` | UART2 | RX GPIO20, TX GPIO21 | 115200, 8N1 |

Arduino-side pins from the supplied sketch:

- `mySerial2(12, 13)`: Arduino RX GPIO12, TX GPIO13.
- `mySerial1(A0, A1)`: Arduino-side robot-controller link.

## SU-03T feedback frame

ESP32 to SU-03T message `4` must be exactly:

```text
AA 55 04 55 AA
```

No CR/LF is appended. Message IDs 1–12 use the same five-byte layout. Visual IDs use 1–6; official music IDs 1–5 map to message IDs 7–11, and custom music ID 6 maps to message ID 12.

## Arduino action bytes

The ESP32 forwards one raw byte to Arduino `mySerial2` without CR/LF. The Arduino sketch contains exactly 19 ordered competition action bytes. `RB` (`0x37`, robot command `"1"`) is intentionally unused. RESET is preserved separately as maintenance command `0x46`, robot command `"J"`, duration 1500 ms.

## Bench commands

From an ESP-IDF environment:

```powershell
idf.py build
idf.py -p COM5 flash
idf.py -p COM5 monitor
```

The attempted flash on 2026-08-17 did not reach the device because `COM5` was not present or was busy. Re-run with the actual ESP32-S3 port.

Expected startup log lines include:

```text
UART1 SU-03T: RX=1 TX=2 baud=115200
UART2 Arduino: RX=20 TX=21 baud=115200
AP 已启动: SSID=ESP32-Pose, password=12345678
```

## Verification sequence

1. Connect the correct ESP32-S3 serial port and flash the image.
2. Confirm the two UART initialization lines and the SoftAP startup line.
3. Connect SU-03T UART1_TX to ESP32 GPIO1. Send `0x44` for task one, then verify voice inputs `0x01` and `0x0D` reach the first and thirteenth Arduino mappings.
4. Verify the corresponding single byte appears at ESP32 GPIO21 and reaches Arduino `mySerial2` RX GPIO12; ESP32 UART2 RX feedback is on GPIO20.
5. Trigger the ESP32 message-4 bench path and verify SU-03T UART1_RX sees `AA 55 04 55 AA`.
6. Send a malformed frame followed by `AA 55 07 55 AA`; verify only message 7 is reported.
7. Send `0x55`, then `0x22`, connect a computer to `ESP32-Pose`, and POST a pose result to `/api/vision/pose`. Verify visual IDs 5 and 6 produce the same Arduino bytes as voice IDs 12 and 13.
8. Send `0x66`, then `0x33`, and verify INMP441 PCM capture starts without UART or camera errors.
9. For official music, verify speech message 7–11 appears before Arduino command `0x44`; no dance byte may be sent during the first 4999 ms after recognition.
10. For custom test music `试音.mp3`, verify speech message 12 appears before Arduino command `0x45`; no dance byte may be sent during the first 4999 ms after recognition.
11. Verify the official dance ACK is accepted after its 75-second action without a 20-second timeout, and the custom dance ACK is accepted after its 105-second action without a 20-second timeout.
12. In task one, verify SU-03T parameters `10`, `11`, `12`, and `13` reach ESP32 as either decimal `0x0A`-`0x0D` or hex-style `0x10`-`0x13`; both encodings must trigger 向右转、防御、叉腰、敬礼 respectively.
