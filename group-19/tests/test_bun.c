#include "../bun.h"
#include <check.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

// Helper: terminate abnormally, after printing a message to stderr
void die(const char *fmt, ...){

  va_list args;
  va_start(args, fmt);

  fprintf(stderr, "fatal error: ");
  vfprintf(stderr, fmt, args);
  fprintf(stderr, "\n");

  va_end(args);

  abort();
}


// Helper: open a test fixture by name, relative to the tests/ directory.
static const char *fixture(const char *filename) {
    // For simplicity, tests assume they are run from the project root, and
    // test BUN files live in tests/samples/{valid,invalid}. Adjust if needed.
    static char path[256];
    int res = snprintf(path, sizeof(path), "tests/samples/%s", filename);
    if (res < 0 ) {
      die("snprintf failed: %s, for %s", strerror(errno), filename);
    }
    if ((size_t) res > sizeof(path)) {
      die("filename '%s' too big for buffer (would write %d bytes to %zu-size buffer)",
          filename, res, sizeof(path));
    }
    return path;
}

typedef struct {
    int called;
    u32 asset_index;
    BunParsedAsset asset;
} AssetCapture;

static void capture_asset_cb(BunParseContext *ctx,
                             const BunParsedAsset *asset,
                             u32 asset_index) {
    AssetCapture *capture = (AssetCapture *)ctx->callback_userdata;

    if (capture != NULL) {
      capture->called++;
      capture->asset_index = asset_index;
      capture->asset = *asset;
    }
}

static u64 align4_u64(u64 value) {
    return (value + 3u) & ~3u;
}

static void make_temp_fixture_path(char *path, size_t path_size, const char *label) {
    static unsigned counter = 0;
    int res = snprintf(path,
                       path_size,
                       "/tmp/bun-test-%ld-%u-%s.bun",
                       (long)getpid(),
                       counter++,
                       label);

    if (res < 0) {
      die("snprintf failed while creating generated fixture path");
    }
    if ((size_t)res >= path_size) {
      die("generated fixture path too large");
    }
}

static void write_byte(FILE *file, u8 byte) {
    if (fputc((int)byte, file) == EOF) {
      die("failed to write generated fixture byte");
    }
}

static void write_le(FILE *file, u64 value, unsigned byte_count) {
    unsigned idx = 0;

    for (idx = 0; idx < byte_count; idx++) {
      write_byte(file, (u8)((value >> (idx * 8u)) & 0xffu));
    }
}

static void write_padding(FILE *file, u64 byte_count) {
    u64 idx = 0;

    for (idx = 0; idx < byte_count; idx++) {
      write_byte(file, 0u);
    }
}

static void write_bytes(FILE *file, const void *data, size_t size) {
    if (size == 0u) {
      return;
    }
    if (fwrite(data, 1, size, file) != size) {
      die("failed to write generated fixture bytes");
    }
}

static void seek_to(FILE *file, u64 offset) {
    if (fseek(file, (long)offset, SEEK_SET) != 0) {
      die("failed to seek in generated fixture");
    }
}

static void write_header(FILE *file,
                         u32 asset_count,
                         u64 asset_table_offset,
                         u64 string_table_offset,
                         u64 string_table_size,
                         u64 data_section_offset,
                         u64 data_section_size) {
    write_le(file, BUN_MAGIC, 4);
    write_le(file, BUN_VERSION_MAJOR, 2);
    write_le(file, BUN_VERSION_MINOR, 2);
    write_le(file, asset_count, 4);
    write_le(file, asset_table_offset, 8);
    write_le(file, string_table_offset, 8);
    write_le(file, string_table_size, 8);
    write_le(file, data_section_offset, 8);
    write_le(file, data_section_size, 8);
    write_le(file, 0u, 8);
}

