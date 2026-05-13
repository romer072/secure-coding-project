#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <limits.h>

#include "bun.h"

/*
 * Decode a 16-bit little-endian value from `buf[offset...]`.
 */
static u16 read_u16_le(const u8 *buf, size_t offset) {
  return (u16)buf[offset]
     | (u16)buf[offset + 1] << 8;
}

/*
 * Decode a 32-bit little-endian value from `buf[offset...]`.
 */
static u32 read_u32_le(const u8 *buf, size_t offset) {
  return (u32)buf[offset]
     | (u32)buf[offset + 1] << 8
     | (u32)buf[offset + 2] << 16
     | (u32)buf[offset + 3] << 24;
}

/*
 * Decode a 64-bit little-endian value from `buf[offset...]`.
 */
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

typedef struct {
  u64 offset;
  u64 size;
} BunSection;

enum {
  BUN_HEADER_MAGIC_OFFSET = 0u,
  BUN_HEADER_VERSION_MAJOR_OFFSET = 4u,
  BUN_HEADER_VERSION_MINOR_OFFSET = 6u,
  BUN_HEADER_ASSET_COUNT_OFFSET = 8u,
  BUN_HEADER_ASSET_TABLE_OFFSET = 12u,
  BUN_HEADER_STRING_TABLE_OFFSET = 20u,
  BUN_HEADER_STRING_TABLE_SIZE_OFFSET = 28u,
  BUN_HEADER_DATA_SECTION_OFFSET = 36u,
  BUN_HEADER_DATA_SECTION_SIZE_OFFSET = 44u,
  BUN_RECORD_NAME_OFFSET_OFFSET = 0u,
  BUN_RECORD_NAME_LENGTH_OFFSET = 4u,
  BUN_RECORD_DATA_OFFSET_OFFSET = 8u,
  BUN_RECORD_DATA_SIZE_OFFSET = 16u,
  BUN_RECORD_UNCOMPRESSED_SIZE_OFFSET = 24u,
  BUN_RECORD_COMPRESSION_OFFSET = 32u,
  BUN_RECORD_CHECKSUM_OFFSET = 40u,
  BUN_RECORD_FLAGS_OFFSET = 44u
};

static u32 crc_table[256];
static int crc_table_initialized = 0;

#define BUN_RLE_EXACT_SIZE_LIMIT ((u64)UINT32_MAX)

/*
 * Clear any previously recorded parser error details in `ctx`.
 */
static void clear_error_state(BunParseContext *ctx) {
  ctx->last_error_code = BUN_OK;
  ctx->error_detail = NULL;
  ctx->error_offset = 0;
  ctx->error_offset_valid = 0;
}

/*
 * Record an error without a specific byte offset and return `code`.
 */
static bun_result_t fail_with(BunParseContext *ctx,
                              bun_result_t code,
                              const char *detail) {
  ctx->last_error_code = code;
  ctx->error_detail = detail;
  ctx->error_offset = 0;
  ctx->error_offset_valid = 0;
  return code;
}

/*
 * Record an error at `offset` and return `code`.
 */
static bun_result_t fail_at(BunParseContext *ctx,
                            bun_result_t code,
                            const char *detail,
                            u64 offset) {
  ctx->last_error_code = code;
  ctx->error_detail = detail;
  ctx->error_offset = offset;
  ctx->error_offset_valid = 1;
  return code;
}

/*
 * Preserve only the first unsupported-feature detail seen during parsing.
 */
static void note_first_issue(const char **detail,
                             u64 *offset,
                             int *offset_valid,
                             const char *candidate_detail,
                             u64 candidate_offset) {
  if (*detail != NULL) {
    return;
  }

  *detail = candidate_detail;
  *offset = candidate_offset;
  *offset_valid = 1;
}

/*
 * Return 1 and store `lhs + rhs` in `result`; return 0 on u64 overflow.
 */
static int add_u64_checked(u64 lhs, u64 rhs, u64 *result) {
  if (UINT64_MAX - lhs < rhs) {
    return 0;
  }

  *result = lhs + rhs;
  return 1;
}

/*
 * Return 1 and store `lhs * rhs` in `result`; return 0 on u64 overflow.
 */
static int mul_u64_checked(u64 lhs, u64 rhs, u64 *result) {
  if (lhs != 0 && UINT64_MAX / lhs < rhs) {
    return 0;
  }

  *result = lhs * rhs;
  return 1;
}

/*
 * Return the current file size as `u64`. The caller must only use this after
 * `bun_open()` has validated that `ctx->file_size` is non-negative.
 */
