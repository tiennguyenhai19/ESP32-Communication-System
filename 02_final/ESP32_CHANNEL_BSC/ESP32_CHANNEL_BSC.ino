#include <Arduino.h>
#include <esp_system.h>
#include "common.h"

// ESP32 #2 — channel model BSC (Binary Symmetric Channel)
static const uint32_t UART_BAUD = 115200;
static const int INPUT_RX_PIN = 16;
static const int INPUT_TX_PIN = 17;
static const int OUTPUT_RX_PIN = 25;
static const int OUTPUT_TX_PIN = 26;
static const float P_ERROR = 0.02f;   // Test 0.00, 0.01, 0.02, 0.05

static Frame frame;
static uint32_t totalFrames = 0;
static uint64_t totalChannelBits = 0;
static uint64_t totalFlippedBits = 0;

static void printChannelTotals() {
  const float measuredBer = totalChannelBits
      ? (float)totalFlippedBits / (float)totalChannelBits : 0.0f;
  Serial.printf("[BSC TOTAL] frames=%lu | tested bits=%llu | flipped=%llu | measured BER=%.6f | configured p=%.4f\n",
                (unsigned long)totalFrames,
                (unsigned long long)totalChannelBits,
                (unsigned long long)totalFlippedBits,
                measuredBer, P_ERROR);
}

void setup() {
  Serial.begin(115200);
  delay(800);
  Serial.println("\nESP32 #2 — BSC channel model");
  Serial.printf("Configured bit-error probability P_ERROR = %.4f\n", P_ERROR);
  Serial1.begin(UART_BAUD, SERIAL_8N1, INPUT_RX_PIN, INPUT_TX_PIN);
  Serial2.begin(UART_BAUD, SERIAL_8N1, OUTPUT_RX_PIN, OUTPUT_TX_PIN);
  Serial1.setTimeout(1000);
  Serial2.setTimeout(1000);
  Serial.println("[BSC] Chi dao bit trong payload Hamming; header duoc giu nguyen.");
}

void loop() {
  if (!readFrame(Serial1, frame, 250)) return;

  uint32_t flipped = 0;
  const uint16_t payloadBytes = frame.payloadBytes;
  Serial.println();
  Serial.println("---------------- ESP32 #2: BSC CHANNEL ----------------");
  Serial.printf("SEQ=%u | Huffman bits=%u | channel-coded bits=%u | payload bytes=%u\n",
                frame.seq, frame.huffmanBits, frame.channelBits, payloadBytes);
  Serial.print("Payload truoc BSC (HEX): ");
  printHexBytes(Serial, frame.payload, payloadBytes);

  for (uint16_t bitIndex = 0; bitIndex < frame.channelBits; ++bitIndex) {
    const double randomUnit = (double)esp_random() / 4294967296.0;
    if (randomUnit < (double)P_ERROR) {
      setPackedBit(frame.payload, bitIndex, !getPackedBit(frame.payload, bitIndex));
      ++flipped;
    }
  }

  Serial.print("Payload sau BSC (HEX):   ");
  printHexBytes(Serial, frame.payload, payloadBytes);
  const float measuredBer = frame.channelBits
      ? (float)flipped / (float)frame.channelBits : 0.0f;
  Serial.printf("Bit duoc dao=%lu / %u | BER mau quan sat=%.6f | p cau hinh=%.4f\n",
                (unsigned long)flipped, frame.channelBits, measuredBer, P_ERROR);

  Serial2.write(frame.header, HEADER_SIZE);
  Serial2.write(frame.payload, payloadBytes);
  Serial2.flush();
  Serial.println("[BSC] Da chuyen tiep frame sang ESP32 #3.");
  Serial.println("-------------------------------------------------------");

  ++totalFrames;
  totalChannelBits += frame.channelBits;
  totalFlippedBits += flipped;
  printChannelTotals();
}