static void write_asset_record(FILE *file,
                               u32 name_offset,
                               u32 name_length,
                               u64 data_offset,
                               u64 data_size,
                               u64 uncompressed_size,
                               u32 compression,
                               u32 type,
                               u32 checksum,
                               u32 flags) {
    write_le(file, name_offset, 4);
    write_le(file, name_length, 4);
    write_le(file, data_offset, 8);
    write_le(file, data_size, 8);
    write_le(file, uncompressed_size, 8);
    write_le(file, compression, 4);
    write_le(file, type, 4);
    write_le(file, checksum, 4);
    write_le(file, flags, 4);
}

static void write_generated_one_asset_bun(const char *path,
                                          const char *name,
                                          const u8 *data,
                                          size_t actual_data_size,
                                          u64 record_data_offset,
                                          u64 record_data_size,
                                          u64 uncompressed_size,
                                          u32 compression,
                                          u32 checksum,
                                          u32 flags) {
    u64 asset_table_offset = BUN_HEADER_SIZE;
    u64 string_table_offset = asset_table_offset + BUN_ASSET_RECORD_SIZE;
    u64 name_length = (u64)strlen(name);
    u64 string_table_size = align4_u64(name_length);
    u64 data_section_offset = align4_u64(string_table_offset + string_table_size);
    u64 data_section_size = align4_u64((u64)actual_data_size);
    FILE *file = fopen(path, "wb");

    if (file == NULL) {
      die("failed to create generated fixture '%s': %s", path, strerror(errno));
    }

    write_header(file,
                 1u,
                 asset_table_offset,
                 string_table_offset,
                 string_table_size,
                 data_section_offset,
                 data_section_size);

    seek_to(file, asset_table_offset);
    write_asset_record(file,
                       0u,
                       (u32)name_length,
                       record_data_offset,
                       record_data_size,
                       uncompressed_size,
                       compression,
                       1u,
                       checksum,
                       flags);

    seek_to(file, string_table_offset);
    write_bytes(file, name, (size_t)name_length);
    write_padding(file, string_table_size - name_length);

    seek_to(file, data_section_offset);
    write_bytes(file, data, actual_data_size);
    write_padding(file, data_section_size - (u64)actual_data_size);

    if (fclose(file) != 0) {
      die("failed to close generated fixture '%s': %s", path, strerror(errno));
    }
}

static void open_generated_fixture(const char *path,
                                   BunParseContext *ctx,
                                   BunHeader *header) {
    ck_assert_int_eq(bun_open(path, ctx), BUN_OK);
    ck_assert_int_eq(bun_parse_header(ctx, header), BUN_OK);
}

// Example test suite: header parsing

START_TEST(test_valid_minimal) {
    BunParseContext ctx = {0};
    BunHeader header = {0};

    ck_assert_int_eq(bun_open(fixture("valid/01-empty.bun"), &ctx), BUN_OK);
    ck_assert_int_eq(bun_parse_header(&ctx, &header), BUN_OK);
    ck_assert_uint_eq(header.magic, BUN_MAGIC);
    bun_close(&ctx);
} END_TEST

START_TEST(test_valid_alt_minimal){
    BunParseContext ctx = {0};
    BunHeader header    = {0};

    bun_result_t r = bun_open(fixture("valid/02-alt-empty.bun"), &ctx);
    ck_assert_int_eq(r, BUN_OK);

    r = bun_parse_header(&ctx, &header);
    ck_assert_int_eq(r, BUN_OK);
    ck_assert_uint_eq(header.magic, BUN_MAGIC);
    ck_assert_uint_eq(header.version_major, 1);
    ck_assert_uint_eq(header.version_minor, 0);

    bun_close(&ctx);
}END_TEST


START_TEST(test_valid_one_asset){
    BunParseContext ctx = {0};
    BunHeader header = {0};

    ck_assert_int_eq(bun_open(fixture("valid/03-one-asset.bun"), &ctx), BUN_OK);

    ck_assert_int_eq(bun_parse_header(&ctx, &header), BUN_OK);

    ck_assert_uint_eq(header.asset_count, 1);

    bun_close(&ctx);
}END_TEST


