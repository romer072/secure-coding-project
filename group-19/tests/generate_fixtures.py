#!/usr/bin/env python3

"""
Generate BUN fixtures used by the Check test suite.

The project brief asks that submitted code archives do not include sample BUN
files. Keeping fixture generation in source form lets `make test` remain
reproducible without storing `.bun` artifacts in version control.
"""

from pathlib import Path
import binascii
import struct

BUN_MAGIC = 0x304E5542
BUN_VERSION_MAJOR = 1
BUN_VERSION_MINOR = 0

BUN_COMPRESSION_NONE = 0
BUN_COMPRESSION_RLE = 1
BUN_COMPRESSION_ZLIB = 2

HEADER_SIZE = 60
RECORD_SIZE = 48

HEADER_FMT = "<IHHIQQQQQQ"
RECORD_FMT = "<IIQQQIIII"

ROOT = Path(__file__).resolve().parent / "samples"


def align4(value):
    return (value + 3) & ~3


def crc32(data):
    return binascii.crc32(data) & 0xFFFFFFFF


def rle_expand(data):
    out = bytearray()
    for idx in range(0, len(data), 2):
        out.extend(bytes([data[idx + 1]]) * data[idx])
    return bytes(out)


def write_bun(rel_path,
              *,
              header,
              records=(),
              string_table=b"",
              data_section=b"",
              total_size=None):
    path = ROOT / rel_path
    path.parent.mkdir(parents=True, exist_ok=True)

    header_values = dict(header)
    asset_table_offset = header_values["asset_table_offset"]
    string_table_offset = header_values["string_table_offset"]
    data_section_offset = header_values["data_section_offset"]

    min_size = HEADER_SIZE
    if records:
        min_size = max(min_size, asset_table_offset + len(records) * RECORD_SIZE)
    if string_table:
        min_size = max(min_size, string_table_offset + len(string_table))
    if data_section:
        min_size = max(min_size, data_section_offset + len(data_section))
    if total_size is None:
        total_size = min_size

    blob = bytearray(total_size)
    struct.pack_into(
        HEADER_FMT,
        blob,
        0,
        header_values.get("magic", BUN_MAGIC),
        header_values.get("version_major", BUN_VERSION_MAJOR),
        header_values.get("version_minor", BUN_VERSION_MINOR),
        header_values["asset_count"],
        asset_table_offset,
        string_table_offset,
        header_values["string_table_size"],
        data_section_offset,
        header_values["data_section_size"],
        header_values.get("reserved", 0),
    )

    for idx, record in enumerate(records):
        struct.pack_into(
            RECORD_FMT,
            blob,
            asset_table_offset + idx * RECORD_SIZE,
            record.get("name_offset", 0),
            record.get("name_length", 0),
            record.get("data_offset", 0),
            record.get("data_size", 0),
            record.get("uncompressed_size", 0),
            record.get("compression", BUN_COMPRESSION_NONE),
            record.get("asset_type", 0),
            record.get("checksum", 0),
            record.get("flags", 0),
        )

    blob[string_table_offset:string_table_offset + len(string_table)] = string_table
    blob[data_section_offset:data_section_offset + len(data_section)] = data_section
    path.write_bytes(blob)


def one_asset(rel_path,
              *,
              name,
              data,
              record_data_size=None,
              uncompressed_size=0,
              compression=BUN_COMPRESSION_NONE,
              asset_type=1,
              checksum=0,
              flags=0,
              name_offset=0,
              name_length=None,
              data_offset=0,
              data_section_size=None,
              string_table_size=None,
              magic=BUN_MAGIC,
              version_major=BUN_VERSION_MAJOR,
              version_minor=BUN_VERSION_MINOR):
    name_bytes = name
    if name_length is None:
        name_length = len(name_bytes)
    if record_data_size is None:
        record_data_size = len(data)
    if string_table_size is None:
        string_table_size = align4(len(name_bytes))
    if data_section_size is None:
        data_section_size = align4(len(data))

    asset_table_offset = HEADER_SIZE
    string_table_offset = asset_table_offset + RECORD_SIZE
    data_section_offset = align4(string_table_offset + string_table_size)

    write_bun(
        rel_path,
        header={
            "magic": magic,
            "version_major": version_major,
            "version_minor": version_minor,
            "asset_count": 1,
            "asset_table_offset": asset_table_offset,
            "string_table_offset": string_table_offset,
            "string_table_size": string_table_size,
            "data_section_offset": data_section_offset,
            "data_section_size": data_section_size,
        },
        records=[
            {
                "name_offset": name_offset,
                "name_length": name_length,
                "data_offset": data_offset,
                "data_size": record_data_size,
                "uncompressed_size": uncompressed_size,
                "compression": compression,
                "asset_type": asset_type,
                "checksum": checksum,
                "flags": flags,
            }
        ],
        string_table=name_bytes.ljust(string_table_size, b"\0"),
        data_section=data.ljust(data_section_size, b"\0"),
    )


