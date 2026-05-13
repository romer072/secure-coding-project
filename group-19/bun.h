#ifndef BUN_H
#define BUN_H

#include <stdint.h>
#include <stdio.h>

//
// Result codes (per BUN spec section 2)
//

typedef enum {
    BUN_OK            = 0,  // success
    BUN_MALFORMED     = 1,  // spec violation
    BUN_UNSUPPORTED   = 2,  // valid but uses unsupported features
    BUN_ERR_IO        = 3,  // file not found, read error, seek failure, or other I/O error
    BUN_ERR_ARGS      = 4,  // wrong number of arguments
    BUN_ERR_OVERFLOW  = 5,  // integer overflow in offset/size arithmetic
    BUN_ERR_TOOBIG    = 6,  // value valid per spec but exceeds implementation limits
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

#define BUN_COMPRESSION_NONE 0u
#define BUN_COMPRESSION_RLE  1u
#define BUN_COMPRESSION_ZLIB 2u

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

//
// Expected on-disk sizes -- these can be used in assertions or static_asserts.
//

#define BUN_HEADER_SIZE       60
#define BUN_ASSET_RECORD_SIZE 48
#define BUN_NAME_PREFIX_MAX   60u
#define BUN_DATA_PREFIX_MAX   60u

typedef struct {
    BunAssetRecord record;
    char name_prefix[BUN_NAME_PREFIX_MAX + 1u];
    u32  name_prefix_length;
    u8   data_prefix[BUN_DATA_PREFIX_MAX];
    u64  data_prefix_size;
    int  name_truncated;
    int  data_truncated;
    int  data_prefix_is_decompressed;
} BunParsedAsset;

//
// Parse context forward declaration
//
typedef struct BunParseContext BunParseContext;

//
// Callback type for handling parsed assets
//
typedef void (*BunAssetCallback)(BunParseContext *ctx, const BunParsedAsset *asset, u32 asset_index);

//
// Parse context
//
// A struct to store information about the state of your parser (rather than
// passing multiple arguments to every function).
//
// You will likely want to add fields to it as your implementation grows.
//

struct BunParseContext {
    FILE   *file;                       // open file handle
    long    file_size;                  // total file size in bytes
    BunAssetCallback asset_callback;    // callback function called for each successfully parsed asset
    void *callback_userdata;            // optional user data passed to callback
    u32 parsed_asset_count;             // number of assets successfully processed
    bun_result_t last_error_code;       // most recent non-OK parser result
    const char *error_detail;           // static explanation for `last_error_code`
    u64 error_offset;                   // byte offset relevant to the last error
    int error_offset_valid;             // whether `error_offset` is meaningful
};

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
 * Returns BUN_OK, BUN_MALFORMED, BUN_UNSUPPORTED, or BUN_ERR_IO.
 */
bun_result_t bun_parse_header(BunParseContext *ctx, BunHeader *header);

/**
 * Parse and validate all asset records. Called after bun_parse_header().
 * Parsed records/previews are passed to ctx->asset_callback when one is set,
 * and ctx->parsed_asset_count reports how many entries were safely processed
 * before the function returned.
 * Returns BUN_OK, BUN_MALFORMED, BUN_UNSUPPORTED, BUN_ERR_IO, or
 * BUN_ERR_OVERFLOW. On failure, ctx also contains a short diagnostic
 * message and, where available, the relevant byte offset.
 */
bun_result_t bun_parse_assets(BunParseContext *ctx, const BunHeader *header);

/**
 * Close the file handle in ctx. Must only be called on a BunParseContext
 * holding an open FILE*. Returns BUN_OK on success, BUN_ERR_IO on error.
 */
bun_result_t bun_close(BunParseContext *ctx);

#endif // BUN_H