START_TEST(test_valid_binar_asset){
    BunParseContext ctx = {0};
    BunHeader header = {0};

    ck_assert_int_eq(bun_open(fixture("valid/04-binary-asset.bun"), &ctx), BUN_OK);
    ck_assert_int_eq(bun_parse_header(&ctx, &header), BUN_OK);

    ck_assert_uint_eq(header.magic, BUN_MAGIC);
    ck_assert_uint_gt(header.asset_count, 0);

    bun_close(&ctx);
}END_TEST


START_TEST(test_valid_multi_asset_stack){
    BunParseContext ctx = {0};
    BunHeader header = {0};

    ck_assert_int_eq(bun_open(fixture("valid/05-multi-assets-slack.bun"), &ctx), BUN_OK);
    ck_assert_int_eq(bun_parse_header(&ctx, &header), BUN_OK);

    ck_assert_uint_gt(header.asset_count, 1);

    bun_close(&ctx);
}END_TEST

START_TEST(test_valid_rle) {
    BunParseContext ctx = {0};
    BunHeader header = {0};

    ck_assert_int_eq(bun_open(fixture("valid/06-rle-valid.bun"), &ctx), BUN_OK);
    ck_assert_int_eq(bun_parse_header(&ctx, &header), BUN_OK);

    ck_assert_int_eq(bun_parse_assets(&ctx, &header), BUN_OK);
    bun_close(&ctx);
} END_TEST

START_TEST(test_valid_crc_no_compression) {
    BunParseContext ctx = {0};
    BunHeader header = {0};

    ck_assert_int_eq(bun_open(fixture("valid/07-raw-crc.bun"), &ctx), BUN_OK);
    ck_assert_int_eq(bun_parse_header(&ctx, &header), BUN_OK);

    ck_assert_int_eq(bun_parse_assets(&ctx, &header), BUN_OK);
    bun_close(&ctx);
} END_TEST

START_TEST(test_valid_crc_rle) {
    BunParseContext ctx = {0};
    BunHeader header = {0};

    ck_assert_int_eq(bun_open(fixture("valid/08-rle-crc.bun"), &ctx), BUN_OK);
    ck_assert_int_eq(bun_parse_header(&ctx, &header), BUN_OK);

    ck_assert_int_eq(bun_parse_assets(&ctx, &header), BUN_OK);
    bun_close(&ctx);
} END_TEST

START_TEST(test_valid_crc_big_rle_file) {
    BunParseContext ctx = {0};
    BunHeader header = {0};

    ck_assert_int_eq(bun_open(fixture("valid/09-big-rle-crc.bun"), &ctx), BUN_OK);
    ck_assert_int_eq(bun_parse_header(&ctx, &header), BUN_OK);

    ck_assert_int_eq(bun_parse_assets(&ctx, &header), BUN_OK);
    bun_close(&ctx);
} END_TEST

START_TEST(test_allowed_asset_flags) {
    char path[256];
    const u8 data[] = {'D', 'A', 'T', 'A'};
    BunParseContext ctx = {0};
    BunHeader header = {0};

    make_temp_fixture_path(path, sizeof(path), "allowed-flags");
    write_generated_one_asset_bun(path,
                                  "flagged_asset",
                                  data,
                                  sizeof(data),
                                  0u,
                                  sizeof(data),
                                  0u,
                                  BUN_COMPRESSION_NONE,
                                  0u,
                                  BUN_FLAG_ENCRYPTED | BUN_FLAG_EXECUTABLE);

    open_generated_fixture(path, &ctx, &header);
    ck_assert_int_eq(bun_parse_assets(&ctx, &header), BUN_OK);
    ck_assert_uint_eq(ctx.parsed_asset_count, 1u);

    bun_close(&ctx);
    remove(path);
} END_TEST

