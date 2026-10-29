#include <Arduino.h>

// Stage 1: byte-for-byte UART relay. No noise is added in this test.
// ESP32 #1 GPIO17 TX -> this board GPIO16 RX.
// This board GPIO26 TX -> ESP32 #3 GPIO16 RX. Share GND.
static const int INPUT_RX_PIN = 16;
static const int INPUT_TX_PIN = 17;
static const int OUTPUT_RX_PIN = 25;
static const int OUTPUT_TX_PIN = 26;
static char lineBuffer[100];
static size_t lineLength = 0;

void setup() {
  Serial.begin(115200);
  Serial1.begin(115200, SERIAL_8N1, INPUT_RX_PIN, INPUT_TX_PIN);
  Serial2.begin(115200, SERIAL_8N1, OUTPUT_RX_PIN, OUTPUT_TX_PIN);
  Serial.println("[CHANNEL UART TEST] Relay byte-for-byte, khong them loi.");
}

void loop() {
  while (Serial1.available() > 0) {
    const char c = (char)Serial1.read();
    Serial2.write((uint8_t)c);

    if (c == '\n') {
      lineBuffer[lineLength] = '\0';
      Serial.printf("[CHANNEL] Da chuyen tiep: %s\n", lineBuffer);
      lineLength = 0;
    } else if (lineLength < sizeof(lineBuffer) - 1) {
      lineBuffer[lineLength++] = c;
    } else {
      lineLength = 0;
    }
  }
}
