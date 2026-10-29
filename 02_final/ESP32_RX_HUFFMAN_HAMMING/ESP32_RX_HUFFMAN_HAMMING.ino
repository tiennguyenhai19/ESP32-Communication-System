#include "common.h"

// ESP32 #3 — receiver, Hamming decoder, Huffman decoder and statistics.
static const uint32_t UART_BAUD = 115200;
static const int UART_RX_PIN = 16;
static const int UART_TX_PIN = 17;

static Frame frame;
static uint8_t huffmanBuffer[MAX_COMPRESSED_BYTES];
static uint8_t decodedText[MAX_TEXT_BYTES + 1];

static uint32_t totalFrames = 0;
static uint32_t crcPassFrames = 0;
static uint32_t crcFailFrames = 0;
static uint64_t totalRawBits = 0;
static uint64_t totalHuffmanBits = 0;
static uint64_t totalChannelBits = 0;
static uint64_t totalFrameBits = 0;
static uint64_t totalUartLineBits = 0;
static uint64_t totalCorrectedCodewords = 0;

static void printPerFrameTable(uint16_t seq, uint32_t rawBits, uint16_t huffmanBits,
                               uint16_t channelBits, uint16_t payloadBytes,
                               uint16_t correctedCodewords, float huffmanRatio,
                               float frameRatio, float uartEfficiency, bool crcOk, bool decodeOk) {
  Serial.println();
  Serial.println("================ BANG SO SANH TUNG GOI ================");
  Serial.println("SEQ | Raw(bit) | Huff(bit) | Ham(bit) | CR_Huff | CR_Frame | EtaUART% | CW_syn | KET_QUA");
  Serial.printf("%3u | %8lu | %9u | %8u | %7.3f | %8.3f | %8.2f | %6u | %s\n",
                seq, (unsigned long)rawBits, huffmanBits, channelBits,
                huffmanRatio, frameRatio, uartEfficiency * 100.0f, correctedCodewords,
                crcOk ? "CRC PASS" : (decodeOk ? "CRC FAIL" : "DECODE FAIL"));
  Serial.printf("Payload=%u byte | Header=%u byte | CRC16 goc=0x%04X\n",
                payloadBytes, HEADER_SIZE, frame.crcOriginal);
  Serial.println("CR_Huff = so bit goc / so bit Huffman; CR_Frame tinh ca header UART.");
  Serial.println("CW_syn = so tu ma Hamming co syndrome khac 0 (khong phai dem chinh xac moi bit loi).");
}

static void printCumulativeTable() {
  const float huffmanRatio = totalHuffmanBits
      ? (float)totalRawBits / (float)totalHuffmanBits : 0.0f;
  const float payloadRatio = totalChannelBits
      ? (float)totalRawBits / (float)totalChannelBits : 0.0f;
  const float frameRatio = totalFrameBits
      ? (float)totalRawBits / (float)totalFrameBits : 0.0f;
  const float huffmanSavingPct = totalRawBits
      ? 100.0f * (1.0f - (float)totalHuffmanBits / (float)totalRawBits) : 0.0f;

  Serial.println("---------------- THONG KE TICH LUY -------------------");
  Serial.printf("So frame=%lu | CRC PASS=%lu | CRC/Decode FAIL=%lu\n",
                (unsigned long)totalFrames,
                (unsigned long)crcPassFrames,
                (unsigned long)crcFailFrames);
  Serial.printf("Tong bit goc=%llu | tong bit Huffman=%llu | tong bit Hamming=%llu\n",
                (unsigned long long)totalRawBits,
                (unsigned long long)totalHuffmanBits,
                (unsigned long long)totalChannelBits);
  Serial.printf("CR Huffman tong = %.3f | tiet kiem Huffman = %.2f%%\n",
                huffmanRatio, huffmanSavingPct);
  Serial.printf("CR payload sau Hamming = %.3f | CR tinh ca header = %.3f\n",
                payloadRatio, frameRatio);
  Serial.printf("Hieu suat bit thong tin / bit tren duong UART 8N1 = %.2f%%\n",
                totalUartLineBits ? 100.0f * (float)totalRawBits / (float)totalUartLineBits : 0.0f);
  Serial.printf("Tong CW co syndrome khac 0 = %llu\n",
                (unsigned long long)totalCorrectedCodewords);
  Serial.println("-------------------------------------------------------");
}

