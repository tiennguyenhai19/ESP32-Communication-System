#include <Arduino.h>

// Stage 1: bare UART link test, before adding Huffman/Hamming/BSC.
// GPIO17 TX -> ESP32 #2 GPIO16 RX; share GND.
static const int UART_RX_PIN = 16;
static const int UART_TX_PIN = 17;
static uint32_t seq = 1;

void setup() {
  Serial.begin(115200);
  Serial2.begin(115200, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);
  Serial.println("[TX UART TEST] Dang gui chuoi ASCII qua UART2.");
}

void loop() {
  char line[80];
  snprintf(line, sizeof(line), "UART_TEST seq=%lu data=HELLO_ESP32\n", (unsigned long)seq++);
  Serial.print("TX -> ");
  Serial.print(line);
  Serial2.print(line);
  Serial2.flush();
  delay(1000);
}