static u64 ctx_file_size_u64(const BunParseContext *ctx) {
  assert(ctx->file_size >= 0);
  return (u64)ctx->file_size;
}

/*
 * Return 1 and store the exclusive end offset of `section`; return 0 if the
 * offset/size pair would overflow u64 arithmetic.
 */
static int section_end(const BunSection *section, u64 *end) {
  return add_u64_checked(section->offset, section->size, end);
}

/*
 * Return 1 if `section` lies fully inside a file of size `file_size`.
 */
static int section_within_file(const BunSection *section, u64 file_size) {
  u64 end = 0;

  if (!section_end(section, &end)) {
    return 0;
  }

  return section->offset <= file_size && end <= file_size;
}

/*
 * Return 1 if two non-empty sections overlap. Overflow while computing an end
 * offset is treated as overlap so the layout can be rejected safely.
 */
static int sections_overlap(const BunSection *lhs, const BunSection *rhs) {
  u64 lhs_end = 0;
  u64 rhs_end = 0;

  if (lhs->size == 0 || rhs->size == 0) {
    return 0;
  }
  if (!section_end(lhs, &lhs_end) || !section_end(rhs, &rhs_end)) {
    return 1;
  }

  return lhs->offset < rhs_end && rhs->offset < lhs_end;
}

/*
 * Return BUN_OK if `file` can be positioned at `offset`.
 */
static bun_result_t seek_to_u64(FILE *file, u64 offset) {
  if (offset > (u64)LONG_MAX) {
    return BUN_ERR_TOOBIG;
  }
  if (fseek(file, (long)offset, SEEK_SET) != 0) {
    return BUN_ERR_IO;
  }
  return BUN_OK;
}

/*
 * Read exactly `size` bytes into `buf`.
 */
static bun_result_t read_exact(FILE *file, void *buf, size_t size) {
  if (fread(buf, 1, size, file) == size) {
    return BUN_OK;
  }
  if (feof(file)) {
    return BUN_MALFORMED;
  }
  return BUN_ERR_IO;
}

/*
 * Read one on-disk asset record from `offset` into `record`.
 */
static bun_result_t read_asset_record(FILE *file, u64 offset, BunAssetRecord *record) {
  u8 buf[BUN_ASSET_RECORD_SIZE];

  bun_result_t result;

  result = seek_to_u64(file, offset);
  if (result != BUN_OK) {
    return result;
  }

  result = read_exact(file, buf, sizeof(buf));
  if (result != BUN_OK) {
    return result;
  }

  record->name_offset       = read_u32_le(buf, 0);
  record->name_length       = read_u32_le(buf, 4);
  record->data_offset       = read_u64_le(buf, 8);
  record->data_size         = read_u64_le(buf, 16);
  record->uncompressed_size = read_u64_le(buf, 24);
  record->compression       = read_u32_le(buf, 32);
  record->type              = read_u32_le(buf, 36);
  record->checksum          = read_u32_le(buf, 40);
  record->flags             = read_u32_le(buf, 44);

  return BUN_OK;
}

/*
 * Release any previously captured asset previews and reset their count to 0.
 */
static void reset_parsed_assets(BunParseContext *ctx) {
  ctx->parsed_asset_count = 0;
  ctx->asset_callback = NULL;
  ctx->callback_userdata = NULL;
}

/*
 * Validate that the header, asset table, string table, and data section all
 * stay within the file and do not overlap. Returns BUN_OK on success.
 */
static bun_result_t validate_section_layout(BunParseContext *ctx,
                                            const BunSection *header_section,
                                            const BunSection *asset_section,
                                            const BunSection *string_section,
                                            const BunSection *data_section,
                                            u64 file_size) {
  if (!section_within_file(header_section, file_size)
    || !section_within_file(asset_section, file_size)
    || !section_within_file(string_section, file_size)
    || !section_within_file(data_section, file_size)) {
    return fail_with(ctx,
                     BUN_MALFORMED,
                     "one or more file sections lie outside the input file");
  }

  if (sections_overlap(header_section, asset_section)
    || sections_overlap(header_section, string_section)
    || sections_overlap(header_section, data_section)
    || sections_overlap(asset_section, string_section)
    || sections_overlap(asset_section, data_section)
    || sections_overlap(string_section, data_section)) {
    return fail_with(ctx, BUN_MALFORMED, "BUN sections overlap in the input file");
  }

  return BUN_OK;
}

/*
 * Asset names are required to be non-empty printable ASCII stored inside the
 * string table. Validate them by streaming a small chunk at a time.
 */
