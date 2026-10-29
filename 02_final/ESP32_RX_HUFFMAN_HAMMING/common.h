#pragma once

#include <Arduino.h>
#include <stdint.h>
#include <string.h>

// ============================================================
// Shared protocol and algorithms for the 3-ESP32 project.
// Huffman codebook is fixed: TX and RX build it from the same
// training corpus. Only the Hamming-coded payload is exposed to BSC.
// ============================================================

static const uint8_t FRAME_MAGIC_0 = 0xA5;
static const uint8_t FRAME_MAGIC_1 = 0x5A;
static const uint8_t PROTOCOL_VERSION = 1;
static const uint16_t HEADER_SIZE = 13;
static const uint16_t MAX_TEXT_BYTES = 128;
static const uint16_t MAX_COMPRESSED_BYTES = 512;
static const uint16_t MAX_PAYLOAD_BYTES = 1024;
static const uint8_t MAX_CODE_BITS = 31;
static const uint16_t MAX_HUFF_NODES = 512;

// Every character used by the test messages is present here.
// Do not change this corpus on only one board: TX and RX must agree.
static const char TRAINING_CORPUS[] =
    "ESP32 UART TEST MESSAGE 001 "
    "HUFFMAN SOURCE CODING TEST "
    "HAMMING 7,4 WITH BSC NOISE "
    "BER CORRECTION AND DATA FRAME";

struct HuffNode {
  uint32_t weight;
  int16_t symbol;       // 0..255 for a leaf, -1 for an internal node
  int16_t left;
  int16_t right;
  uint16_t minSymbol;   // deterministic tie break
};

static HuffNode g_huffNodes[MAX_HUFF_NODES];
static uint16_t g_huffNodeCount = 0;
static int16_t g_huffRoot = -1;
static uint32_t g_huffCode[256];
static uint8_t g_huffCodeLen[256];

struct Frame {
  uint8_t header[HEADER_SIZE];
  uint8_t payload[MAX_PAYLOAD_BYTES];
  uint16_t seq;
  uint16_t originalLen;
  uint16_t huffmanBits;
  uint16_t channelBits;
  uint16_t crcOriginal;
  uint16_t payloadBytes;
};