START_TEST(test_long_asset_name_preview_truncates) {
    char path[256];
    const char *long_name =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789LONGNAME";
    const u8 data[] = {'D', 'A', 'T', 'A'};
    BunParseContext ctx = {0};
    BunHeader header = {0};
    AssetCapture capture = {0};

    make_temp_fixture_path(path, sizeof(path), "long-name");
    write_generated_one_asset_bun(path,
                                  long_name,
                                  data,
                                  sizeof(data),
                                  0u,
                                  sizeof(data),
                                  0u,
                                  BUN_COMPRESSION_NONE,
                                  0u,
                                  0u);

    open_generated_fixture(path, &ctx, &header);
    ctx.asset_callback = capture_asset_cb;
    ctx.callback_userdata = &capture;

    ck_assert_int_eq(bun_parse_assets(&ctx, &header), BUN_OK);
    ck_assert_int_eq(capture.called, 1);
    ck_assert_uint_eq(capture.asset_index, 0u);
    ck_assert_uint_eq(capture.asset.name_prefix_length, BUN_NAME_PREFIX_MAX);
    ck_assert_int_eq(capture.asset.name_truncated, 1);
    ck_assert_int_eq(capture.asset.name_prefix[BUN_NAME_PREFIX_MAX], '\0');
    ck_assert_int_eq(memcmp(capture.asset.name_prefix,
                            long_name,
                            BUN_NAME_PREFIX_MAX),
                     0);

    bun_close(&ctx);
    remove(path);
} END_TEST

/*******************************************************************/
/*******************************************************************/

START_TEST(test_bad_magic) {
    BunParseContext ctx = {0};
    BunHeader header = {0};

    ck_assert_int_eq(bun_open(fixture("invalid/01-bad-magic.bun"), &ctx), BUN_OK);
    ck_assert_int_eq(bun_parse_header(&ctx, &header), BUN_MALFORMED);
    bun_close(&ctx);
} END_TEST

START_TEST(test_bad_version) {
    BunParseContext ctx = {0};
    BunHeader header = {0};

    ck_assert_int_eq(bun_open(fixture("invalid/02-bad-version.bun"), &ctx), BUN_OK);
    ck_assert_int_eq(bun_parse_header(&ctx, &header), BUN_UNSUPPORTED);
    bun_close(&ctx);
} END_TEST

START_TEST(test_bad_offset_alignment) {
    BunParseContext ctx = {0};
    BunHeader header = {0};

    ck_assert_int_eq(bun_open(fixture("invalid/03-bad-offset-alignment.bun"), &ctx), BUN_OK);
    ck_assert_int_eq(bun_parse_header(&ctx, &header), BUN_MALFORMED);
    bun_close(&ctx);
} END_TEST



START_TEST(test_bad_section_past_eof) {
    BunParseContext ctx = {0};
    BunHeader header = {0};
    ck_assert_int_eq(bun_open(fixture("invalid/04-section-past-eof.bun"), &ctx), BUN_OK);
    // Header is structurally fine
    ck_assert_int_eq(bun_parse_header(&ctx, &header), BUN_OK);
    // Logic error caught here
    ck_assert_int_eq(bun_parse_assets(&ctx, &header), BUN_MALFORMED);
    bun_close(&ctx);
} END_TEST

START_TEST(test_bad_overlapping_sections) {
    BunParseContext ctx = {0};
    BunHeader header = {0};
    ck_assert_int_eq(bun_open(fixture("invalid/05-overlapping-sections.bun"), &ctx), BUN_OK);
    ck_assert_int_eq(bun_parse_header(&ctx, &header), BUN_OK);
    ck_assert_int_eq(bun_parse_assets(&ctx, &header), BUN_MALFORMED);
    bun_close(&ctx);
} END_TEST

START_TEST(test_bad_asset_name_past_string_table) {
    BunParseContext ctx = {0};
    BunHeader header = {0};

    bun_open(fixture("invalid/06-asset-name-past-string-table.bun"), &ctx);

    bun_parse_header(&ctx, &header);

    ck_assert_int_eq(bun_parse_assets(&ctx, &header), BUN_MALFORMED);
    bun_close(&ctx);
}
END_TEST


