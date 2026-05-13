#ifndef BUN_H
#define BUN_H

#include <stdint.h>
#include <stdio.h>

//
// Result codes (per BUN spec section 2)
//

typedef enum {
    BUN_OK          = 0,
    BUN_MALFORMED   = 1,
    BUN_UNSUPPORTED = 2,
    BUN_ERR_IO      = 3,      // I/O error or file not found
} bun_result_t;

//
// Data types (per BUN spec section 2)
// All multi-byte integers are little-endian on disk.
//

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

//
// On-disk structures (per BUN spec sections 4 and 5)
//

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

typedef struct {
    u8 count;
    u8 value;
} BunRlePair;

//
// Expected on-disk sizes -- these can be used in assertions or static_asserts.
//

#define BUN_HEADER_SIZE       60
#define BUN_ASSET_RECORD_SIZE 48
#define MAX_ERRORS            32
#define MAX_PREVIEW_SIZE      60


typedef struct {
    FILE *file;         // open file handle
    long file_size;     // total file size in bytes

    const char *error_msgs[MAX_ERRORS];     // print a message when the file is malformed or unsupported
    int error_count;

} BunParseContext;

//
// Public API
//
// The function declarations below define the public API for your parser;
// you implement them in the `bun_parse.c` file.
//
// A note on I/O and output:
//   The functions below return result codes; the intention is that they
//   should not print to stdout or stderr themselves.
//   Keeping I/O out of these functions makes them much easier to test (your
//   tests can call them and inspect the return value without terminal output
//   getting cluttered with other content).
//   If you need to pass additional information in or out, `ctx` is a good place
//   to put it.
//
//   So printing (human-readable output for valid files and error messages
//   for invalid ones) should happen in main.c, based on the result code and
//   the content of `ctx`.
//
//   (This is a suggestion, not a requirement. But mixing output deeply into
//   parsing logic tends to make both harder to maintain.)

/**
 * Open a BUN file and populate ctx. Returns BUN_ERR_IO if the file cannot
 * be opened or its size determined.
 */
bun_result_t bun_open(const char *path, BunParseContext *ctx);

/**
 * Parse and validate the BUN header from ctx->file, populating *header.
 * Returns BUN_OK, BUN_MALFORMED, or BUN_UNSUPPORTED.
 */
bun_result_t bun_parse_header(BunParseContext *ctx, BunHeader *header);

/**
 * Parse and validate all asset records. Called after bun_parse_header().
 * Returns BUN_OK, BUN_MALFORMED, or BUN_UNSUPPORTED.
 *
 * You will probably want to extend this signature -- for instance, to pass
 * in the header (needed for offset calculations) or to return the parsed
 * records to the caller.
 */
bun_result_t bun_parse_assets(BunParseContext *ctx, const BunHeader *header);

/**
 * Print a human-readable summary of the BUN header to stdout.
 * Called on valid files to display header information.
 */
void bun_print_header(const BunHeader *header);

/**
 * Print a human-readable summary of one asset record.
 * The name and payload pointers may be NULL when unavailable.
 */
void bun_print_asset_record(const BunAssetRecord *record, int index, u32 asset_count, const char *name, const u8 *data_preview_bytes, size_t data_preview_size);

/**
 * Close the file handle in ctx. Must only be called on a BunParseContext
 * holding an open FILE*. Returns BUN_OK on success, BUN_ERR_IO on error.
 */
bun_result_t bun_close(BunParseContext *ctx);

#endif // BUN_H
