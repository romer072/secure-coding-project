# BUN File Parser

A secure-by-design parser for the BUN (Bun Archive) file format, implemented in C with comprehensive validation and security checks.

## Contributors

Group 23

| Name| Student Number | GitHub username |
|------|------|-------------|
| Rohma Rehman | 23845362 | romer072 |
| Ritch Elbert Rayawang| 23940731 | Ritch-Wang |
| Kavya Anil | 24217516 | kvn776 |
| Alan Ling | 23843412 | AlanLingYS |

## Overview

This project implements a parser for the BUN archive format — a binary file format for packaging assets (text, images, data files) with support for optional RLE compression. The parser is designed with security as a primary concern, implementing multiple layers of validation to reject malformed or malicious input files.

## What the Parser Does

The parser validates BUN files through three main stages:

### 1. Header Validation (`bun_parse_header`)

- **Magic number verification**: Ensures file starts with `0x304E5542` ("BUN0")
- **Version checking**: Only accepts version 1.0 files
- **Alignment validation**: All offsets and sizes must be 4-byte aligned
- **Bounds checking**: Verifies all table offsets and sizes stay within file limits
- **Overlap detection**: Ensures asset table, string table, and data section don't overlap

### 2. Asset Record Validation (`bun_parse_assets`)

- **Record bounds**: Each asset record must be within the asset table region
- **Name validation**: Asset names must be non-empty and contain only printable ASCII (32-126)
- **Data bounds**: Asset data offsets must reference valid positions within the data section
- **Compression handling**:
  - Type 0 (none): Pass-through validation
  - Type 1 (RLE): Validates pair structure, rejects zero counts, enforces size limits
  - Type 2 (zlib): Explicitly rejected (unsupported)

### 3. Security Protections

| Protection | Implementation |
|------------|----------------|
| **Integer overflow** | Checked arithmetic via `checked_add_u64()` and `checked_mul_u64()` |
| **Decompression bombs** | Accumulated output tracked; fails if > `uncompressed_size` |
| **Out-of-bounds access** | `range_within_file()` validates all memory ranges |
| **Section overlaps** | `ranges_overlap()` detects conflicting regions |

## BUN File Format

```
┌─────────────────────────────────────┐
│           Header (60 bytes)         │
│  magic: 0x304E5542                  │
│  version: 1.0                       │
│  asset_count                        │
│  asset_table_offset                 │
│  string_table_offset/size           │
│  data_section_offset/size            │
├─────────────────────────────────────┤
│        Asset Table (48 bytes/asset) │
│  name_offset, name_length           │
│  data_offset, data_size             │
│  uncompressed_size                  │
│  compression, type, checksum, flags │
├─────────────────────────────────────┤
│           String Table              │
│  (asset names as null-terminated    │
│   ASCII strings)                    │
├─────────────────────────────────────┤
│          Data Section               │
│  (raw or RLE-compressed asset data) │
└─────────────────────────────────────┘
```

### Compression Types

| Type | Name | Description |
|------|------|-------------|
| 0 | None | Uncompressed data |
| 1 | RLE | Run-Length Encoding (count, byte) pairs |
| 2 | zlib | Not supported |

## Building

```bash
make
```

This produces:
- `bun_parser` — The executable parser
- `bun_parse.o` — Compiled object file

## Running

```bash
./bun_parser <path-to.bun>
```

### Exit Codes

| Code | Meaning |
|------|---------|
| 0 | Valid BUN file |
| 1 | Malformed file |
| 2 | Unsupported features |
| 3 | I/O error |

### Output Examples

**Valid file:**
```
BUN file: <path>
Version: 1.0
Assets: <count>
```

**Invalid file:**
```
Error: <violation message>
...
```

## API Reference

### Core Functions

| Function | Purpose |
|----------|---------|
| `bun_open()` | Open file, determine size |
| `bun_parse_header()` | Validate header fields |
| `bun_parse_assets()` | Validate all asset records |
| `bun_close()` | Close file handle |

### Helper Functions

| Function | Purpose |
|----------|---------|
| `read_u16_le()` | Read 16-bit little-endian |
| `read_u32_le()` | Read 32-bit little-endian |
| `read_u64_le()` | Read 64-bit little-endian |
| `bun_add_violation()` | Record validation error |

### Result Codes

```c
typedef enum {
    BUN_OK          = 0,  // Success
    BUN_MALFORMED   = 1,  // Invalid structure
    BUN_UNSUPPORTED = 2,  // Known but unsupported
    BUN_ERR_IO      = 3,  // File access failure
} bun_result_t;
```

## Security Design Notes

1. **No silent failures**: All validation errors are collected and reported
2. **Fail-fast on critical errors**: Bounds/overflow issues cause immediate rejection
3. **Defensive arithmetic**: All size/offset calculations use overflow-checked operations
4. **Input sanitization**: Rejects non-printable characters, empty names, invalid flags
5. **Feature restrictions**: Encrypted/executable flags and checksums must be zero

## Testing

Test files are located in `tests/samples/`:

## Running Sanitizer Tests

The repository includes a `sanitizer_test_runner.sh` script. This script runs all tests against the valid and invalid sample files using:

- AddressSanitizer
- UndefinedBehaviorSanitizer

To run the sanitizer test script, use the following commands from the project root:

```bash
chmod +x sanitizer_test_runner.sh
./sanitizer_test_runner.sh

- `valid/` — Well-formed BUN files
- `invalid/` — Malformed files designed to test rejection logic

Run the test suite:
```bash
./sanitizer_test_runner.sh
```
