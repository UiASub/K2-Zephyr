#!/usr/bin/env python3
"""
Continuous UDP blast client for STM32 K2-Zephyr application.
Sends structured packets with proper CRC32 validation until Ctrl+C.
"""

import socket
import struct
import binascii
import time

# Configuration
TARGET_IP = "192.168.1.100"
TARGET_PORT = 12345
STATS_INTERVAL_S = 1.0


# CRC32 calculation (IEEE 802.3 polynomial)
def calculate_crc32(data):
    """Calculate CRC32 using IEEE 802.3 polynomial"""
    crc = 0xFFFFFFFF
    polynomial = 0xEDB88320

    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ polynomial
            else:
                crc = crc >> 1

    return (~crc) & 0xFFFFFFFF


def create_packet(sequence, payload, corrupt_crc=False, verbose=False):
    """Create a structured packet with CRC32"""
    # Pack sequence (uint32) and payload (uint64) in network byte order (big-endian)
    seq_bytes = struct.pack('>I', sequence)
    payload_bytes = struct.pack('>Q', payload)

    # Calculate CRC32 over sequence + payload
    data_for_crc = seq_bytes + payload_bytes
    crc32 = calculate_crc32(data_for_crc)

    # Corrupt CRC32 if requested (for testing error handling)
    if corrupt_crc:
        crc32 = crc32 ^ 0xDEADBEEF
        if verbose:
            print("  CRC32 intentionally corrupted for testing")

    crc_bytes = struct.pack('>I', crc32)

    # Complete packet
    packet = seq_bytes + payload_bytes + crc_bytes

    if verbose:
        print("  Packet created:")
        print(f"    Sequence: {sequence}")
        print(f"    Payload: 0x{payload:016X}")
        print(f"    CRC32: 0x{crc32:08X}")
        print(f"    Size: {len(packet)} bytes")
        print(f"    Raw bytes: {binascii.hexlify(packet).decode().upper()}")

    return packet


def main():
    print("=== UDP Blast Client for STM32 K2-Zephyr ===")
    print(f"Target: {TARGET_IP}:{TARGET_PORT}")
    print("Expected packet structure: [uint32 seq][uint64 payload][uint32 crc32] = 16 bytes")
    print("Mode: continuous blast until manually stopped (Ctrl+C)")
    print()

    sock = None
    try:
        # Create UDP socket
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.settimeout(5.0)

        print(f"Connected to {TARGET_IP}:{TARGET_PORT}")

        # Send one verbose packet so format is visible at startup
        first_payload = time.perf_counter_ns() & 0xFFFFFFFFFFFFFFFF
        first_packet = create_packet(1, first_payload, corrupt_crc=False, verbose=True)
        sock.sendto(first_packet, (TARGET_IP, TARGET_PORT))
        print("Blasting packets... press Ctrl+C to stop.")

        seq = 2
        sent_total = 1
        sent_interval = 0
        t_last = time.perf_counter()

        while True:
            payload = time.perf_counter_ns() & 0xFFFFFFFFFFFFFFFF
            packet = create_packet(seq, payload, corrupt_crc=False, verbose=False)
            sock.sendto(packet, (TARGET_IP, TARGET_PORT))

            seq = (seq + 1) & 0xFFFFFFFF
            if seq == 0:
                seq = 1

            sent_total += 1
            sent_interval += 1

            now = time.perf_counter()
            elapsed = now - t_last
            if elapsed >= STATS_INTERVAL_S:
                pps = sent_interval / elapsed
                print(f"sent_total={sent_total:,}  seq={seq}  rate={pps:,.0f} pkt/s")
                sent_interval = 0
                t_last = now

    except KeyboardInterrupt:
        print("\nStopped by user (Ctrl+C).")
    except socket.error as e:
        print(f"Socket error: {e}")
    except Exception as e:
        print(f"Error: {e}")
    finally:
        if sock is not None:
            try:
                sock.close()
                print("UDP socket closed.")
            except Exception:
                pass

    print("Exiting.")


if __name__ == "__main__":
    main()