static bun_result_t validate_asset_name(BunParseContext *ctx,
                                        const BunHeader *header,
                                        const BunAssetRecord *record,
                                        u64 record_offset,
                                        BunParsedAsset *parsed) {
  u8  buf[256];
  u64 remaining = (u64)record->name_length;
  u64 offset = 0;
  u64 name_start = 0;
  u32 prefix_len = 0;

  if (record->name_length == 0) {
    return fail_at(ctx,
                   BUN_MALFORMED,
                   "asset name length must be non-zero",
                   record_offset + BUN_RECORD_NAME_LENGTH_OFFSET);
  }

  parsed->name_truncated = record->name_length > BUN_NAME_PREFIX_MAX;

  if (!add_u64_checked(header->string_table_offset,
                       (u64)record->name_offset,
                       &name_start)) {
    return fail_at(ctx,
                   BUN_ERR_OVERFLOW,
                   "asset name start offset overflows 64-bit arithmetic",
                   record_offset + BUN_RECORD_NAME_OFFSET_OFFSET);
  }

  while (remaining > 0) {
    size_t chunk = remaining < sizeof(buf) ? (size_t)remaining : sizeof(buf);
    size_t idx = 0;
    u64 chunk_start = 0;
    bun_result_t result;

    if (!add_u64_checked(name_start, offset, &chunk_start)) {
      return fail_at(ctx,
                     BUN_ERR_OVERFLOW,
                     "asset name read offset overflows 64-bit arithmetic",
                     record_offset + BUN_RECORD_NAME_OFFSET_OFFSET);
    }

    result = seek_to_u64(ctx->file, chunk_start);
    if (result != BUN_OK) {
      return fail_at(ctx,
                     result,
                     "failed to seek to asset name bytes",
                     chunk_start);
    }
    result = read_exact(ctx->file, buf, chunk);
    if (result != BUN_OK) {
      return fail_at(ctx,
                     result,
                     "failed to read asset name bytes",
                     chunk_start);
    }

    for (idx = 0; idx < chunk; idx++) {
      if (buf[idx] < 0x20 || buf[idx] > 0x7e) {
        return fail_at(ctx,
                       BUN_MALFORMED,
                       "asset name contains non-printable ASCII bytes",
                       chunk_start + (u64)idx);
      }
      if (prefix_len < BUN_NAME_PREFIX_MAX) {
        parsed->name_prefix[prefix_len++] = (char)buf[idx];
      }
    }

    remaining -= chunk;
    offset += chunk;
  }

  parsed->name_prefix[prefix_len] = '\0';
  parsed->name_prefix_length = prefix_len;
  return BUN_OK;
}

/*
 * For uncompressed or unsupported asset formats, capture a small raw prefix so
 * main.c can still display useful context without loading the whole payload.
 */
static bun_result_t capture_raw_data_preview(BunParseContext *ctx,
                                             const BunHeader *header,
                                             const BunAssetRecord *record,
                                             u64 record_offset,
                                             BunParsedAsset *parsed) {
  u64 data_start = 0;
  size_t preview_size = 0;

  parsed->data_prefix_is_decompressed = 0;
  parsed->data_prefix_size = 0;
  parsed->data_truncated = record->data_size > BUN_DATA_PREFIX_MAX;

  bun_result_t result = BUN_OK;

  if (record->data_size == 0u) {
    return BUN_OK;
  }
  if (!add_u64_checked(header->data_section_offset,
                       record->data_offset,
                       &data_start)) {
    return fail_at(ctx,
                   BUN_ERR_OVERFLOW,
                   "asset data start offset overflows 64-bit arithmetic",
                   record_offset + BUN_RECORD_DATA_OFFSET_OFFSET);
  }

  preview_size = record->data_size < BUN_DATA_PREFIX_MAX
               ? (size_t)record->data_size
               : (size_t)BUN_DATA_PREFIX_MAX;

  result = seek_to_u64(ctx->file, data_start);
  if (result != BUN_OK) {
    return fail_at(ctx,
                   result,
                   "failed to seek to asset data bytes",
                   data_start);
  }
  result = read_exact(ctx->file, parsed->data_prefix, preview_size);
  if (result != BUN_OK) {
    return fail_at(ctx,
                   result,
                   "failed to read asset data preview",
                   data_start);
  }

  parsed->data_prefix_size = preview_size;
  return BUN_OK;
}

/*
 * RLE validation is done without allocating an output buffer. This keeps the
 * parser's memory use sub-linear even for large data sections, while still
 * preserving a short uncompressed preview for display.
 */