def empty(rel_path, *, magic=BUN_MAGIC, version_major=1, asset_table_offset=64):
    write_bun(
        rel_path,
        header={
            "magic": magic,
            "version_major": version_major,
            "version_minor": 0,
            "asset_count": 0,
            "asset_table_offset": asset_table_offset,
            "string_table_offset": 72,
            "string_table_size": 0,
            "data_section_offset": 80,
            "data_section_size": 0,
        },
        total_size=80 if magic != BUN_MAGIC else 60,
    )


def generate_valid():
    empty("valid/01-empty.bun")
    empty("valid/02-alt-empty.bun")

    one_asset("valid/03-one-asset.bun", name=b"hello", data=b"world")
    one_asset(
        "valid/04-binary-asset.bun",
        name=b"image",
        data=bytes.fromhex(
            "89504e470d0a1a0a0000000d49484452000102030405060708090a0b0c0d0e0f"
        ),
        data_section_size=32,
    )

    names = b"one\0two\0"
    write_bun(
        "valid/05-multi-assets-slack.bun",
        header={
            "asset_count": 2,
            "asset_table_offset": HEADER_SIZE,
            "string_table_offset": HEADER_SIZE + 2 * RECORD_SIZE,
            "string_table_size": 8,
            "data_section_offset": HEADER_SIZE + 2 * RECORD_SIZE + 8,
            "data_section_size": 8,
        },
        records=[
            {"name_offset": 0, "name_length": 3, "data_offset": 0, "data_size": 1, "asset_type": 1},
            {"name_offset": 4, "name_length": 3, "data_offset": 4, "data_size": 1, "asset_type": 1},
        ],
        string_table=names,
        data_section=b"A\0\0\0B\0\0\0",
    )

    one_asset(
        "valid/06-rle-valid.bun",
        name=b"test",
        data=bytes([10, ord("A"), 5, ord("B")]),
        uncompressed_size=15,
        compression=BUN_COMPRESSION_RLE,
    )

    raw_crc_payload = b"Hello, BUN world!\n"
    one_asset(
        "valid/07-raw-crc.bun",
        name=b"hello",
        data=raw_crc_payload,
        record_data_size=len(raw_crc_payload),
        checksum=crc32(raw_crc_payload),
        asset_type=0,
    )

    rle_crc_payload = bytes([5, ord("A"), 3, ord("B"), 2, ord("C")])
    one_asset(
        "valid/08-rle-crc.bun",
        name=b"rle_test",
        data=rle_crc_payload,
        record_data_size=len(rle_crc_payload),
        uncompressed_size=len(rle_expand(rle_crc_payload)),
        compression=BUN_COMPRESSION_RLE,
        checksum=crc32(rle_expand(rle_crc_payload)),
        asset_type=0,
    )

    big_rle = bytes([250, ord("A"), 250, ord("B"), 250, ord("C"), 250, ord("D"), 250, ord("E")]) * 400
    one_asset(
        "valid/09-big-rle-crc.bun",
        name=b"big_streaming_asset",
        data=big_rle,
        record_data_size=len(big_rle),
        uncompressed_size=len(rle_expand(big_rle)),
        compression=BUN_COMPRESSION_RLE,
        checksum=crc32(rle_expand(big_rle)),
        asset_type=0,
    )


