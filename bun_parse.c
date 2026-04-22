#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "bun.h"

/**
 * Example helper: convert 4 bytes in `buf`, positioned at `offset`,
 * into a little-endian u32.
 */
static u16 read_u16_le(const u8 *buf, size_t offset) {
  return (u16)buf[offset]
     | (u16)buf[offset + 1] << 8;
}

// add u16 and u64 read because the BUN file stores multi-byte integers in little-endian format.

static u32 read_u32_le(const u8 *buf, size_t offset) {
  return (u32)buf[offset]
     | (u32)buf[offset + 1] << 8
     | (u32)buf[offset + 2] << 16
     | (u32)buf[offset + 3] << 24;
}

static u64 read_u64_le(const u8 *buf, size_t offset) {
  return (u64)buf[offset]
     | (u64)buf[offset + 1] << 8
     | (u64)buf[offset + 2] << 16
     | (u64)buf[offset + 3] << 24
     | (u64)buf[offset + 4] << 32
     | (u64)buf[offset + 5] << 40
     | (u64)buf[offset + 6] << 48
     | (u64)buf[offset + 7] << 56;
}

//
// API implementation
static int checked_add_u64(u64 a, u64 b, u64 *result) {
  if (UINT64_MAX - a < b) {
    return 0; // overflow
  }
  *result = a + b;
  return 1; // success
}

static int checked_mul_u64(u64 a, u64 b, u64 *result) {
  if (a != 0 && UINT64_MAX / a < b) {
    return 0; // overflow
  }
  *result = a * b;
  return 1; // success
}

static int range_within_file(u64 offset, u64 size, u64 file_size) {
  u64 end;
  if (!checked_add_u64(offset, size, &end)) {
    return 0; // overflow
  }
  return end <= file_size;
}

static int ranges_overlap(u64 offset1, u64 size1, u64 offset2, u64 size2) {
  u64 end1, end2;

  if (!checked_add_u64(offset1, size1, &end1)) {
    return 1;
  }
  if (!checked_add_u64(offset2, size2, &end2)) {
    return 1;
  }

  return (offset1 < end2) && (offset2 < end1);
}
//

bun_result_t bun_open(const char *path, BunParseContext *ctx) {
  // we open the file; seek to the end, to get the size; then jump back to the
  // beginning, ready to start parsing.

  ctx->file = fopen(path, "rb");
  if (!ctx->file) {
    return BUN_ERR_IO;
  }

  if (fseek(ctx->file, 0, SEEK_END) != 0) {
    fclose(ctx->file);
    return BUN_ERR_IO;
  }
  ctx->file_size = ftell(ctx->file);
  if (ctx->file_size < 0) {
    fclose(ctx->file);
    return BUN_ERR_IO;
  }
  rewind(ctx->file);

  return BUN_OK;
}

bun_result_t bun_parse_header(BunParseContext *ctx, BunHeader *header) {
  u8 buf[BUN_HEADER_SIZE];

  // our file is far too short, and cannot be valid!
  // (query: how do we let `main` know that "file was too short"
  // was the exact problem? Where can we put details about the
  // exact validation problem that occurred?)
  if (ctx->file_size < (long)BUN_HEADER_SIZE) {
    return BUN_MALFORMED;
  }

  // slurp the header into `buf`
  if (fread(buf, 1, BUN_HEADER_SIZE, ctx->file) != BUN_HEADER_SIZE) {
    return BUN_ERR_IO;
  }

  // TODO: populate `header` from `buf`.
  header->magic               = read_u32_le(buf, 0);
  header->version_major       = read_u16_le(buf, 4);
  header->version_minor       = read_u16_le(buf, 6);
  header->asset_count         = read_u32_le(buf, 8);
  header->asset_table_offset  = read_u64_le(buf, 12);
  header->string_table_offset = read_u64_le(buf, 20);
  header->string_table_size   = read_u64_le(buf, 28);
  header->data_section_offset = read_u64_le(buf, 36);
  header->data_section_size   = read_u64_le(buf, 44);
  header->reserved            = read_u64_le(buf, 52);
  // TODO: validate fields and return BUN_MALFORMED or BUN_UNSUPPORTED
  // as required by the spec. The magic check is a good place to start.

  if (header->magic != BUN_MAGIC) {
    return BUN_MALFORMED;
  }

  // we only support version 1.0 of the BUN format, so if the major or minor
  // version is different, we should reject the file with BUN_UNSUPPORTED.  
  if (header->version_major != BUN_VERSION_MAJOR ||
    header->version_minor != BUN_VERSION_MINOR) {
  return BUN_UNSUPPORTED;
  }

    // The table, data and string offset along with their two sizes must all be divisible by 4
   if (header->asset_table_offset % 4 != 0 ||
    header->data_section_offset % 4 != 0 ||
     header->data_section_size % 4 != 0 ||
     header->string_table_offset % 4 != 0 ||
     header->string_table_size % 4 != 0) {
   return BUN_MALFORMED;
 }
  
  u64 file_size = (u64)ctx->file_size;
  u64 asset_table_size = 0;

  if (!checked_mul_u64((u64)header->asset_count,
                        (u64)BUN_ASSET_RECORD_SIZE,
                        &asset_table_size)) {
    return BUN_MALFORMED;
  }

  if (!range_within_file(header->asset_table_offset, asset_table_size, file_size) ||
      !range_within_file(header->string_table_offset, header->string_table_size, file_size) ||
      !range_within_file(header->data_section_offset, header->data_section_size, file_size)) {
    return BUN_MALFORMED;
  }

  if (ranges_overlap(header->asset_table_offset, asset_table_size,
                      header->string_table_offset, header->string_table_size) ||
      ranges_overlap(header->asset_table_offset, asset_table_size,
                      header->data_section_offset, header->data_section_size) ||
      ranges_overlap(header->string_table_offset, header->string_table_size,
                      header->data_section_offset, header->data_section_size)) {
    return BUN_MALFORMED;
  }

  return BUN_OK;
}

bun_result_t bun_parse_assets(BunParseContext *ctx, const BunHeader *header) {

  // TODO: implement asset record parsing and validation

  return BUN_OK;
}

bun_result_t bun_close(BunParseContext *ctx) {
  assert(ctx->file);

  int res = fclose(ctx->file);
  if (res) {
    return BUN_ERR_IO;
  } else {
    ctx->file = NULL;
    return BUN_OK;
  }
}