START_TEST(test_bad_asset_name_nonprintable) {
    BunParseContext ctx = {0};
    BunHeader header = {0};
    ck_assert_int_eq(bun_open(fixture("invalid/07-asset-name-nonprintable.bun"), &ctx), BUN_OK);
    ck_assert_int_eq(bun_parse_header(&ctx, &header), BUN_OK);
    ck_assert_int_eq(bun_parse_assets(&ctx, &header), BUN_MALFORMED);
    bun_close(&ctx);
} END_TEST


START_TEST(test_bad_truncated_file) {
    BunParseContext ctx = {0};
    BunHeader header = {0};

    bun_open(fixture("invalid/08-truncated-file.bun"), &ctx);
    ck_assert_int_eq(bun_parse_header(&ctx, &header), BUN_MALFORMED);
    bun_close(&ctx);
}
END_TEST

START_TEST(test_bad_misaligned_section_size) {
    BunParseContext ctx = {0};
    BunHeader header = {0};

    bun_open(fixture("invalid/09-misaligned-section-size.bun"), &ctx);
    ck_assert_int_eq(bun_parse_header(&ctx, &header), BUN_MALFORMED);
    bun_close(&ctx);
}
END_TEST

START_TEST(test_bad_overlapping_with_nonprintable) {
    BunParseContext ctx = {0};
    BunHeader header = {0};

    bun_open(fixture("invalid/10-overlapping-with-nonprintable.bun"), &ctx);

    bun_parse_header(&ctx, &header);

    ck_assert_int_eq(bun_parse_assets(&ctx, &header), BUN_MALFORMED);
    bun_close(&ctx);
}
END_TEST

START_TEST(test_bad_second_asset_empty_name) {
    BunParseContext ctx = {0};
    BunHeader header = {0};

    bun_open(fixture("invalid/11-second-asset-empty-name.bun"), &ctx);

    bun_parse_header(&ctx, &header);

    ck_assert_int_eq(bun_parse_assets(&ctx, &header), BUN_MALFORMED);
    bun_close(&ctx);
}
END_TEST

START_TEST(test_bad_asset_name_oob) {
    BunParseContext ctx = {0};
    BunHeader header = {0};

    bun_open(fixture("invalid/12-asset-name-oob.bun"), &ctx);

    bun_parse_header(&ctx, &header);

    ck_assert_int_eq(bun_parse_assets(&ctx, &header), BUN_MALFORMED);
    bun_close(&ctx);
}
END_TEST

START_TEST(test_bad_asset_empty_name) {
    BunParseContext ctx = {0};
    BunHeader header = {0};

    bun_open(fixture("invalid/13-asset-empty-name.bun"), &ctx);

    bun_parse_header(&ctx, &header);

    ck_assert_int_eq(bun_parse_assets(&ctx, &header), BUN_MALFORMED);
    bun_close(&ctx);
}
END_TEST

START_TEST(test_bad_rle_zero_count) {
    BunParseContext ctx = {0};
    BunHeader header = {0};

    ck_assert_int_eq(bun_open(fixture("invalid/14-rle-zero-count.bun"), &ctx), BUN_OK);
    ck_assert_int_eq(bun_parse_header(&ctx, &header), BUN_OK);
    ck_assert_int_eq(bun_parse_assets(&ctx, &header), BUN_MALFORMED);
    bun_close(&ctx);
} END_TEST


START_TEST(test_bad_rle_bomb) {
    BunParseContext ctx = {0};
    BunHeader header = {0};

    ck_assert_int_eq(bun_open(fixture("invalid/15-rle-bomb.bun"), &ctx), BUN_OK);

    /* If header is malformed, that's still a valid rejection */
    ck_assert_int_eq(bun_parse_header(&ctx, &header), BUN_MALFORMED);

    bun_close(&ctx);
}
END_TEST

