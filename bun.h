#ifndef BUN_H
#define BUN_H

#include <stdint.h>
#include <stddef.h> 
#include <stdio.h>

// Result codes (per BUN spec section 2)

typedef enum {
    BUN_OK          = 0,
    BUN_MALFORMED   = 1,
    BUN_UNSUPPORTED = 2,
    BUN_ERR_IO      = 3,   // I/O error or file not found
} bun_result_t;

// Data types (per BUN spec section 2)
// All multi-byte integers are little-endian on disk.

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

// On-disk structures (per BUN spec sections 4 and 5)

#define BUN_MAGIC         0x304E5542u   // "BUN0" in little-endian
#define BUN_VERSION_MAJOR 1
#define BUN_VERSION_MINOR 0

#define BUN_FLAG_ENCRYPTED  0x1u
#define BUN_FLAG_EXECUTABLE 0x2u

typedef struct {
    u32 magic;
    u16 version_major;
    u16 version_minor;
    u32 asset_count;
    u64 asset_table_offset;
    u64 string_table_offset;
    u64 string_table_size;
    u64 data_section_offset;
    u64 data_section_size;
    u64 reserved;
} BunHeader;

typedef struct {
    u32 name_offset;
    u32 name_length;
    u64 data_offset;
    u64 data_size;
    u64 uncompressed_size;
    u32 compression;
    u32 type;
    u32 checksum;
    u32 flags;
} BunAssetRecord;

// Expected on-disk sizes -- these can be used in assertions or static_asserts.

#define BUN_HEADER_SIZE       60
#define BUN_ASSET_RECORD_SIZE 48

// Parse context

#define BUN_MAX_VIOLATIONS 100
#define BUN_MAX_VIOLATION_LEN 256

typedef struct {
    char message[BUN_MAX_VIOLATION_LEN];
} BunViolation;

typedef struct {
    FILE   *file;           // open file handle
    long    file_size;      // total file size in bytes
    // add further fields here as needed

    BunViolation violations[BUN_MAX_VIOLATIONS];
    size_t violation_count;
} BunParseContext;

// Public API

u16 read_u16_le(const u8 *buf, size_t offset);
u32 read_u32_le(const u8 *buf, size_t offset);
u64 read_u64_le(const u8 *buf, size_t offset);


// Record a validation error message in the parse context.
// This allows main.c to collect and print all violations rather than
// immediately printing to stderr, making the parser easier to test.

// @param ctx     Pointer to the parse context
// @param message Error description to store

void bun_add_violation(BunParseContext *ctx, const char *message);

// The function declarations below define the public API for the parser.
// Implement them in bun_parse.c.

// I/O Note: These functions return result codes and should not print to
// stdout or stderr themselves. Keeping I/O out of these functions makes
// them easier to test (tests can inspect return values without terminal
// output getting cluttered). Use ctx to pass additional information.
//
// Output: Printing (human-readable output for valid files and error
// messages for invalid ones) should happen in main.c, based on the
// result code and the content of ctx.


// Open a BUN file and populate ctx with file handle and size.

// @param path Path to the BUN file
// @param ctx  Parse context to populate
// @return     BUN_ERR_IO if file cannot be opened/sized, BUN_OK on success

bun_result_t bun_open(const char *path, BunParseContext *ctx);


// Parse and validate the BUN header from ctx->file, populating *header.

// @param ctx    Parse context with open file handle
// @param header Header structure to populate
// @return       BUN_OK if valid, BUN_MALFORMED if invalid, BUN_UNSUPPORTED if wrong version

bun_result_t bun_parse_header(BunParseContext *ctx, BunHeader *header);


// Parse and validate all asset records. Must be called after bun_parse_header().
 
// @param ctx    Parse context with open file handle
// @param header Parsed header (needed for offset calculations)
// @return       BUN_OK if valid, BUN_MALFORMED if invalid, BUN_UNSUPPORTED if unsupported features

bun_result_t bun_parse_assets(BunParseContext *ctx, const BunHeader *header);


// Close the file handle in ctx.
// @param ctx Parse context with open FILE*
// @return    BUN_OK on success, BUN_ERR_IO on error

bun_result_t bun_close(BunParseContext *ctx);

#endif // BUN_H
