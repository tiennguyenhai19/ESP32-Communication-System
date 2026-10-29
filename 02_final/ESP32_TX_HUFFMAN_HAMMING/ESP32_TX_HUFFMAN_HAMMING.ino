#include "common.h"

// ESP32 #1 — transmitter
// UART2 TX GPIO17 -> ESP32 #2 RX GPIO16; all boards share GND.
static const uint32_t UART_BAUD = 115200;
static const int UART_RX_PIN = 16;
static const int UART_TX_PIN = 17;
static const uint32_t SEND_INTERVAL_MS = 3000;

static const char *TEST_MESSAGES[] = {
  "ESP32 UART TEST MESSAGE 001",
  "HUFFMAN SOURCE CODING TEST",
  "HAMMING 7,4 WITH BSC NOISE",
  "BER CORRECTION AND DATA FRAME"
};
static const uint8_t MESSAGE_COUNT = sizeof(TEST_MESSAGES) / sizeof(TEST_MESSAGES[0]);

static uint16_t sequenceNumber = 1;
static uint8_t huffmanBuffer[MAX_COMPRESSED_BYTES];
static uint8_t hammingBuffer[MAX_PAYLOAD_BYTES];
static uint32_t lastSendMs = 0;
static bool codebookReady = false;

static void sendOneMessage(const char *message) {
  const uint16_t originalLen = (uint16_t)strlen(message);
  if (originalLen == 0 || originalLen > MAX_TEXT_BYTES) {
    Serial.println("[TX] Loi: do dai ban tin khong hop le.");
    return;
  }

  uint16_t huffmanBits = 0;
  uint16_t channelBits = 0;
  if (!huffmanEncode((const uint8_t *)message, originalLen, huffmanBuffer,
                     sizeof(huffmanBuffer), huffmanBits)) {
    Serial.println("[TX] Loi ma hoa Huffman. Kiem tra TRAINING_CORPUS.");
    return;
  }
  if (!hammingEncode(huffmanBuffer, huffmanBits, hammingBuffer,
                     sizeof(hammingBuffer), channelBits)) {
    Serial.println("[TX] Loi ma hoa Hamming hoac tran bo dem.");
    return;
  }

  const uint16_t crc = crc16Ccitt((const uint8_t *)message, originalLen);
  uint8_t header[HEADER_SIZE];
  makeFrameHeader(header, sequenceNumber, originalLen, huffmanBits, channelBits, crc);
  const uint16_t payloadBytes = (uint16_t)((channelBits + 7U) / 8U);

  const float huffmanRatio = (float)(originalLen * 8U) / (float)huffmanBits;
  const float netPayloadRatio = (float)(originalLen * 8U) / (float)channelBits;
  const float frameRatio = (float)(originalLen * 8U) /
      (float)((HEADER_SIZE + payloadBytes) * 8U);
  const float uartEfficiency = (float)(originalLen * 8U) /
      (float)((HEADER_SIZE + payloadBytes) * 10U);

  Serial.println();
  Serial.println("================ ESP32 #1: TRANSMITTER ================");
  Serial.printf("SEQ: %u\n", sequenceNumber);
  Serial.printf("Du lieu goc (%u byte): %s\n", originalLen, message);
  Serial.printf("CRC16-CCITT: 0x%04X\n", crc);
  Serial.printf("Bit goc: %u | Bit Huffman: %u | Bit Hamming(7,4): %u\n",
                originalLen * 8U, huffmanBits, channelBits);
  Serial.printf("CR Huffman = raw/Huffman = %.3f\n", huffmanRatio);
  Serial.printf("CR payload sau Hamming = raw/Hamming = %.3f\n", netPayloadRatio);
  Serial.printf("CR tinh ca header (byte framing, 8-bit/byte) = %.3f\n", frameRatio);
  Serial.printf("Hieu suat bit thong tin / bit tren duong UART 8N1 = %.2f%%\n",
                uartEfficiency * 100.0f);
  Serial.print("Chuoi bit Huffman (MSB-first): ");
  printPackedBits(Serial, huffmanBuffer, huffmanBits);
  Serial.printf("Payload Hamming HEX (%u byte; chi truong nay co the bi BSC dao bit):\n", payloadBytes);
  printHexBytes(Serial, hammingBuffer, payloadBytes);

  Serial2.write(header, HEADER_SIZE);
  Serial2.write(hammingBuffer, payloadBytes);
  Serial2.flush();
  Serial.println("[TX] Da gui header + payload qua UART2.");
  Serial.println("=======================================================");

  ++sequenceNumber;
}

void setup() {
  Serial.begin(115200);
  delay(800);
  Serial.println("\nESP32 #1 — TX: Huffman + Hamming(7,4)");
  Serial2.begin(UART_BAUD, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);
  Serial2.setTimeout(1000);

  codebookReady = buildHuffmanCodebook();
  if (!codebookReady) {
    Serial.println("[TX] KHONG tao duoc bang ma Huffman. Dung chuong trinh.");
    return;
  }
  Serial.println("[TX] Bang ma Huffman co dinh da duoc tao.");
  Serial.println("[TX] Bat dau gui ban tin moi moi 3 giay.");
  lastSendMs = millis() - SEND_INTERVAL_MS;
}

void loop() {
  if (!codebookReady) {
    delay(1000);
    return;
  }
  if ((uint32_t)(millis() - lastSendMs) >= SEND_INTERVAL_MS) {
    lastSendMs = millis();
    const uint8_t messageIndex = (uint8_t)((sequenceNumber - 1U) % MESSAGE_COUNT);
    sendOneMessage(TEST_MESSAGES[messageIndex]);
  }
}