static bun_result_t validate_rle_data(BunParseContext *ctx,
                                      const BunHeader *header,
                                      const BunAssetRecord *record,
                                      u64 record_offset) {
  u8  buf[2048];
  u64 remaining = record->data_size;
  u64 data_start = 0;
  u64 expanded_size = 0;

  bun_result_t result = BUN_OK;

  if ((record->data_size % 2u) != 0u) {
    return fail_at(ctx,
                   BUN_MALFORMED,
                   "RLE asset data size must be even",
                   record_offset + BUN_RECORD_DATA_SIZE_OFFSET);
  }

  if (!add_u64_checked(header->data_section_offset,
                       record->data_offset,
                       &data_start)) {
    return fail_at(ctx,
                   BUN_ERR_OVERFLOW,
                   "RLE asset data start offset overflows 64-bit arithmetic",
                   record_offset + BUN_RECORD_DATA_OFFSET_OFFSET);
  }

  result = seek_to_u64(ctx->file, data_start);
  if (result != BUN_OK) {
    return fail_at(ctx,
                   result,
                   "failed to seek to RLE asset data",
                   data_start);
  }

  while (remaining > 0) {
    size_t chunk = remaining < sizeof(buf) ? (size_t)remaining : sizeof(buf);
    size_t idx = 0;

    result = read_exact(ctx->file, buf, chunk);
    if (result != BUN_OK) {
      return fail_at(ctx,
                     result,
                     "failed to read RLE asset data",
                     data_start + (record->data_size - remaining));
    }

    for (idx = 0; idx < chunk; idx += 2) {
      u8 count = buf[idx];
      if (count == 0) {
        return fail_at(ctx,
                       BUN_MALFORMED,
                       "RLE run length must be non-zero",
                       data_start + (record->data_size - remaining) + (u64)idx);
      }
      if (!add_u64_checked(expanded_size, (u64)count, &expanded_size)) {
        return fail_at(ctx,
                       BUN_ERR_OVERFLOW,
                       "RLE expanded size overflows 64-bit arithmetic",
                       record_offset + BUN_RECORD_UNCOMPRESSED_SIZE_OFFSET);
      }
    }

    remaining -= chunk;
  }

  if (expanded_size == record->uncompressed_size) {
    return BUN_OK;
  }
  if (expanded_size > record->uncompressed_size) {
    return fail_at(ctx,
                   BUN_MALFORMED,
                   "RLE expanded size exceeds uncompressed_size",
                   record_offset + BUN_RECORD_UNCOMPRESSED_SIZE_OFFSET);
  }

  return fail_at(ctx,
                 BUN_MALFORMED,
                 "RLE expanded size does not match uncompressed_size",
                 record_offset + BUN_RECORD_UNCOMPRESSED_SIZE_OFFSET);
}


static bun_result_t generate_rle_preview(BunParseContext *ctx,
                                         const BunHeader *header,
                                         const BunAssetRecord *record,
                                         u64 record_offset,
                                         BunParsedAsset *parsed) {
  u64 data_start = 0;
  u8 buf[2048];
  u64 remaining = record->data_size;
  bun_result_t result = BUN_OK;

  parsed->data_prefix_is_decompressed = 1;
  parsed->data_prefix_size = 0;
  parsed->data_truncated = record->uncompressed_size > BUN_DATA_PREFIX_MAX;

  if (record->data_size == 0 || record->uncompressed_size == 0) {
    return BUN_OK;
  }

  if (!add_u64_checked(header->data_section_offset, record->data_offset, &data_start)) {
    return fail_at(ctx,
                   BUN_ERR_OVERFLOW,
                   "asset data start offset overflows 64-bit arithmetic",
                   record_offset + BUN_RECORD_DATA_OFFSET_OFFSET);
  }

  result = seek_to_u64(ctx->file, data_start);
  if (result != BUN_OK) {
    return fail_at(ctx,
                   result,
                   "failed to seek to RLE data",
                   data_start);
  }

  while (remaining > 0 && parsed->data_prefix_size < BUN_DATA_PREFIX_MAX) {
    size_t chunk = remaining < sizeof(buf) ? (size_t)remaining : sizeof(buf);

    result = read_exact(ctx->file, buf, chunk);
    if (result != BUN_OK) {
      return fail_at(ctx,
                     result,
                     "failed to read RLE data for preview",
                     data_start + (record->data_size - remaining));
    }

    for (size_t idx = 0; idx < chunk && parsed->data_prefix_size < BUN_DATA_PREFIX_MAX; idx += 2) {
      u8 count = buf[idx];
      u8 value = buf[idx + 1];
      size_t copies_to_store = (size_t)(BUN_DATA_PREFIX_MAX - parsed->data_prefix_size);

      if (copies_to_store > count) {
        copies_to_store = count;
      }

      memset(parsed->data_prefix + parsed->data_prefix_size, value, copies_to_store);
      parsed->data_prefix_size += copies_to_store;
    }

    remaining -= chunk;
  }

  return BUN_OK;
}

