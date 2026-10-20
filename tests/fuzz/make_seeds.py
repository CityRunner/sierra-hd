#!/usr/bin/env python3
"""Writes the synthetic seed corpora of the sci-resource fuzz targets.

    python3 tests/fuzz/make_seeds.py

The seeds are small hand-made SCI0 structures (no game data): stored, LZW and
Huffman entries, a resource map, and patch files. Run it again after changing
it; the generated files are committed.
"""

import struct
from pathlib import Path

HERE = Path(__file__).resolve().parent


def lzw(codes: list[int]) -> bytes:
    """Packs 9-bit codes least significant bit first."""
    value, bits = 0, 0
    for index, code in enumerate(codes):
        value |= code << (9 * index)
        bits += 9
    return value.to_bytes((bits + 7) // 8, "little")


def msb_bits(fields: list[tuple[int, int]]) -> bytes:
    value, bits = 0, 0
    for field, width in fields:
        value = (value << width) | field
        bits += width
    pad = (8 - bits % 8) % 8
    return (value << pad).to_bytes((bits + pad) // 8, "big")


LZW_ABAB = lzw([ord("A"), ord("B"), 258, 257])
HUFFMAN_XXQ = bytes([2, ord("T"), 0x00, 0x10, ord("X"), 0x00]) + msb_bits(
    [(0, 1), (0, 1), (1, 1), (ord("Q"), 8), (1, 1), (ord("T"), 8)]
)


def entry(type_: int, number: int, method: int, unpacked: int, packed: bytes) -> bytes:
    return struct.pack("<HHHH", (type_ << 11) | number, len(packed) + 4, unpacked, method) + packed


def folder(files: dict[str, bytes]) -> bytes:
    out = b""
    for name, content in files.items():
        out += bytes([len(name)]) + name.encode() + struct.pack("<H", len(content)) + content
    return out


def main() -> None:
    decompress = HERE / "corpus" / "decompress"
    for name, method, unpacked, data in (
        ("stored", 0, 3, b"abc"),
        ("lzw", 1, 4, LZW_ABAB),
        ("huffman", 2, 3, HUFFMAN_XXQ),
    ):
        (decompress / name).write_bytes(bytes([method]) + struct.pack("<HH", len(data), unpacked) + data)

    volume = entry(0, 1, 0, 3, b"abc")
    lzw_offset = len(volume)
    volume += entry(2, 52, 1, 4, LZW_ABAB)
    huffman_offset = len(volume)
    volume += entry(1, 3, 2, 3, HUFFMAN_XXQ)
    resource_map = (
        struct.pack("<HI", (0 << 11) | 1, (1 << 26) | 0)
        + struct.pack("<HI", (2 << 11) | 52, (1 << 26) | lzw_offset)
        + struct.pack("<HI", (1 << 11) | 3, (1 << 26) | huffman_offset)
        + b"\xff" * 6
    )
    game_folder = HERE / "corpus" / "game_folder"
    (game_folder / "volume").write_bytes(folder({"RESOURCE.MAP": resource_map, "RESOURCE.001": volume}))
    (game_folder / "patches").write_bytes(
        folder(
            {
                "RESOURCE.MAP": resource_map,
                "RESOURCE.001": volume,
                "SCRIPT.052": b"\x82\x00patched",
                "3.p56": b"\x81\x00\x01\x02",
            }
        )
    )


if __name__ == "__main__":
    main()