def generate_invalid():
    empty("invalid/01-bad-magic.bun", magic=BUN_MAGIC - 1)
    empty("invalid/02-bad-version.bun", version_major=2)
    empty("invalid/03-bad-offset-alignment.bun", asset_table_offset=65)

    write_bun(
        "invalid/04-section-past-eof.bun",
        header={
            "asset_count": 0,
            "asset_table_offset": 64,
            "string_table_offset": 72,
            "string_table_size": 0,
            "data_section_offset": 64,
            "data_section_size": 1000,
        },
        total_size=60,
    )

    write_bun(
        "invalid/05-overlapping-sections.bun",
        header={
            "asset_count": 2,
            "asset_table_offset": 64,
            "string_table_offset": 100,
            "string_table_size": 20,
            "data_section_offset": 80,
            "data_section_size": 0,
        },
        records=[{}, {}],
        string_table=b"\0" * 20,
        total_size=260,
    )

    one_asset("invalid/06-asset-name-past-string-table.bun", name=b"test", data=b"data", name_length=100)
    one_asset("invalid/07-asset-name-nonprintable.bun", name=b"te\x01st", data=b"data")
    (ROOT / "invalid/08-truncated-file.bun").write_bytes(b"BUN0\x01\x00\x00\x00xx")
    one_asset(
        "invalid/09-misaligned-section-size.bun",
        name=b"ok.txt",
        data=b"ABC",
        record_data_size=3,
        data_section_size=5,
    )

    write_bun(
        "invalid/10-overlapping-with-nonprintable.bun",
        header={
            "asset_count": 1,
            "asset_table_offset": 60,
            "string_table_offset": 104,
            "string_table_size": 8,
            "data_section_offset": 116,
            "data_section_size": 4,
        },
        records=[{"name_offset": 0, "name_length": 6, "data_offset": 0, "data_size": 3, "asset_type": 1}],
        string_table=b"\0\0\0\0ok.t",
        data_section=b"ABC\0",
        total_size=120,
    )

    write_bun(
        "invalid/11-second-asset-empty-name.bun",
        header={
            "asset_count": 2,
            "asset_table_offset": 60,
            "string_table_offset": 156,
            "string_table_size": 8,
            "data_section_offset": 164,
            "data_section_size": 4,
        },
        records=[
            {"name_offset": 0, "name_length": 6, "data_offset": 0, "data_size": 3, "asset_type": 1},
            {"name_offset": 0, "name_length": 0, "data_offset": 0, "data_size": 0},
        ],
        string_table=b"ok.txt\0\0",
        data_section=b"ABC\0",
    )

    one_asset("invalid/12-asset-name-oob.bun", name=b"ok.txt", data=b"ABC", name_length=20, record_data_size=3)
    one_asset("invalid/13-asset-empty-name.bun", name=b"ok.txt", data=b"ABC", name_length=0, record_data_size=3)
    one_asset(
        "invalid/14-rle-zero-count.bun",
        name=b"test",
        data=bytes([10, ord("A"), 0, ord("B")]),
        uncompressed_size=10,
        compression=BUN_COMPRESSION_RLE,
    )
    one_asset(
        "invalid/15-rle-bomb.bun",
        name=b"test",
        data=bytes([200, 0xFF]),
        data_section_size=2,
        uncompressed_size=100,
        compression=BUN_COMPRESSION_RLE,
    )
    one_asset(
        "invalid/16-rle-truncated.bun",
        name=b"test",
        data=bytes([10, ord("A")]),
        data_section_size=2,
        uncompressed_size=100,
        compression=BUN_COMPRESSION_RLE,
    )

    raw_crc_payload = b"Hello, BUN world!\n"
    one_asset(
        "invalid/17-wrong-crc-raw.bun",
        name=b"hello",
        data=raw_crc_payload,
        record_data_size=len(raw_crc_payload),
        checksum=crc32(raw_crc_payload) ^ 0xFFFFFFFF,
        asset_type=0,
    )

    rle_crc_payload = bytes([5, ord("A"), 3, ord("B"), 2, ord("C")])
    one_asset(
        "invalid/18-wrong-crc-rle.bun",
        name=b"rle_test",
        data=rle_crc_payload,
        record_data_size=len(rle_crc_payload),
        uncompressed_size=len(rle_expand(rle_crc_payload)),
        compression=BUN_COMPRESSION_RLE,
        checksum=crc32(rle_expand(rle_crc_payload)) ^ 0xFFFFFFFF,
        asset_type=0,
    )


def main():
    generate_valid()
    generate_invalid()


if __name__ == "__main__":
    main()