typedef struct {
  u8 active_value;
  u8 active_count;
} BunRleState;

/*
 * Decompress an RLE stream into a bounded output buffer while carrying state
 * across calls. The caller validates the input stream before using this helper.
 */
static void decompress_rle(BunRleState *state,
                           const u8 *in_buffer,
                           size_t in_size,
                           u8 *out_buffer,
                           size_t out_size,
                           size_t *bytes_consumed,
                           size_t *bytes_written) {
  while (out_size != 0u) {
    if (state->active_count == 0u) {
      if (*bytes_consumed >= in_size) {
        return;
      }
      state->active_count = in_buffer[*bytes_consumed];
      state->active_value = in_buffer[*bytes_consumed + 1u];
      *bytes_consumed += 2u;
    }

    size_t store_count = out_size > state->active_count ? state->active_count : out_size;
    memset(out_buffer, state->active_value, store_count);

    out_buffer += store_count;
    out_size -= store_count;
    *bytes_written += store_count;
    state->active_count -= (u8)store_count;
  }
}

/*
 * Populate the lookup table for the standard CRC-32 polynomial.
 */
static void populate_crc_table(void) {
  u32 crc32 = 1u;

  for (unsigned int i = 128u; i != 0u; i >>= 1u) {
    crc32 = (crc32 >> 1u) ^ ((crc32 & 1u) ? 0xedb88320u : 0u);
    for (unsigned int j = 0u; j < 256u; j += 2u * i) {
      crc_table[i + j] = crc32 ^ crc_table[j];
    }
  }
}

/*
 * Compute the next CRC-32 value for a streamed data chunk.
 */
static u32 crc32_update(u32 crc, const u8 data[], size_t data_length) {
  if (!crc_table_initialized) {
    populate_crc_table();
    crc_table_initialized = 1;
  }

  crc ^= 0xFFFFFFFFu;

  for (size_t i = 0; i < data_length; i++) {
    crc ^= data[i];
    crc = (crc >> 8u) ^ crc_table[crc & 0xFFu];
  }

  return crc ^ 0xFFFFFFFFu;
}

/*
 * Check a single supported asset record against its stored CRC-32.
 */
static bun_result_t crc_check(BunParseContext *ctx,
                              const BunHeader *header,
                              const BunAssetRecord *record,
                              u64 record_offset) {
  bun_result_t code = BUN_OK;
  BunRleState state = {0, 0};
  u8 raw_buf[2048];
  u8 clean_buf[2048];
  size_t in_avail = 0;
  size_t in_pos = 0;
  u64 data_start = 0;
  u64 expected_data_size = record->uncompressed_size == 0 ? record->data_size : record->uncompressed_size;
  u64 total_bytes_read = 0;
  u64 remaining = record->data_size;
  u32 running_crc = 0;

  if (record->checksum == 0u) {
    return BUN_OK;
  }

  if (record->compression == BUN_COMPRESSION_RLE) {
    code = validate_rle_data(ctx, header, record, record_offset);
    if (code != BUN_OK) {
      return code;
    }
  }

  if (!add_u64_checked(header->data_section_offset, record->data_offset, &data_start)) {
    return fail_at(ctx,
                   BUN_ERR_OVERFLOW,
                   "record data start offset overflows 64-bit arithmetic",
                   record_offset + BUN_RECORD_DATA_OFFSET_OFFSET);
  }

  code = seek_to_u64(ctx->file, data_start);
  if (code != BUN_OK) {
    return fail_at(ctx,
                   code,
                   "failed to seek to asset data",
                   data_start);
  }

  while (total_bytes_read < expected_data_size) {
    size_t consumed = 0;
    size_t written = 0;

    if (in_pos == in_avail) {
      size_t chunk = remaining < sizeof(raw_buf) ? (size_t)remaining : sizeof(raw_buf);

      if (chunk == 0) {
        return fail_at(ctx,
                       BUN_MALFORMED,
                       "asset data ended before checksum input was complete",
                       data_start + (record->data_size - remaining));
      }

      code = read_exact(ctx->file, raw_buf, chunk);
      if (code != BUN_OK) {
        return fail_at(ctx,
                       code,
                       "failed to read asset data",
                       data_start + (record->data_size - remaining));
      }

      if (record->compression == BUN_COMPRESSION_RLE && chunk % 2 != 0) {
        return fail_at(ctx,
                       BUN_MALFORMED,
                       "RLE compressed data size is not even",
                       data_start + (record->data_size - remaining));
      }

      remaining -= chunk;
      in_pos = 0;
      in_avail = chunk;
    }

    switch (record->compression) {
    case BUN_COMPRESSION_NONE: {
      size_t available = in_avail - in_pos;
      memcpy(clean_buf, raw_buf, available);
      consumed = available;
      written = available;
      break;
    }
    case BUN_COMPRESSION_RLE:
      decompress_rle(&state,
                     raw_buf + in_pos,
                     in_avail - in_pos,
                     clean_buf,
                     sizeof(clean_buf),
                     &consumed,
                     &written);
      break;
    case BUN_COMPRESSION_ZLIB:
      return fail_at(ctx,
                     BUN_UNSUPPORTED,
                     "zlib-compressed assets are not supported yet",
                     record_offset + BUN_RECORD_COMPRESSION_OFFSET);
    default:
      return fail_at(ctx,
                     BUN_UNSUPPORTED,
                     "unknown compression type is not supported",
                     record_offset + BUN_RECORD_COMPRESSION_OFFSET);
    }

    in_pos += consumed;
    total_bytes_read += written;
    running_crc = crc32_update(running_crc, clean_buf, written);
  }

  if (state.active_count != 0 || remaining != 0) {
    return fail_with(ctx,
                     BUN_MALFORMED,
                     "asset data size does not match checksum input size");
  }

  if (record->checksum != running_crc) {
    return fail_at(ctx,
                   BUN_MALFORMED,
                   "CRC check failed",
                   record_offset + BUN_RECORD_CHECKSUM_OFFSET);
  }

  return BUN_OK;
}

