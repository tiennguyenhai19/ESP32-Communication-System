#include <Arduino.h>

// Stage 1: receive ASCII lines from ESP32 #2 GPIO26 TX into GPIO16 RX.
static const int UART_RX_PIN = 16;
static const int UART_TX_PIN = 17;
static char lineBuffer[120];
static size_t lineLength = 0;

void setup() {
  Serial.begin(115200);
  Serial2.begin(115200, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);
  Serial.println("[RX UART TEST] Dang doi du lieu tu ESP32 #2.");
}

void loop() {
  while (Serial2.available() > 0) {
    const char c = (char)Serial2.read();
    if (c == '\n') {
      lineBuffer[lineLength] = '\0';
      Serial.printf("[RX] Nhan duoc: %s\n", lineBuffer);
      lineLength = 0;
    } else if (c != '\r' && lineLength < sizeof(lineBuffer) - 1) {
      lineBuffer[lineLength++] = c;
    } else if (lineLength >= sizeof(lineBuffer) - 1) {
      lineLength = 0;
    }
  }
}