static uint16_t readU16LE(const uint8_t *p) {
  return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static void writeU16LE(uint8_t *p, uint16_t value) {
  p[0] = (uint8_t)(value & 0xFF);
  p[1] = (uint8_t)((value >> 8) & 0xFF);
}

static bool getPackedBit(const uint8_t *buffer, uint32_t bitIndex) {
  return ((buffer[bitIndex >> 3] >> (7 - (bitIndex & 7))) & 0x01) != 0;
}

static void setPackedBit(uint8_t *buffer, uint32_t bitIndex, bool value) {
  const uint8_t mask = (uint8_t)(1U << (7 - (bitIndex & 7)));
  if (value) {
    buffer[bitIndex >> 3] |= mask;
  } else {
    buffer[bitIndex >> 3] &= (uint8_t)~mask;
  }
}

static uint16_t crc16Ccitt(const uint8_t *data, uint16_t length) {
  uint16_t crc = 0xFFFF;
  for (uint16_t i = 0; i < length; ++i) {
    crc ^= (uint16_t)data[i] << 8;
    for (uint8_t b = 0; b < 8; ++b) {
      if (crc & 0x8000) {
        crc = (uint16_t)((crc << 1) ^ 0x1021);
      } else {
        crc = (uint16_t)(crc << 1);
      }
    }
  }
  return crc;
}

static bool nodeLess(uint16_t a, uint16_t b) {
  if (g_huffNodes[a].weight != g_huffNodes[b].weight) {
    return g_huffNodes[a].weight < g_huffNodes[b].weight;
  }
  if (g_huffNodes[a].minSymbol != g_huffNodes[b].minSymbol) {
    return g_huffNodes[a].minSymbol < g_huffNodes[b].minSymbol;
  }
  return a < b;
}

static bool assignHuffmanCodes(int16_t nodeIndex, uint32_t code, uint8_t depth) {
  if (nodeIndex < 0) return false;
  const HuffNode &node = g_huffNodes[nodeIndex];

  if (node.symbol >= 0) {
    const uint8_t actualDepth = (depth == 0) ? 1 : depth;
    if (actualDepth > MAX_CODE_BITS) return false;
    g_huffCode[(uint8_t)node.symbol] = (depth == 0) ? 0 : code;
    g_huffCodeLen[(uint8_t)node.symbol] = actualDepth;
    return true;
  }

  if (depth >= MAX_CODE_BITS) return false;
  if (!assignHuffmanCodes(node.left, (uint32_t)(code << 1), (uint8_t)(depth + 1))) return false;
  if (!assignHuffmanCodes(node.right, (uint32_t)((code << 1) | 1U), (uint8_t)(depth + 1))) return false;
  return true;
}

static bool buildHuffmanCodebook() {
  uint32_t frequencies[256] = {0};
  memset(g_huffCode, 0, sizeof(g_huffCode));
  memset(g_huffCodeLen, 0, sizeof(g_huffCodeLen));
  g_huffNodeCount = 0;
  g_huffRoot = -1;

  for (size_t i = 0; i < sizeof(TRAINING_CORPUS) - 1; ++i) {
    frequencies[(uint8_t)TRAINING_CORPUS[i]]++;
  }

  bool hasParent[MAX_HUFF_NODES] = {false};
  for (uint16_t symbol = 0; symbol < 256; ++symbol) {
    if (frequencies[symbol] == 0) continue;
    if (g_huffNodeCount >= MAX_HUFF_NODES) return false;
    HuffNode &n = g_huffNodes[g_huffNodeCount++];
    n.weight = frequencies[symbol];
    n.symbol = (int16_t)symbol;
    n.left = -1;
    n.right = -1;
    n.minSymbol = symbol;
  }

  if (g_huffNodeCount == 0) return false;
  if (g_huffNodeCount == 1) {
    g_huffRoot = 0;
    return assignHuffmanCodes(g_huffRoot, 0, 0);
  }

  uint16_t rootsRemaining = g_huffNodeCount;
  while (rootsRemaining > 1) {
    int16_t first = -1;
    int16_t second = -1;

    for (uint16_t i = 0; i < g_huffNodeCount; ++i) {
      if (hasParent[i]) continue;
      if (first < 0 || nodeLess(i, (uint16_t)first)) {
        second = first;
        first = (int16_t)i;
      } else if (second < 0 || nodeLess(i, (uint16_t)second)) {
        second = (int16_t)i;
      }
    }

    if (first < 0 || second < 0 || g_huffNodeCount >= MAX_HUFF_NODES) return false;

    HuffNode &parent = g_huffNodes[g_huffNodeCount];
    parent.weight = g_huffNodes[first].weight + g_huffNodes[second].weight;
    parent.symbol = -1;
    parent.left = first;
    parent.right = second;
    parent.minSymbol = (g_huffNodes[first].minSymbol < g_huffNodes[second].minSymbol)
                           ? g_huffNodes[first].minSymbol
                           : g_huffNodes[second].minSymbol;

    hasParent[first] = true;
    hasParent[second] = true;
    hasParent[g_huffNodeCount] = false;
    ++g_huffNodeCount;
    --rootsRemaining;
  }

  for (uint16_t i = 0; i < g_huffNodeCount; ++i) {
    if (!hasParent[i]) {
      g_huffRoot = (int16_t)i;
      break;
    }
  }

  return g_huffRoot >= 0 && assignHuffmanCodes(g_huffRoot, 0, 0);
}

static bool huffmanEncode(const uint8_t *input, uint16_t inputLen,
                          uint8_t *output, uint16_t outputCapacity,
                          uint16_t &outputBits) {
  if (outputCapacity == 0 || inputLen == 0) return false;
  memset(output, 0, outputCapacity);
  outputBits = 0;

  for (uint16_t i = 0; i < inputLen; ++i) {
    const uint8_t symbol = input[i];
    const uint8_t length = g_huffCodeLen[symbol];
    if (length == 0 || length > MAX_CODE_BITS) return false;
    if ((uint32_t)outputBits + length > (uint32_t)outputCapacity * 8U) return false;

    for (int8_t bit = (int8_t)length - 1; bit >= 0; --bit) {
      const bool value = ((g_huffCode[symbol] >> bit) & 1U) != 0;
      setPackedBit(output, outputBits, value);
      ++outputBits;
    }
  }
  return outputBits > 0;
}

static bool huffmanDecode(const uint8_t *input, uint16_t inputBits,
                          uint8_t *output, uint16_t expectedOutputLen,
                          uint16_t outputCapacity, uint16_t &producedLen) {
  producedLen = 0;
  if (g_huffRoot < 0 || inputBits == 0 || expectedOutputLen > outputCapacity) return false;
  int16_t nodeIndex = g_huffRoot;

  for (uint16_t i = 0; i < inputBits; ++i) {
    nodeIndex = getPackedBit(input, i) ? g_huffNodes[nodeIndex].right : g_huffNodes[nodeIndex].left;
    if (nodeIndex < 0) return false;

    const HuffNode &node = g_huffNodes[nodeIndex];
    if (node.symbol >= 0) {
      if (producedLen >= outputCapacity || producedLen >= expectedOutputLen) return false;
      output[producedLen++] = (uint8_t)node.symbol;
      nodeIndex = g_huffRoot;
      if (producedLen == expectedOutputLen && i != inputBits - 1) return false;
    }
  }
  return producedLen == expectedOutputLen && nodeIndex == g_huffRoot;
}

static uint8_t hammingEncodeNibble(uint8_t nibble) {
  const uint8_t d1 = (nibble >> 3) & 1U;
  const uint8_t d2 = (nibble >> 2) & 1U;
  const uint8_t d3 = (nibble >> 1) & 1U;
  const uint8_t d4 = nibble & 1U;
  const uint8_t p1 = d1 ^ d2 ^ d4;
  const uint8_t p2 = d1 ^ d3 ^ d4;
  const uint8_t p4 = d2 ^ d3 ^ d4;

  // Order is positions 1..7: p1, p2, d1, p4, d2, d3, d4.
  return (uint8_t)((p1 << 6) | (p2 << 5) | (d1 << 4) | (p4 << 3) |
                   (d2 << 2) | (d3 << 1) | d4);
}

static bool hammingEncode(const uint8_t *input, uint16_t inputBits,
                          uint8_t *output, uint16_t outputCapacity,
                          uint16_t &outputBits) {
  if (inputBits == 0) return false;
  const uint16_t codewords = (uint16_t)((inputBits + 3U) / 4U);
  outputBits = (uint16_t)(codewords * 7U);
  const uint16_t neededBytes = (uint16_t)((outputBits + 7U) / 8U);
  if (neededBytes > outputCapacity) return false;
  memset(output, 0, outputCapacity);

  for (uint16_t cw = 0; cw < codewords; ++cw) {
    uint8_t nibble = 0;
    for (uint8_t j = 0; j < 4; ++j) {
      const uint16_t sourceBit = (uint16_t)(cw * 4U + j);
      const bool bit = sourceBit < inputBits ? getPackedBit(input, sourceBit) : false;
      nibble = (uint8_t)((nibble << 1) | (bit ? 1U : 0U));
    }
    const uint8_t word = hammingEncodeNibble(nibble);
    for (uint8_t j = 0; j < 7; ++j) {
      const bool bit = ((word >> (6 - j)) & 1U) != 0;
      setPackedBit(output, (uint32_t)cw * 7U + j, bit);
    }
  }
  return true;
}

static bool hammingDecode(const uint8_t *input, uint16_t inputBits,
                          uint16_t expectedHuffmanBits,
                          uint8_t *output, uint16_t outputCapacity,
                          uint16_t &correctedCodewords) {
  correctedCodewords = 0;
  if (inputBits == 0 || inputBits % 7U != 0) return false;
  const uint16_t expectedChannelBits = (uint16_t)(((expectedHuffmanBits + 3U) / 4U) * 7U);
  if (inputBits != expectedChannelBits) return false;
  if (((expectedHuffmanBits + 7U) / 8U) > outputCapacity) return false;
  memset(output, 0, outputCapacity);

  const uint16_t codewords = (uint16_t)(inputBits / 7U);
  for (uint16_t cw = 0; cw < codewords; ++cw) {
    uint8_t b[7];
    for (uint8_t j = 0; j < 7; ++j) b[j] = getPackedBit(input, (uint32_t)cw * 7U + j) ? 1U : 0U;

    const uint8_t s1 = b[0] ^ b[2] ^ b[4] ^ b[6];
    const uint8_t s2 = b[1] ^ b[2] ^ b[5] ^ b[6];
    const uint8_t s4 = b[3] ^ b[4] ^ b[5] ^ b[6];
    const uint8_t syndrome = (uint8_t)(s1 | (s2 << 1) | (s4 << 2));
    if (syndrome != 0 && syndrome <= 7) {
      b[syndrome - 1] ^= 1U;
      ++correctedCodewords;
    }

    const uint8_t nibble = (uint8_t)((b[2] << 3) | (b[4] << 2) | (b[5] << 1) | b[6]);
    for (uint8_t j = 0; j < 4; ++j) {
      const uint16_t outBitIndex = (uint16_t)(cw * 4U + j);
      if (outBitIndex < expectedHuffmanBits) {
        const bool bit = ((nibble >> (3 - j)) & 1U) != 0;
        setPackedBit(output, outBitIndex, bit);
      }
    }
  }
  return true;
}

static void makeFrameHeader(uint8_t *header, uint16_t seq, uint16_t originalLen,
                            uint16_t huffmanBits, uint16_t channelBits,
                            uint16_t originalCrc) {
  memset(header, 0, HEADER_SIZE);
  header[0] = FRAME_MAGIC_0;
  header[1] = FRAME_MAGIC_1;
  header[2] = PROTOCOL_VERSION;
  writeU16LE(&header[3], seq);
  writeU16LE(&header[5], originalLen);
  writeU16LE(&header[7], huffmanBits);
  writeU16LE(&header[9], channelBits);
  writeU16LE(&header[11], originalCrc);
}

// Reads one frame from a UART byte stream. The channel model only changes
// payload bits, so the header must remain intact in this project.
static bool readFrame(Stream &port, Frame &frame, uint32_t scanTimeoutMs = 250) {
  uint8_t syncState = 0;
  const uint32_t scanStart = millis();
  bool found = false;

  while ((uint32_t)(millis() - scanStart) < scanTimeoutMs) {
    if (port.available() <= 0) {
      delay(1);
      continue;
    }
    const int raw = port.read();
    if (raw < 0) continue;
    const uint8_t value = (uint8_t)raw;

    if (syncState == 0) {
      if (value == FRAME_MAGIC_0) syncState = 1;
    } else {
      if (value == FRAME_MAGIC_1) {
        frame.header[0] = FRAME_MAGIC_0;
        frame.header[1] = FRAME_MAGIC_1;
        found = true;
        break;
      }
      syncState = (value == FRAME_MAGIC_0) ? 1 : 0;
    }
  }

  if (!found) return false;
  const size_t remainingHeader = HEADER_SIZE - 2;
  if (port.readBytes((char *)&frame.header[2], remainingHeader) != remainingHeader) return false;
  if (frame.header[2] != PROTOCOL_VERSION) return false;

  frame.seq = readU16LE(&frame.header[3]);
  frame.originalLen = readU16LE(&frame.header[5]);
  frame.huffmanBits = readU16LE(&frame.header[7]);
  frame.channelBits = readU16LE(&frame.header[9]);
  frame.crcOriginal = readU16LE(&frame.header[11]);

  if (frame.originalLen == 0 || frame.originalLen > MAX_TEXT_BYTES) return false;
  if (frame.huffmanBits == 0 || frame.huffmanBits > MAX_COMPRESSED_BYTES * 8U) return false;
  const uint16_t expectedChannelBits = (uint16_t)(((frame.huffmanBits + 3U) / 4U) * 7U);
  if (frame.channelBits != expectedChannelBits) return false;

  frame.payloadBytes = (uint16_t)((frame.channelBits + 7U) / 8U);
  if (frame.payloadBytes == 0 || frame.payloadBytes > MAX_PAYLOAD_BYTES) return false;
  if (port.readBytes((char *)frame.payload, frame.payloadBytes) != frame.payloadBytes) return false;
  return true;
}

static void printHexBytes(Print &out, const uint8_t *data, uint16_t length) {
  for (uint16_t i = 0; i < length; ++i) {
    if (i) out.print(' ');
    if (data[i] < 0x10) out.print('0');
    out.print(data[i], HEX);
  }
  out.println();
}

static void printPackedBits(Print &out, const uint8_t *data, uint16_t bitLength) {
  for (uint16_t i = 0; i < bitLength; ++i) {
    out.print(getPackedBit(data, i) ? '1' : '0');
    if (((i + 1) % 8U) == 0 && i + 1 < bitLength) out.print(' ');
  }
  out.println();
}
