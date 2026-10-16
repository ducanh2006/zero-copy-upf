#!/usr/bin/env python3
"""gen_sample_pcap.py - generate a small self-contained 5G-style traffic pcap.

Produces data/pcap/sample_5g_traffic.pcap with 256 packets:
  Eth(14) + IPv4(20) + UDP(8) + 800B payload  (~842B on wire, ~210KB file)

These act as "Server Data" (N3/N9 inner payload P): the DPDK app prepends
the 50B 3GPP outer header (Eth/IPv4/UDP-2152/GTP-U) via zero-copy chain.
Pure stdlib (no scapy). Layout mirrors src/outer_header.h conventions.

Usage:
  python3 scripts/gen_sample_pcap.py [--packets 256] [--out data/pcap/sample_5g_traffic.pcap]
  python3 scripts/gen_sample_pcap.py --source host_big   # link 1M-pkt file for servers
"""
import argparse
import os
import struct
import sys

HOST_BIG = ("/mnt/70E63C66E63C2EAA/Users/ADMIN/Desktop/coding/"
            "hpc-spi-classifier/tests/data/pcap/balanced_traffic.pcap")


def put_be16(v):
    return struct.pack(">H", v)


def put_be32(v):
    return struct.pack(">I", v)


def build_packet(seed):
    # Ethernet: DMAC/SMAC + 0x0800
    eth = (bytes([0x02, 0x00, 0x00, 0x00, 0x00, 0x01]) +
           bytes([0x02, 0x00, 0x00, 0x00, 0x00, 0x02]) +
           put_be16(0x0800))
    # IPv4: ver/IHL, total = 20 + 8 + 800
    ip = (b"\x45\x00" + put_be16(828) + put_be16(seed & 0xFFFF) +
          b"\x40\x00\x40\x11\x00\x00" +
          put_be32(0x0A000002) + put_be32(0x0A000003))
    # UDP: sport hash, dport 5000 (inner server traffic), len = 8 + 800
    udp = (put_be16(40000 + (seed % 1024)) + put_be16(5000) +
           put_be16(808) + b"\x00\x00")
    payload = bytes(((i * 31 + seed) & 0xFF) for i in range(800))
    return eth + ip + udp + payload


def write_pcap(path, n):
    with open(path, "wb") as f:
        # Global header, LE, v2.4, snaplen 65535, LINKTYPE_ETHERNET
        f.write(struct.pack("<IHHIIII", 0xA1B2C3D4, 2, 4, 0, 0, 65535, 1))
        for i in range(n):
            pkt = build_packet(i)
            f.write(struct.pack("<IIII", 0, i, len(pkt), len(pkt)))
            f.write(pkt)
    return os.path.getsize(path)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--packets", type=int, default=256)
    ap.add_argument("--out", default="data/pcap/sample_5g_traffic.pcap")
    ap.add_argument("--source", default="gen",
                    help="'gen' (default) or 'host_big'")
    args = ap.parse_args()

    if args.source == "host_big":
        dst = "data/pcap/input_big.pcap"
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        if not os.path.exists(HOST_BIG):
            print(f"host big pcap not found: {HOST_BIG}", file=sys.stderr)
            return 1
        try:
            if os.path.exists(dst):
                os.remove(dst)
            os.symlink(HOST_BIG, dst)
            print(f"linked {dst} -> {HOST_BIG}")
        except OSError:
            import shutil
            shutil.copyfile(HOST_BIG, dst)
            print(f"copied {HOST_BIG} -> {dst}")
        return 0

    os.makedirs(os.path.dirname(args.out) or ".", exist_ok=True)
    size = write_pcap(args.out, args.packets)
    print(f"wrote {args.out}: {args.packets} packets, {size} bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
