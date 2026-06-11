#!/usr/bin/env python3
"""Send a short K2 pilot command for quick thruster/VESC testing."""

import argparse
import socket
import struct
import time


DEFAULT_IP = "10.77.0.2"
DEFAULT_PORT = 12345


def crc32_ieee(data: bytes) -> int:
    crc = 0xFFFFFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0xEDB88320
            else:
                crc >>= 1
    return (~crc) & 0xFFFFFFFF


def clamp_i8(value: int) -> int:
    return max(-128, min(127, value))


def make_payload(args: argparse.Namespace) -> int:
    fields = [
        clamp_i8(args.surge) + 128,
        clamp_i8(args.sway) + 128,
        clamp_i8(args.heave) + 128,
        clamp_i8(args.roll) + 128,
        clamp_i8(args.pitch) + 128,
        clamp_i8(args.yaw) + 128,
        max(0, min(255, args.light)),
        max(0, min(255, args.manipulator)),
    ]

    payload = 0
    for shift, value in enumerate(fields):
        payload |= value << (8 * shift)

    return payload


def make_packet(sequence: int, payload: int) -> bytes:
    body = struct.pack(">IQ", sequence & 0xFFFFFFFF, payload & 0xFFFFFFFFFFFFFFFF)
    crc = crc32_ieee(body)
    return body + struct.pack(">I", crc)


def send_command(sock: socket.socket, target: tuple[str, int], sequence: int, payload: int) -> None:
    sock.sendto(make_packet(sequence, payload), target)


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Quickly nudge K2 thrusters through the main UDP command path."
    )
    parser.add_argument("--ip", default=DEFAULT_IP, help=f"ROV IP, default {DEFAULT_IP}")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT, help=f"UDP command port, default {DEFAULT_PORT}")
    parser.add_argument("--duration", type=float, default=1.0, help="Active command duration in seconds")
    parser.add_argument("--rate", type=float, default=20.0, help="Command send rate in Hz")
    parser.add_argument("--surge", type=int, default=51, help="Pilot surge value, -128..127")
    parser.add_argument("--sway", type=int, default=0, help="Pilot sway value, -128..127")
    parser.add_argument("--heave", type=int, default=0, help="Pilot heave value, -128..127")
    parser.add_argument("--roll", type=int, default=0, help="Pilot roll value, -128..127")
    parser.add_argument("--pitch", type=int, default=0, help="Pilot pitch value, -128..127")
    parser.add_argument("--yaw", type=int, default=0, help="Pilot yaw value, -128..127")
    parser.add_argument("--light", type=int, default=0, help="Light byte, 0..255")
    parser.add_argument("--manipulator", type=int, default=0, help="Manipulator byte, 0..255")
    args = parser.parse_args()

    interval = 1.0 / max(args.rate, 1.0)
    active_payload = make_payload(args)

    zero_args = argparse.Namespace(
        surge=0, sway=0, heave=0, roll=0, pitch=0, yaw=0, light=0, manipulator=0
    )
    zero_payload = make_payload(zero_args)

    target = (args.ip, args.port)
    sequence = 1

    print(f"Target: {args.ip}:{args.port}")
    print(
        "Command: "
        f"surge={args.surge} sway={args.sway} heave={args.heave} "
        f"roll={args.roll} pitch={args.pitch} yaw={args.yaw}"
    )
    print("Default surge=51 gives about +0.20 duty on the local UART thruster.")
    print("Press Ctrl+C to stop; zeros are sent on exit.")

    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        try:
            for _ in range(5):
                send_command(sock, target, sequence, zero_payload)
                sequence += 1
                time.sleep(interval)

            deadline = time.monotonic() + max(0.0, args.duration)
            while time.monotonic() < deadline:
                send_command(sock, target, sequence, active_payload)
                sequence += 1
                time.sleep(interval)
        finally:
            for _ in range(20):
                send_command(sock, target, sequence, zero_payload)
                sequence += 1
                time.sleep(0.02)

    print("Done. Stop commands sent.")


if __name__ == "__main__":
    main()