void setup() {
  Serial.begin(115200);
  delay(800);
  Serial.println("\nESP32 #3 — RX: Hamming(7,4) + Huffman + statistics");
  Serial2.begin(UART_BAUD, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);
  Serial2.setTimeout(1000);

  if (!buildHuffmanCodebook()) {
    Serial.println("[RX] KHONG tao duoc bang ma Huffman. Kiem tra TRAINING_CORPUS.");
    return;
  }
  Serial.println("[RX] Bang ma Huffman co dinh da duoc tao.");
  Serial.println("[RX] Dang doi frame tu ESP32 #2...");
}

void loop() {
  if (g_huffRoot < 0) {
    delay(1000);
    return;
  }
  if (!readFrame(Serial2, frame, 250)) return;

  memset(huffmanBuffer, 0, sizeof(huffmanBuffer));
  memset(decodedText, 0, sizeof(decodedText));
  uint16_t correctedCodewords = 0;
  const bool hammingOk = hammingDecode(frame.payload, frame.channelBits,
                                      frame.huffmanBits, huffmanBuffer,
                                      sizeof(huffmanBuffer), correctedCodewords);
  uint16_t decodedLen = 0;
  bool decodeOk = false;
  if (hammingOk) {
    decodeOk = huffmanDecode(huffmanBuffer, frame.huffmanBits, decodedText,
                            frame.originalLen, MAX_TEXT_BYTES, decodedLen);
  }
  if (decodeOk && decodedLen <= MAX_TEXT_BYTES) decodedText[decodedLen] = '\0';
  const uint16_t decodedCrc = decodeOk
      ? crc16Ccitt(decodedText, decodedLen) : 0;
  const bool crcOk = decodeOk && decodedLen == frame.originalLen &&
                     decodedCrc == frame.crcOriginal;

  const uint32_t rawBits = (uint32_t)frame.originalLen * 8U;
  const float huffmanRatio = frame.huffmanBits
      ? (float)rawBits / (float)frame.huffmanBits : 0.0f;
  const float frameRatio = (float)rawBits /
      (float)((HEADER_SIZE + frame.payloadBytes) * 8U);
  const float uartEfficiency = (float)rawBits /
      (float)((HEADER_SIZE + frame.payloadBytes) * 10U);

  Serial.println();
  Serial.println("================ ESP32 #3: RECEIVER ===================");
  Serial.printf("Nhan SEQ=%u | originalLen=%u byte | HuffmanBits=%u | HammingBits=%u\n",
                frame.seq, frame.originalLen, frame.huffmanBits, frame.channelBits);
  Serial.print("Payload nhan (HEX): ");
  printHexBytes(Serial, frame.payload, frame.payloadBytes);
  Serial.printf("Hamming decoder: %s | so codeword co syndrome khac 0=%u\n",
                hammingOk ? "OK" : "FAIL", correctedCodewords);
  Serial.printf("Huffman decoder: %s | bytes tao ra=%u\n",
                decodeOk ? "OK" : "FAIL", decodedLen);
  if (decodeOk) Serial.printf("Du lieu giai ma: %s\n", (const char *)decodedText);
  Serial.printf("CRC16 nhan trong header=0x%04X | CRC16 tinh lai=0x%04X | %s\n",
                frame.crcOriginal, decodedCrc, crcOk ? "PASS" : "FAIL");

  ++totalFrames;
  if (crcOk) ++crcPassFrames;
  else ++crcFailFrames;
  totalRawBits += rawBits;
  totalHuffmanBits += frame.huffmanBits;
  totalChannelBits += frame.channelBits;
  totalFrameBits += (uint64_t)(HEADER_SIZE + frame.payloadBytes) * 8U;
  totalUartLineBits += (uint64_t)(HEADER_SIZE + frame.payloadBytes) * 10U;
  totalCorrectedCodewords += correctedCodewords;

  printPerFrameTable(frame.seq, rawBits, frame.huffmanBits, frame.channelBits,
                     frame.payloadBytes, correctedCodewords, huffmanRatio,
                     frameRatio, uartEfficiency, crcOk, decodeOk);
  printCumulativeTable();
}