//
// API implementation
//

bun_result_t bun_open(const char *path, BunParseContext *ctx) {
  // we open the file; seek to the end, to get the size; then jump back to the
  // beginning, ready to start parsing.

  clear_error_state(ctx);
  reset_parsed_assets(ctx);
  ctx->file = fopen(path, "rb");
  if (!ctx->file) {
    return fail_with(ctx, BUN_ERR_IO, "could not open input file");
  }

  if (fseek(ctx->file, 0, SEEK_END) != 0) {
    fclose(ctx->file);
    ctx->file = NULL;
    return fail_with(ctx, BUN_ERR_IO, "could not seek to end of input file");
  }

  ctx->file_size = ftell(ctx->file);
  if (ctx->file_size < 0) {
    fclose(ctx->file);
    ctx->file = NULL;
    return fail_with(ctx, BUN_ERR_TOOBIG, "could not determine input file size");
  }

  // NOTE: We replace rewind() with a checked fseek so failures are not silently
  if (fseek(ctx->file, 0, SEEK_SET) != 0) {
    fclose(ctx->file);
    ctx->file = NULL;
    return fail_with(ctx, BUN_ERR_IO, "could not seek to beginning of input file");
  }

  return BUN_OK;
}

bun_result_t bun_parse_header(BunParseContext *ctx, BunHeader *header) {
  u8 buf[BUN_HEADER_SIZE];

  clear_error_state(ctx);

  // Our file is far too short, and cannot be valid.
  if (ctx->file_size < (long)BUN_HEADER_SIZE) {
    return fail_at(ctx,
                   BUN_MALFORMED,
                   "file ends before the full BUN header",
                   ctx_file_size_u64(ctx));
  }

  // slurp the header into `buf`
  bun_result_t result = read_exact(ctx->file, buf, BUN_HEADER_SIZE);
  if (result != BUN_OK) {
    return fail_at(ctx, result, "failed to read BUN header", 0u);
  }

  // Decode the fixed-size header directly from the on-disk byte buffer.
  header->magic                = read_u32_le(buf, 0);
  header->version_major        = read_u16_le(buf, 4);
  header->version_minor        = read_u16_le(buf, 6);
  header->asset_count          = read_u32_le(buf, 8);
  header->asset_table_offset   = read_u64_le(buf, 12);
  header->string_table_offset  = read_u64_le(buf, 20);
  header->string_table_size    = read_u64_le(buf, 28);
  header->data_section_offset  = read_u64_le(buf, 36);
  header->data_section_size    = read_u64_le(buf, 44);
  header->reserved             = read_u64_le(buf, 52);

  // The magic must match exactly for the file to be a BUN container.
  if (header->magic != BUN_MAGIC) {
    return fail_at(ctx,
                   BUN_MALFORMED,
                   "invalid BUN magic value",
                   BUN_HEADER_MAGIC_OFFSET);
  }

  // Reject misaligned sections before attempting any deeper parsing.
  if (header->asset_table_offset % 4 != 0) {
    return fail_at(ctx,
                   BUN_MALFORMED,
                   "asset table offset must be 4-byte aligned",
                   BUN_HEADER_ASSET_TABLE_OFFSET);
  }
  if (header->string_table_offset % 4 != 0) {
    return fail_at(ctx,
                   BUN_MALFORMED,
                   "string table offset must be 4-byte aligned",
                   BUN_HEADER_STRING_TABLE_OFFSET);
  }
  if (header->data_section_offset % 4 != 0) {
    return fail_at(ctx,
                   BUN_MALFORMED,
                   "data section offset must be 4-byte aligned",
                   BUN_HEADER_DATA_SECTION_OFFSET);
  }
  if (header->string_table_size % 4 != 0) {
    return fail_at(ctx,
                   BUN_MALFORMED,
                   "string table size must be 4-byte aligned",
                   BUN_HEADER_STRING_TABLE_SIZE_OFFSET);
  }
  if (header->data_section_size % 4 != 0) {
    return fail_at(ctx,
                   BUN_MALFORMED,
                   "data section size must be 4-byte aligned",
                   BUN_HEADER_DATA_SECTION_SIZE_OFFSET);
  }

  // Version Check
  if (header->version_major != 1) {
    return fail_at(ctx,
                   BUN_UNSUPPORTED,
                   "unsupported BUN major version",
                   BUN_HEADER_VERSION_MAJOR_OFFSET);
  }
  if (header->version_minor != 0) {
    return fail_at(ctx,
                   BUN_UNSUPPORTED,
                   "unsupported BUN minor version",
                   BUN_HEADER_VERSION_MINOR_OFFSET);
  }

  return BUN_OK;
}

