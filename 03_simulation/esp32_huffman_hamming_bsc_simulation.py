#!/usr/bin/env python3
"""Reproducible software simulation of the project's Huffman + Hamming(7,4) + BSC chain.

This models the algorithms/frame payload in common.h. It does NOT emulate UART electrical
behavior or run on actual ESP32 hardware. Python standard library only.
"""
from collections import Counter
import random

CORPUS = (
    "ESP32 UART TEST MESSAGE 001 "
    "HUFFMAN SOURCE CODING TEST "
    "HAMMING 7,4 WITH BSC NOISE "
    "BER CORRECTION AND DATA FRAME"
).encode("ascii")
MESSAGES = [
    "ESP32 UART TEST MESSAGE 001",
    "HUFFMAN SOURCE CODING TEST",
    "HAMMING 7,4 WITH BSC NOISE",
    "BER CORRECTION AND DATA FRAME",
]
HEADER_BYTES = 13


def build_huffman(corpus):
    freq = Counter(corpus)
    nodes = []
    for sym in range(256):
        if freq[sym]:
            nodes.append({"weight": freq[sym], "symbol": sym, "left": -1,
                          "right": -1, "min_symbol": sym})
    parent = [False] * 512

    def less(a, b):
        A, B = nodes[a], nodes[b]
        if A["weight"] != B["weight"]:
            return A["weight"] < B["weight"]
        if A["min_symbol"] != B["min_symbol"]:
            return A["min_symbol"] < B["min_symbol"]
        return a < b

    roots_left = len(nodes)
    while roots_left > 1:
        first = second = -1
        for i in range(len(nodes)):
            if parent[i]:
                continue
            if first < 0 or less(i, first):
                second, first = first, i
            elif second < 0 or less(i, second):
                second = i
        if first < 0 or second < 0:
            raise RuntimeError("Huffman tree could not be built")
        a, b = nodes[first], nodes[second]
        nodes.append({"weight": a["weight"] + b["weight"], "symbol": -1,
                      "left": first, "right": second,
                      "min_symbol": min(a["min_symbol"], b["min_symbol"])})
        parent[first] = parent[second] = True
        parent[len(nodes) - 1] = False
        roots_left -= 1
    root = next(i for i, p in enumerate(parent[:len(nodes)]) if not p)
    codes = {}

    def walk(idx, code, depth):
        node = nodes[idx]
        if node["symbol"] >= 0:
            codes[node["symbol"]] = (code if depth else 0, depth if depth else 1)
            return
        walk(node["left"], code << 1, depth + 1)
        walk(node["right"], (code << 1) | 1, depth + 1)
    walk(root, 0, 0)
    return nodes, root, codes


NODES, ROOT, CODES = build_huffman(CORPUS)


def huffman_encode(data):
    out = []
    for symbol in data:
        code, length = CODES[symbol]
        out.extend((code >> bit) & 1 for bit in range(length - 1, -1, -1))
    return out


def huffman_decode(bits, expected_len):
    out, node_idx = bytearray(), ROOT
    for i, bit in enumerate(bits):
        node_idx = NODES[node_idx]["right"] if bit else NODES[node_idx]["left"]
        if node_idx < 0:
            return None
        node = NODES[node_idx]
        if node["symbol"] >= 0:
            out.append(node["symbol"])
            node_idx = ROOT
            if len(out) > expected_len or (len(out) == expected_len and i != len(bits) - 1):
                return None
    if len(out) != expected_len or node_idx != ROOT:
        return None
    return bytes(out)


def hamming_encode(bits):
    out = []
    for start in range(0, len(bits), 4):
        nibble = bits[start:start + 4] + [0] * max(0, start + 4 - len(bits))
        d1, d2, d3, d4 = nibble
        p1, p2, p4 = d1 ^ d2 ^ d4, d1 ^ d3 ^ d4, d2 ^ d3 ^ d4
        out.extend([p1, p2, d1, p4, d2, d3, d4])
    return out