START_TEST(test_bad_rle_truncated) {
    BunParseContext ctx = {0};
    BunHeader header = {0};

    bun_open(fixture("invalid/16-rle-truncated.bun"), &ctx);
    ck_assert_int_eq(bun_parse_header(&ctx, &header), BUN_MALFORMED);
    bun_close(&ctx);
}
END_TEST

START_TEST(test_bad_crc_no_compression) {
    BunParseContext ctx = {0};
    BunHeader header = {0};

    bun_open(fixture("invalid/17-wrong-crc-raw.bun"), &ctx);
    ck_assert_int_eq(bun_parse_header(&ctx, &header), BUN_OK);
    ck_assert_int_eq(bun_parse_assets(&ctx, &header), BUN_MALFORMED);
    bun_close(&ctx);
} END_TEST

START_TEST(test_bad_crc_rle) {
    BunParseContext ctx = {0};
    BunHeader header = {0};

    bun_open(fixture("invalid/18-wrong-crc-rle.bun"), &ctx);
    ck_assert_int_eq(bun_parse_header(&ctx, &header), BUN_OK);
    ck_assert_int_eq(bun_parse_assets(&ctx, &header), BUN_MALFORMED);
    bun_close(&ctx);
} END_TEST

START_TEST(test_zlib_compression_is_unsupported) {
    char path[256];
    const u8 data[] = {'D', 'A', 'T', 'A'};
    BunParseContext ctx = {0};
    BunHeader header = {0};

    make_temp_fixture_path(path, sizeof(path), "zlib");
    write_generated_one_asset_bun(path,
                                  "zlib_asset",
                                  data,
                                  sizeof(data),
                                  0u,
                                  sizeof(data),
                                  sizeof(data),
                                  BUN_COMPRESSION_ZLIB,
                                  0u,
                                  0u);

    open_generated_fixture(path, &ctx, &header);
    ck_assert_int_eq(bun_parse_assets(&ctx, &header), BUN_UNSUPPORTED);

    bun_close(&ctx);
    remove(path);
} END_TEST

START_TEST(test_zlib_with_checksum_is_unsupported) {
    char path[256];
    const u8 data[] = {'D', 'A', 'T', 'A'};
    BunParseContext ctx = {0};
    BunHeader header = {0};

    make_temp_fixture_path(path, sizeof(path), "zlib-checksum");
    write_generated_one_asset_bun(path,
                                  "zlib_checksum_asset",
                                  data,
                                  sizeof(data),
                                  0u,
                                  sizeof(data),
                                  sizeof(data),
                                  BUN_COMPRESSION_ZLIB,
                                  1u,
                                  0u);

    open_generated_fixture(path, &ctx, &header);
    ck_assert_int_eq(bun_parse_assets(&ctx, &header), BUN_UNSUPPORTED);

    bun_close(&ctx);
    remove(path);
} END_TEST

START_TEST(test_unknown_compression_is_unsupported) {
    char path[256];
    const u8 data[] = {'D', 'A', 'T', 'A'};
    BunParseContext ctx = {0};
    BunHeader header = {0};

    make_temp_fixture_path(path, sizeof(path), "unknown-compression");
    write_generated_one_asset_bun(path,
                                  "unknown_compression_asset",
                                  data,
                                  sizeof(data),
                                  0u,
                                  sizeof(data),
                                  sizeof(data),
                                  99u,
                                  0u,
                                  0u);

    open_generated_fixture(path, &ctx, &header);
    ck_assert_int_eq(bun_parse_assets(&ctx, &header), BUN_UNSUPPORTED);

    bun_close(&ctx);
    remove(path);
} END_TEST