bun_result_t bun_parse_assets(BunParseContext *ctx, const BunHeader *header) {
  BunSection header_section = {0, BUN_HEADER_SIZE};
  BunSection asset_section = {0, 0};
  BunSection string_section = {header->string_table_offset, header->string_table_size};
  BunSection data_section = {header->data_section_offset, header->data_section_size};
  u64 file_size = ctx_file_size_u64(ctx);
  u64 asset_table_size = 0;
  u32 idx = 0;
  int saw_unsupported = 0;
  const char *unsupported_detail = NULL;
  u64 unsupported_offset = 0;
  int unsupported_offset_valid = 0;

  bun_result_t result = BUN_OK;
  BunParsedAsset parsed_asset = {0};

  clear_error_state(ctx);

  if (!mul_u64_checked((u64)header->asset_count,
                       (u64)BUN_ASSET_RECORD_SIZE,
                       &asset_table_size)) {
    return fail_at(ctx,
                   BUN_ERR_OVERFLOW,
                   "asset table size overflows 64-bit arithmetic",
                   BUN_HEADER_ASSET_COUNT_OFFSET);
  }
  ctx->parsed_asset_count = 0;

  asset_section.offset = header->asset_table_offset;
  asset_section.size = asset_table_size;
  result = validate_section_layout(ctx,
                                   &header_section,
                                   &asset_section,
                                   &string_section,
                                   &data_section,
                                   file_size);
  if (result != BUN_OK) {
    return result;
  }

  for (idx = 0; idx < header->asset_count; idx++) {
    BunAssetRecord record = {0};
    u64 name_end = 0;
    u64 data_end = 0;
    u32 unsupported_flag_bits = 0;

    memset(&parsed_asset, 0, sizeof(parsed_asset));

    /*
     * Safe after `validate_section_layout()`: the full asset table has already
     * been range-checked against the file size.
     */
    u64 record_offset = header->asset_table_offset
                      + (u64)idx * (u64)BUN_ASSET_RECORD_SIZE;

    result = read_asset_record(ctx->file, record_offset, &record);
    if (result != BUN_OK) {
      return fail_at(ctx, result, "failed to read asset record", record_offset);
    }

    if (!add_u64_checked((u64)record.name_offset,
                         (u64)record.name_length,
                         &name_end)) {
      return fail_at(ctx,
                     BUN_ERR_OVERFLOW,
                     "asset name range overflows 64-bit arithmetic",
                     record_offset + BUN_RECORD_NAME_OFFSET_OFFSET);
    }
    if (name_end > header->string_table_size) {
      return fail_at(ctx,
                     BUN_MALFORMED,
                     "asset name range extends past the string table",
                     record_offset + BUN_RECORD_NAME_OFFSET_OFFSET);
    }

    if (!add_u64_checked(record.data_offset, record.data_size, &data_end)) {
      return fail_at(ctx,
                     BUN_ERR_OVERFLOW,
                     "asset data range overflows 64-bit arithmetic",
                     record_offset + BUN_RECORD_DATA_OFFSET_OFFSET);
    }
    if (data_end > header->data_section_size) {
      return fail_at(ctx,
                     BUN_MALFORMED,
                     "asset data range extends past the data section",
                     record_offset + BUN_RECORD_DATA_OFFSET_OFFSET);
    }

    parsed_asset.record = record;

    result = validate_asset_name(ctx, header, &record, record_offset, &parsed_asset);
    if (result != BUN_OK) {
      return result;
    }

    unsupported_flag_bits = record.flags & ~(BUN_FLAG_ENCRYPTED | BUN_FLAG_EXECUTABLE);
    if (unsupported_flag_bits != 0u) {
      saw_unsupported = 1;
      note_first_issue(&unsupported_detail,
                       &unsupported_offset,
                       &unsupported_offset_valid,
                       "asset uses unsupported flag bits",
                       record_offset + BUN_RECORD_FLAGS_OFFSET);
    }
    if (record.compression == BUN_COMPRESSION_NONE) {
      if (record.uncompressed_size != 0u) {
        return fail_at(ctx,
                       BUN_MALFORMED,
                       "uncompressed assets must store uncompressed_size as zero",
                       record_offset + BUN_RECORD_UNCOMPRESSED_SIZE_OFFSET);
      }
      result = crc_check(ctx, header, &record, record_offset);
      if (result != BUN_OK) {
        return result;
      }
      result = capture_raw_data_preview(ctx, header, &record, record_offset, &parsed_asset);
      if (result != BUN_OK) {
        return result;
      }
      ctx->parsed_asset_count++;
      if (ctx->asset_callback != NULL) {
        ctx->asset_callback(ctx, &parsed_asset, ctx->parsed_asset_count - 1);
      }
      continue;
    }

    if (record.compression == BUN_COMPRESSION_RLE) {
      result = record.checksum != 0u
             ? crc_check(ctx, header, &record, record_offset)
             : validate_rle_data(ctx, header, &record, record_offset);
      if (result != BUN_OK) {
        return result;
      }
      result = generate_rle_preview(ctx, header, &record, record_offset, &parsed_asset);
      if (result != BUN_OK) {
        return result;
      }
      ctx->parsed_asset_count++;
      if (ctx->asset_callback != NULL) {
        ctx->asset_callback(ctx, &parsed_asset, ctx->parsed_asset_count - 1);
      }
      continue;
    }

    if (record.compression == BUN_COMPRESSION_ZLIB) {
      result = capture_raw_data_preview(ctx, header, &record, record_offset, &parsed_asset);
      if (result != BUN_OK) {
        return result;
      }
      saw_unsupported = 1;
      note_first_issue(&unsupported_detail,
                       &unsupported_offset,
                       &unsupported_offset_valid,
                       "zlib-compressed assets are not supported yet",
                       record_offset + BUN_RECORD_COMPRESSION_OFFSET);
      ctx->parsed_asset_count++;
      if (ctx->asset_callback != NULL) {
        ctx->asset_callback(ctx, &parsed_asset, ctx->parsed_asset_count - 1);
      }
      continue;
    }

    result = capture_raw_data_preview(ctx, header, &record, record_offset, &parsed_asset);
    if (result != BUN_OK) {
      return result;
    }
    saw_unsupported = 1;
    note_first_issue(&unsupported_detail,
                     &unsupported_offset,
                     &unsupported_offset_valid,
                     "unknown compression type is not supported",
                     record_offset + BUN_RECORD_COMPRESSION_OFFSET);
    ctx->parsed_asset_count++;
    if (ctx->asset_callback != NULL) {
      ctx->asset_callback(ctx, &parsed_asset, ctx->parsed_asset_count - 1);
    }
  }

  if (saw_unsupported) {
    return unsupported_offset_valid
         ? fail_at(ctx, BUN_UNSUPPORTED, unsupported_detail, unsupported_offset)
         : fail_with(ctx, BUN_UNSUPPORTED, "file uses unsupported features");
  }

  return BUN_OK;
}

bun_result_t bun_close(BunParseContext *ctx) {
  assert(ctx->file);

  ctx->asset_callback = NULL;
  ctx->callback_userdata = NULL;
  int res = fclose(ctx->file);
  if (res) {
    return fail_with(ctx, BUN_ERR_IO, "failed to close input file");
  } else {
    ctx->file = NULL;
    clear_error_state(ctx);
    return BUN_OK;
  }
}
