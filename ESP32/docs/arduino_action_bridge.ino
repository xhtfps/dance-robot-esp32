#include <SoftwareSerial.h>

// Arduino -> robot STM32 Bluetooth transparent serial
SoftwareSerial robotSerial(A0, A1);   // RX=A0, TX=A1

// ESP32 -> Arduino
SoftwareSerial espSerial(12, 13);     // RX=D12, TX=D13

const byte ACTION_ERROR = 0xFE;

struct ActionCommand {
  byte command;
  const char *name;
  const char *robotCommand;
  unsigned long durationMs;
};

// These durations are conservative estimates because STM32 cannot return
// an action-complete message. Measure each routine on the real robot and
// keep a safety margin of at least 300-500 ms.
const ActionCommand actions[] = {
  {0x31, "LA",    "7", 2500},
  {0x33, "LB",    "4", 2500},
  {0x32, "LC",    "6", 2500},
  {0x34, "LD",    "5", 2500},
  {0x39, "UP",    "D", 4000},
  {0x61, "DW",    "B", 4000},
  {0x62, "LF",    "A", 3500},
  {0x63, "RG",    "C", 3500},
  {0x42, "L1",    "K", 4000},
  {0x43, "L2",    "8", 4000},
  {0x66, "X",     "E", 3500},
  {0x64, "Y",     "H", 3500},
  {0x65, "A",     "F", 3500},
  {0x41, "B",     "G", 4000},
  {0x35, "RA",    "3", 4000},
  {0x36, "RC",    "2", 4000},
  {0x38, "RD",    "10", 4000},
  {0x44, "R1",    "L", 75000},  // Five official songs share one dance.
  {0x45, "R2",    "9", 105000}, // Custom dance: 90-120 s.
};

const ActionCommand resetCommand = {0x46, "RESET", "J", 1500};

const size_t ACTION_COUNT = sizeof(actions) / sizeof(actions[0]);

const ActionCommand *findAction(byte command)
{
  if (command == resetCommand.command) {
    return &resetCommand;
  }
  for (size_t i = 0; i < ACTION_COUNT; ++i) {
    if (actions[i].command == command) {
      return &actions[i];
    }
  }
  return nullptr;
}

void printHex(byte value)
{
  if (value < 0x10) {
    Serial.print("0");
  }
  Serial.print(value, HEX);
}

void sendResultToEsp32(byte value)
{
  espSerial.listen();
  espSerial.write(value);
  espSerial.flush();
}

void executeAction(byte command)
{
  const ActionCommand *action = findAction(command);
  if (action == nullptr) {
    Serial.println("未知动作，不发送给 STM32");
    sendResultToEsp32(ACTION_ERROR);
    return;
  }

  Serial.print("执行动作: ");
  Serial.println(action->name);

  // Keep the existing control path: Arduino sends the joystick-like
  // command over Bluetooth to STM32.
  robotSerial.println(action->robotCommand);
  robotSerial.flush();

  // STM32 cannot be changed, so completion is estimated by a measured
  // per-action duration. No next ESP32 command is accepted during this time.
  unsigned long start = millis();
  while (millis() - start < action->durationMs) {
    delay(1);
  }

  // ACK only after the estimated action duration has elapsed.
  sendResultToEsp32(command);

  Serial.print("动作时间结束，回传 ESP32: 0x");
  printHex(command);
  Serial.println();
}

void setup()
{
  Serial.begin(115200);
  robotSerial.begin(115200);
  espSerial.begin(115200);
  espSerial.listen();

  Serial.println("=== Arduino 动作桥接启动 ===");
}

void loop()
{
  espSerial.listen();

  if (espSerial.available()) {
    byte command = espSerial.read();

    Serial.print("ESP32 发来: 0x");
    printHex(command);
    Serial.println();

    executeAction(command);
  }
}