START_TEST(test_unknown_compression_with_checksum_is_unsupported) {
    char path[256];
    const u8 data[] = {'D', 'A', 'T', 'A'};
    BunParseContext ctx = {0};
    BunHeader header = {0};

    make_temp_fixture_path(path, sizeof(path), "unknown-compression-checksum");
    write_generated_one_asset_bun(path,
                                  "unknown_compression_checksum_asset",
                                  data,
                                  sizeof(data),
                                  0u,
                                  sizeof(data),
                                  sizeof(data),
                                  99u,
                                  1u,
                                  0u);

    open_generated_fixture(path, &ctx, &header);
    ck_assert_int_eq(bun_parse_assets(&ctx, &header), BUN_UNSUPPORTED);

    bun_close(&ctx);
    remove(path);
} END_TEST

START_TEST(test_unknown_flag_bits_are_unsupported) {
    char path[256];
    const u8 data[] = {'D', 'A', 'T', 'A'};
    BunParseContext ctx = {0};
    BunHeader header = {0};

    make_temp_fixture_path(path, sizeof(path), "unknown-flags");
    write_generated_one_asset_bun(path,
                                  "unknown_flags_asset",
                                  data,
                                  sizeof(data),
                                  0u,
                                  sizeof(data),
                                  0u,
                                  BUN_COMPRESSION_NONE,
                                  0u,
                                  0x4u);

    open_generated_fixture(path, &ctx, &header);
    ck_assert_int_eq(bun_parse_assets(&ctx, &header), BUN_UNSUPPORTED);

    bun_close(&ctx);
    remove(path);
} END_TEST

START_TEST(test_asset_data_range_overflow) {
    char path[256];
    const u8 data[] = {'D', 'A', 'T', 'A'};
    BunParseContext ctx = {0};
    BunHeader header = {0};

    make_temp_fixture_path(path, sizeof(path), "data-overflow");
    write_generated_one_asset_bun(path,
                                  "overflow_asset",
                                  data,
                                  sizeof(data),
                                  UINT64_MAX - 1u,
                                  8u,
                                  0u,
                                  BUN_COMPRESSION_NONE,
                                  0u,
                                  0u);

    open_generated_fixture(path, &ctx, &header);
    ck_assert_int_eq(bun_parse_assets(&ctx, &header), BUN_ERR_OVERFLOW);

    bun_close(&ctx);
    remove(path);
} END_TEST

START_TEST(test_uncompressed_asset_rejects_uncompressed_size) {
    char path[256];
    const u8 data[] = {'D', 'A', 'T', 'A'};
    BunParseContext ctx = {0};
    BunHeader header = {0};

    make_temp_fixture_path(path, sizeof(path), "raw-uncompressed-size");
    write_generated_one_asset_bun(path,
                                  "raw_size_asset",
                                  data,
                                  sizeof(data),
                                  0u,
                                  sizeof(data),
                                  sizeof(data),
                                  BUN_COMPRESSION_NONE,
                                  0u,
                                  0u);

    open_generated_fixture(path, &ctx, &header);
    ck_assert_int_eq(bun_parse_assets(&ctx, &header), BUN_MALFORMED);

    bun_close(&ctx);
    remove(path);
} END_TEST



