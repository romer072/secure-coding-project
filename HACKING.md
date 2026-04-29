# Contributor's guide

(`HACKING.md` conventionally documents technical details and coding standards
needed to keep the codebase maintainable - it's aimed at developers and
contributors. Feel free to replace with your own content.)

## Running tests

The test code provided assumes that

- Sample BUN files are unzipped into tests/samples/{bigfile,valid,invalid}.  
- The test suite is run from the project root by executing `make test`.

## Code Overview

This BUN file parser consists of three main source files:

| File | Purpose |
|------|---------|
| `bun.h` | Header file: type definitions, constants, struct definitions |
| `bun_parse.c` | Parser implementation: file reading, validation, error tracking |
| `main.c` | CLI entry point: parses arguments, drives parser, formats output |

---

## bun.h

**Purpose:** Central header defining all types, constants, and data structures used across the parser.

### Result Codes

```c
typedef enum {
    BUN_OK          = 0,  // File is valid
    BUN_MALFORMED   = 1,  // File structure is invalid/corrupted
    BUN_UNSUPPORTED = 2,  // File uses unsupported features
    BUN_ERR_IO      = 3   // File not found or read error
} bun_result_t;
```

### Data Types

All multi-byte integers are little-endian on disk:

```c
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
```

### Constants

| Constant | Value | Description |
|----------|-------|-------------|
| `BUN_MAGIC` | `0x304E5542` | "BUN0" in little-endian |
| `BUN_VERSION_MAJOR` | `1` | Major version |
| `BUN_VERSION_MINOR` | `0` | Minor version |
| `BUN_HEADER_SIZE` | `60` | Header size in bytes |
| `BUN_ASSET_RECORD_SIZE` | `48` | Asset record size in bytes |
| `BUN_FLAG_ENCRYPTED` | `0x1` | Encrypted file flag |
| `BUN_FLAG_EXECUTABLE` | `0x2` | Executable file flag |

### Structures

**BunHeader** - File header (60 bytes):
- `magic`, `version_major`, `version_minor`
- `asset_count`, `asset_table_offset`
- `string_table_offset`, `string_table_size`
- `data_section_offset`, `data_section_size`
- `reserved`

**BunAssetRecord** - Asset record (48 bytes):
- `name_offset`, `name_length`
- `data_offset`, `data_size`, `uncompressed_size`
- `compression`, `type`, `checksum`, `flags`

**BunViolation** - Validation error:
- `message[BUN_MAX_VIOLATION_LEN]` - Error description

**BunParseContext** - Parser state:
- `file` - Open file handle
- `file_size` - Total file size
- `violations[]` - Array of validation errors
- `violation_count` - Number of errors recorded

---

## bun_parse.c

**Purpose:** Core parser implementation with file I/O, validation, and error tracking.

### Public API

| Function | Description |
|----------|-------------|
| `bun_open(path, ctx)` | Opens a BUN file, seeks to end to get size |
| `bun_parse_header(ctx, header)` | Parses and validates the 60-byte header |
| `bun_parse_assets(ctx, header)` | Parses asset records and validates bounds |
| `bun_close(ctx)` | Closes the open file handle |
| `bun_add_violation(ctx, message)` | Records a validation error |

### Little-Endian Read Functions

| Function | Description |
|----------|-------------|
| `read_u16_le(buf, offset)` | Reads 2-byte little-endian u16 |
| `read_u32_le(buf, offset)` | Reads 4-byte little-endian u32 |
| `read_u64_le(buf, offset)` | Reads 8-byte little-endian u64 |

### Internal Validation Functions

| Function | Description |
|----------|-------------|
| `checked_add_u64(a, b, result)` | Checked 64-bit addition (prevents overflow) |
| `checked_mul_u64(a, b, result)` | Checked 64-bit multiplication (prevents overflow) |
| `range_within_file(offset, size, file_size)` | Checks if range is within file bounds |
| `ranges_overlap(o1, s1, o2, s2)` | Checks if two ranges overlap |

### Validation Checks (in bun_parse_header)

1. **File size** - Must be at least 60 bytes (header size)
2. **Magic number** - Must be `0x304E5542` ("BUN0")
3. **Version** - Must be 1.0 (only supported version)
4. **Alignment** - All offsets/sizes must be divisible by 4
5. **Overflow** - Asset table size must not overflow
6. **Bounds** - All sections must be within file
7. **Overlap** - Sections must not overlap each other

### Validation Checks (in bun_parse_assets)

1. **Name bounds** - Asset name offset/length within string table
2. **Data bounds** - Asset data offset/size within data section
3. **Compression** - Only compression type 0 (none) is supported
4. **Checksum** - Only checksum type 0 (none) is supported

---

## main.c

**Purpose:** Command-line interface that drives the parser and formats output.

### Functions

| Function | Description |
|----------|-------------|
| `is_printable_ascii(data, size)` | Checks if data is printable ASCII (32-126) |
| `print_hex_dump(data, size)` | Prints up to 60 bytes as hex |
| `print_text_preview(data, size)` | Prints up to 60 bytes as escaped text |
| `result_to_string(result)` | Converts result code to string |

### Output Format

**Valid file:**
```
=== BUN File Header ===
  Magic: 0x304e5542 (expected 0x304e5542)
  Version: 1.0
  Asset count: 1
  ...
=== Asset Records ===

Asset 0:
  Name: "hello"
  ...

Result: bun_ok
```

**Invalid file:**
```
Error: header invalid (code 1=malformed)

Validation errors:
  - Invalid magic number: expected 0x304E5542

Result: bun_malformed
```

### Exit Codes

| Code | Meaning |
|------|---------|
| 0 | Success (valid file) |
| 1 | Malformed file |
| 2 | Unsupported features |
| 3+ | I/O error |

---

## Building

```bash
make clean    # Clean build artifacts
make all      # Build the parser
./bun_parser <file.bun>  # Run on a BUN file
```

## Debugging

Common issues:
- **Missing functions:** Ensure `read_u16_le`, `read_u32_le`, `read_u64_le` are declared in `bun.h` and not `static`
- **Struct field mismatch:** Ensure `BunViolation` has `message` field, not `description`
- **Link errors:** Ensure all source files are included in Makefile