def hamming_decode(bits, expected_huffman_bits):
    if not bits or len(bits) % 7 or len(bits) != ((expected_huffman_bits + 3) // 4) * 7:
        return None, 0
    out, syndrome_nonzero = [], 0
    for start in range(0, len(bits), 7):
        b = bits[start:start + 7].copy()
        s1 = b[0] ^ b[2] ^ b[4] ^ b[6]
        s2 = b[1] ^ b[2] ^ b[5] ^ b[6]
        s4 = b[3] ^ b[4] ^ b[5] ^ b[6]
        syndrome = s1 | (s2 << 1) | (s4 << 2)
        if syndrome:
            b[syndrome - 1] ^= 1
            syndrome_nonzero += 1
        out.extend([b[2], b[4], b[5], b[6]])
    return out[:expected_huffman_bits], syndrome_nonzero


def crc16_ccitt(data):
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def pack_bits(bits):
    out = bytearray((len(bits) + 7) // 8)
    for i, bit in enumerate(bits):
        if bit:
            out[i // 8] |= 1 << (7 - (i % 8))
    return bytes(out)


def make_test_frame(message):
    raw = message.encode("ascii")
    huff_bits = huffman_encode(raw)
    ham_bits = hamming_encode(huff_bits)
    payload = pack_bits(ham_bits)
    return {"message": message, "raw": raw, "huff": huff_bits, "ham": ham_bits,
            "payload": payload, "crc": crc16_ccitt(raw),
            "raw_bits": len(raw) * 8, "huff_bits": len(huff_bits),
            "ham_bits": len(ham_bits), "payload_bytes": len(payload),
            "frame_bytes": HEADER_BYTES + len(payload)}


def receive(frame, received_hamming_bits):
    huff_recovered, syndrome_count = hamming_decode(received_hamming_bits, frame["huff_bits"])
    decoded = huffman_decode(huff_recovered, len(frame["raw"])) if huff_recovered is not None else None
    exact = decoded == frame["raw"]
    crc_ok = decoded is not None and len(decoded) == len(frame["raw"]) and crc16_ccitt(decoded) == frame["crc"]
    return decoded, syndrome_count, crc_ok, exact


def systematic_hamming_test():
    cases = 0
    for nibble in range(16):
        data = [(nibble >> shift) & 1 for shift in (3, 2, 1, 0)]
        word = hamming_encode(data)
        decoded, syndrome_count = hamming_decode(word, 4)
        assert decoded == data and syndrome_count == 0
        cases += 1
        for bit_pos in range(7):
            corrupted = word.copy()
            corrupted[bit_pos] ^= 1
            decoded, syndrome_count = hamming_decode(corrupted, 4)
            assert decoded == data and syndrome_count == 1, (nibble, bit_pos)
            cases += 1
    return cases


def print_single_run(p=0.02, seed=12345):
    rng = random.Random(seed)
    print(f"\nDETERMINISTIC PACKET TRACE: p={p:.3f}, seed={seed}")
    print("SEQ | Original text | raw/huff/ham bits | flips | syndrome flags | exact recovered | CRC")
    for seq, message in enumerate(MESSAGES, 1):
        frame = make_test_frame(message)
        rx = frame["ham"].copy()
        positions = []
        for i in range(len(rx)):
            if rng.random() < p:
                rx[i] ^= 1
                positions.append(i)
        decoded, syndromes, crc_ok, exact = receive(frame, rx)
        print(f"{seq:3d} | {message} | {frame['raw_bits']}/{frame['huff_bits']}/{frame['ham_bits']} | "
              f"{len(positions)} | {syndromes} | {str(exact):5s} | {('PASS' if crc_ok else 'FAIL')}")
        if seq == 4:
            print("  payload before BSC:", frame["payload"].hex(" ").upper())
            print("  payload after  BSC:", pack_bits(rx).hex(" ").upper())
            print("  flipped bit indexes (0-based, payload-only):", positions)
            print("  receiver decoded:", repr(decoded.decode('ascii', errors='replace') if decoded is not None else None))


def monte_carlo(p, packets=10000, seed=42000):
    rng = random.Random(seed)
    good = flips = total_bits = syndrome_total = crc_pass = decode_invalid = 0
    for idx in range(packets):
        frame = make_test_frame(MESSAGES[idx % len(MESSAGES)])
        rx = frame["ham"].copy()
        for i in range(len(rx)):
            if rng.random() < p:
                rx[i] ^= 1
                flips += 1
        total_bits += len(rx)
        decoded, syndromes, crc_ok, exact = receive(frame, rx)
        syndrome_total += syndromes
        crc_pass += bool(crc_ok)
        decode_invalid += decoded is None
        good += bool(exact and crc_ok)
    return {"p": p, "packets": packets, "success": good, "fail": packets - good,
            "success_pct": 100 * good / packets, "fail_pct": 100 * (packets - good) / packets,
            "observed_ber": flips / total_bits, "flips": flips, "tested_bits": total_bits,
            "avg_syndrome_codewords_per_frame": syndrome_total / packets,
            "crc_pass_count": crc_pass, "decode_invalid": decode_invalid}


def main():
    print("ESP32 HUFFMAN + HAMMING(7,4) + BSC SOFTWARE SIMULATION")
    print("Huffman codebook derived from the shared TRAINING_CORPUS in common.h")
    print("UART wires are assumed error-free; BSC affects only Hamming payload bits.\n")
    hamming_cases = systematic_hamming_test()
    print(f"Hamming unit test: PASS {hamming_cases}/{hamming_cases} cases (16 nibbles x no-error + 7 single-bit positions).")
    print("\nPER-MESSAGE ENCODING AND CLEAN-CHANNEL RESULTS")
    print("Message | Raw bits | Huff bits | Hamming bits | Payload bytes | Frame bytes | CR_Huff | CR_Frame | Clean decode")
    for message in MESSAGES:
        f = make_test_frame(message)
        _, _, crc_ok, exact = receive(f, f["ham"])
        cr_huff = f["raw_bits"] / f["huff_bits"]
        cr_frame = f["raw_bits"] / (f["frame_bytes"] * 8)
        print(f"{message} | {f['raw_bits']} | {f['huff_bits']} | {f['ham_bits']} | {f['payload_bytes']} | "
              f"{f['frame_bytes']} | {cr_huff:.3f} | {cr_frame:.3f} | {'PASS' if exact and crc_ok else 'FAIL'}")

    print_single_run(p=0.02, seed=12345)
    print("\nMONTE CARLO PACKET SUCCESS (10,000 frames per p; fixed seed per p; messages round-robin)")
    print("p | BER observed | CRC PASS count | exact+CRC pass | fail | success % | avg syndrome flags/frame")
    for p in [0.0, 0.001, 0.005, 0.01, 0.02, 0.03, 0.05, 0.10]:
        result = monte_carlo(p, packets=10000, seed=40000 + int(p * 100000))
        print(f"{result['p']:.3f} | {result['observed_ber']:.5f} | {result['crc_pass_count']} | "
              f"{result['success']} | {result['fail']} | {result['success_pct']:.3f}% | "
              f"{result['avg_syndrome_codewords_per_frame']:.3f}")

    total_raw = sum(len(m.encode('ascii')) for m in MESSAGES)
    total_frame = sum(make_test_frame(m)["frame_bytes"] for m in MESSAGES)
    total_huff = sum(len(make_test_frame(m)["huff"]) for m in MESSAGES)
    total_ham = sum(len(make_test_frame(m)["ham"]) for m in MESSAGES)
    print("\nAGGREGATED FOUR-MESSAGE METRICS (one of each test message)")
    print(f"Raw data: {total_raw} bytes ({total_raw * 8} bits)")
    print(f"Huffman: {total_huff} bits; source saving = {100*(1-total_huff/(total_raw*8)):.2f}%")
    print(f"Hamming payload: {total_ham} bits; transmitted payload bytes={sum(make_test_frame(m)['payload_bytes'] for m in MESSAGES)}")
    print(f"Full frame bytes (13-byte header each): {total_frame}; raw/frame ratio={total_raw/total_frame:.3f}; frame expansion={(total_frame/total_raw-1)*100:.2f}%")
    print(f"Raw information bits / UART line bits (8N1 uses 10 line bits/byte): {total_raw*8/(total_frame*10):.3f}")
    print("\nInterpretation: results are algorithmic software simulation, not a measurement from physical ESP32 boards.")

if __name__ == '__main__':
    main()