// Assemble a test suite from our tests
static Suite *bun_suite(void) {
    Suite *s = suite_create("\n\nBUN-Parser-Security-Gauntlet");

    /* 1. POSITIVE TESTS: Files that should work perfectly */
    TCase *tc_valid = tcase_create("\n\nValid-Files");
    tcase_add_test(tc_valid, test_valid_minimal);
    tcase_add_test(tc_valid, test_valid_alt_minimal);
    tcase_add_test(tc_valid, test_valid_one_asset);
    tcase_add_test(tc_valid, test_valid_binar_asset);
    tcase_add_test(tc_valid, test_valid_multi_asset_stack);
    tcase_add_test(tc_valid, test_valid_rle);
    tcase_add_test(tc_valid, test_valid_crc_no_compression);
    tcase_add_test(tc_valid, test_valid_crc_rle);
    tcase_add_test(tc_valid, test_valid_crc_big_rle_file);
    tcase_add_test(tc_valid, test_allowed_asset_flags);
    tcase_add_test(tc_valid, test_long_asset_name_preview_truncates);
    suite_add_tcase(s, tc_valid);

    /* 2. STRUCTURAL CHECKS: Magic numbers, versions, and alignment */
    TCase *tc_structure = tcase_create("\n\nStructure-and-Alignment");
    tcase_add_test(tc_structure, test_bad_magic);
    tcase_add_test(tc_structure, test_bad_version);
    tcase_add_test(tc_structure, test_bad_offset_alignment);
    tcase_add_test(tc_structure, test_bad_misaligned_section_size);
    suite_add_tcase(s, tc_structure);

    /* 3. BOUNDARY & SECURITY: Overlaps and EOF violations */
    TCase *tc_security = tcase_create("\n\nMemory-Boundary-Safety");
    tcase_add_test(tc_security, test_bad_section_past_eof);
    tcase_add_test(tc_security, test_bad_overlapping_sections);
    tcase_add_test(tc_security, test_bad_truncated_file);
    suite_add_tcase(s, tc_security);

    /* 4. STRING TABLE: Name validation and pointer logic */
    TCase *tc_strings = tcase_create("\n\nString-Table-Logic");
    tcase_add_test(tc_strings, test_bad_asset_name_past_string_table);
    tcase_add_test(tc_strings, test_bad_asset_name_nonprintable);
    tcase_add_test(tc_strings, test_bad_overlapping_with_nonprintable);
    tcase_add_test(tc_strings, test_bad_second_asset_empty_name);
    tcase_add_test(tc_strings, test_bad_asset_name_oob);
    tcase_add_test(tc_strings, test_bad_asset_empty_name);
    suite_add_tcase(s, tc_strings);

    /* 5. COMPRESSION: RLE specifics and decompression bombs */
    TCase *tc_compression = tcase_create("\n\nCompression-Hardening");
    tcase_add_test(tc_compression, test_bad_rle_zero_count);
    tcase_add_test(tc_compression, test_bad_rle_bomb);
    tcase_add_test(tc_compression, test_bad_rle_truncated);
    suite_add_tcase(s, tc_compression);

    /* 6. OPTIONAL FEATURES: Unsupported extensions fail closed */
    TCase *tc_unsupported = tcase_create("\n\nUnsupported-Features");
    tcase_add_test(tc_unsupported, test_zlib_compression_is_unsupported);
    tcase_add_test(tc_unsupported, test_zlib_with_checksum_is_unsupported);
    tcase_add_test(tc_unsupported, test_unknown_compression_is_unsupported);
    tcase_add_test(tc_unsupported, test_unknown_compression_with_checksum_is_unsupported);
    tcase_add_test(tc_unsupported, test_unknown_flag_bits_are_unsupported);
    suite_add_tcase(s, tc_unsupported);

    /* 7. GENERATED BOUNDARIES: Arithmetic and format edge cases */
    TCase *tc_generated_boundaries = tcase_create("\n\nGenerated-Boundaries");
    tcase_add_test(tc_generated_boundaries, test_asset_data_range_overflow);
    tcase_add_test(tc_generated_boundaries, test_uncompressed_asset_rejects_uncompressed_size);
    suite_add_tcase(s, tc_generated_boundaries);

    /* 8. INVALID CHECKSUM: Wrong CRC-32 checksum values */
    TCase *tc_checksum = tcase_create("\n\nInvalid-Checksum");
    tcase_add_test(tc_checksum, test_bad_crc_no_compression);
    tcase_add_test(tc_checksum, test_bad_crc_rle);
    suite_add_tcase(s, tc_checksum);

    return s;
}

int main(void) {
    Suite   *s  = bun_suite();
    SRunner *sr = srunner_create(s);

    // Set to CK_VERBOSE to see categories passing/failing
    srunner_run_all(sr, CK_VERBOSE);

    int failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
