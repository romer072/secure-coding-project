#include "../bun.h"
#include <check.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>

// Helper: terminate abnormally, after printing a message to stderr
void die(const char *fmt, ...)
{
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
    // Tests assume they are run from the project root, and
    // test BUN files live in tests/fixtures/{valid,invalid}.
    static char path[256];
    int res = snprintf(path, sizeof(path), "tests/samples/%s", filename);
    if (res < 0) {
      die("snprintf failed: %s", strerror(errno));
    }
    if ((size_t) res > sizeof(path)) {
      die("filename '%s' too big for buffer (would write %d bytes to %zu-size buffer)",
          filename, res, sizeof(path));
    }
    return path;
}

/* =========================================================
   TCASE 1: header-tests
   Tests that the header parser correctly reads and validates
   the BUN file header fields.
   ========================================================= */

// A valid empty BUN file (no assets) should parse without errors.
// Checks magic, version major, and version minor are all correct.
START_TEST(test_valid_minimal) {
    BunParseContext ctx = {0};
    BunHeader header    = {0};

    bun_result_t r = bun_open(fixture("valid/01-empty.bun"), &ctx);
    ck_assert_int_eq(r, BUN_OK);

    r = bun_parse_header(&ctx, &header);
    ck_assert_int_eq(r, BUN_OK);
    ck_assert_uint_eq(header.magic, BUN_MAGIC);
    ck_assert_uint_eq(header.version_major, 1);
    ck_assert_uint_eq(header.version_minor, 0);

    bun_close(&ctx);
}
END_TEST

// A second valid empty file with a different layout (alt-empty).
// Confirms the parser handles alternate-layout valid files correctly.
START_TEST(test_valid_alt_empty) {
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
}
END_TEST

// A file with wrong magic bytes should be rejected as malformed.
// The magic number is the first check — if it fails, nothing else matters.
START_TEST(test_bad_magic) {
    BunParseContext ctx = {0};
    BunHeader header    = {0};

    bun_result_t r = bun_open(fixture("invalid/01-bad-magic.bun"), &ctx);
    ck_assert_int_eq(r, BUN_OK);

    r = bun_parse_header(&ctx, &header);
    ck_assert_int_eq(r, BUN_MALFORMED);

    bun_close(&ctx);
}
END_TEST

// A file with version != 1.0 should be rejected as unsupported.
// We only support version 1.0; anything else returns BUN_UNSUPPORTED.
START_TEST(test_unsupported_version) {
    BunParseContext ctx = {0};
    BunHeader header    = {0};

    bun_result_t r = bun_open(fixture("invalid/02-bad-version.bun"), &ctx);
    ck_assert_int_eq(r, BUN_OK);

    r = bun_parse_header(&ctx, &header);
    ck_assert_int_eq(r, BUN_UNSUPPORTED);

    bun_close(&ctx);
}
END_TEST

// Offsets/sizes that are not divisible by 4 should be rejected.
// The spec (section 4.1) requires all offsets and sizes to be 4-byte aligned.
START_TEST(test_bad_offset_alignment) {
    BunParseContext ctx = {0};
    BunHeader header    = {0};

    bun_result_t r = bun_open(fixture("invalid/03-bad-offset-alignment.bun"), &ctx);
    ck_assert_int_eq(r, BUN_OK);

    r = bun_parse_header(&ctx, &header);
    ck_assert_int_eq(r, BUN_MALFORMED);

    bun_close(&ctx);
}
END_TEST

// A header that declares a section starting past the end of the file
// should be rejected — sections must lie entirely within the file.
START_TEST(test_section_past_eof) {
    BunParseContext ctx = {0};
    BunHeader header    = {0};

    bun_result_t r = bun_open(fixture("invalid/04-section-past-eof.bun"), &ctx);
    ck_assert_int_eq(r, BUN_OK);

    r = bun_parse_header(&ctx, &header);
    ck_assert_int_eq(r, BUN_MALFORMED);

    bun_close(&ctx);
}
END_TEST

// Two sections that overlap each other should be rejected.
// The spec says no two sections may share any bytes.
START_TEST(test_overlapping_sections) {
    BunParseContext ctx = {0};
    BunHeader header    = {0};

    bun_result_t r = bun_open(fixture("invalid/05-overlapping-sections.bun"), &ctx);
    ck_assert_int_eq(r, BUN_OK);

    r = bun_parse_header(&ctx, &header);
    ck_assert_int_eq(r, BUN_MALFORMED);

    bun_close(&ctx);
}
END_TEST

// A section size that is not divisible by 4 should be rejected.
// Catches misaligned string_table_size or data_section_size.
START_TEST(test_misaligned_section_size) {
    BunParseContext ctx = {0};
    BunHeader header    = {0};

    bun_result_t r = bun_open(fixture("invalid/09-misaligned-section-size.bun"), &ctx);
    ck_assert_int_eq(r, BUN_OK);

    r = bun_parse_header(&ctx, &header);
    ck_assert_int_eq(r, BUN_MALFORMED);

    bun_close(&ctx);
}
END_TEST

// A file that is too short to even contain a header should be malformed.
// BUN_HEADER_SIZE is 60 bytes; anything shorter cannot be valid.
START_TEST(test_truncated_file) {
    BunParseContext ctx = {0};
    BunHeader header    = {0};

    bun_result_t r = bun_open(fixture("invalid/08-truncated-file.bun"), &ctx);
    ck_assert_int_eq(r, BUN_OK);

    r = bun_parse_header(&ctx, &header);
    ck_assert_int_eq(r, BUN_MALFORMED);

    bun_close(&ctx);
}
END_TEST

// Opening a file that does not exist should return an I/O error.
START_TEST(test_open_nonexistent_file) {
    BunParseContext ctx = {0};

    bun_result_t r = bun_open(fixture("valid/does-not-exist.bun"), &ctx);
    ck_assert_int_eq(r, BUN_ERR_IO);
}
END_TEST


/* =========================================================
   TCASE 2: asset-tests
   Tests that bun_parse_assets() correctly reads and validates
   asset records, names, and data references.
   ========================================================= */

// A valid file with one uncompressed text asset should parse fully.
// Covers the basic happy path for asset parsing.
START_TEST(test_valid_one_asset) {
    BunParseContext ctx = {0};
    BunHeader header    = {0};

    bun_result_t r = bun_open(fixture("valid/03-one-asset.bun"), &ctx);
    ck_assert_int_eq(r, BUN_OK);

    r = bun_parse_header(&ctx, &header);
    ck_assert_int_eq(r, BUN_OK);

    r = bun_parse_assets(&ctx, &header);
    ck_assert_int_eq(r, BUN_OK);

    bun_close(&ctx);
}
END_TEST

// A valid file with a binary (non-text) asset should also parse correctly.
START_TEST(test_valid_binary_asset) {
    BunParseContext ctx = {0};
    BunHeader header    = {0};

    bun_result_t r = bun_open(fixture("valid/04-binary-asset.bun"), &ctx);
    ck_assert_int_eq(r, BUN_OK);

    r = bun_parse_header(&ctx, &header);
    ck_assert_int_eq(r, BUN_OK);

    r = bun_parse_assets(&ctx, &header);
    ck_assert_int_eq(r, BUN_OK);

    bun_close(&ctx);
}
END_TEST

// A valid file with multiple assets and slack (gap) space should parse correctly.
// Tests that gaps between sections don't confuse the parser.
START_TEST(test_valid_multi_assets_slack) {
    BunParseContext ctx = {0};
    BunHeader header    = {0};

    bun_result_t r = bun_open(fixture("valid/05-multi-assets-slack.bun"), &ctx);
    ck_assert_int_eq(r, BUN_OK);

    r = bun_parse_header(&ctx, &header);
    ck_assert_int_eq(r, BUN_OK);

    r = bun_parse_assets(&ctx, &header);
    ck_assert_int_eq(r, BUN_OK);

    bun_close(&ctx);
}
END_TEST

// An asset record whose name_offset+name_length points past the string table
// should be rejected as malformed (spec section 9, rule 5).
START_TEST(test_asset_name_past_string_table) {
    BunParseContext ctx = {0};
    BunHeader header    = {0};

    bun_result_t r = bun_open(fixture("invalid/06-asset-name-past-string-table.bun"), &ctx);
    ck_assert_int_eq(r, BUN_OK);

    r = bun_parse_header(&ctx, &header);
    ck_assert_int_eq(r, BUN_OK);

    r = bun_parse_assets(&ctx, &header);
    ck_assert_int_eq(r, BUN_MALFORMED);

    bun_close(&ctx);
}
END_TEST

// An asset whose name contains non-printable ASCII (outside 0x20–0x7E)
// must be rejected as malformed (spec section 5).
START_TEST(test_asset_name_nonprintable) {
    BunParseContext ctx = {0};
    BunHeader header    = {0};

    bun_result_t r = bun_open(fixture("invalid/07-asset-name-nonprintable.bun"), &ctx);
    ck_assert_int_eq(r, BUN_OK);

    r = bun_parse_header(&ctx, &header);
    ck_assert_int_eq(r, BUN_OK);

    r = bun_parse_assets(&ctx, &header);
    ck_assert_int_eq(r, BUN_MALFORMED);

    bun_close(&ctx);
}
END_TEST

// An asset with an out-of-bounds name reference (name_offset alone is past the
// string table) should be rejected.
START_TEST(test_asset_name_oob) {
    BunParseContext ctx = {0};
    BunHeader header    = {0};

    bun_result_t r = bun_open(fixture("invalid/12-asset-name-oob.bun"), &ctx);
    ck_assert_int_eq(r, BUN_OK);

    r = bun_parse_header(&ctx, &header);
    ck_assert_int_eq(r, BUN_OK);

    r = bun_parse_assets(&ctx, &header);
    ck_assert_int_eq(r, BUN_MALFORMED);

    bun_close(&ctx);
}
END_TEST

// An asset with a zero-length name (name_length == 0) must be rejected.
// The spec requires names to have a non-zero number of characters.
START_TEST(test_asset_empty_name) {
    BunParseContext ctx = {0};
    BunHeader header    = {0};

    bun_result_t r = bun_open(fixture("invalid/13-asset-empty-name.bun"), &ctx);
    ck_assert_int_eq(r, BUN_OK);

    r = bun_parse_header(&ctx, &header);
    ck_assert_int_eq(r, BUN_OK);

    r = bun_parse_assets(&ctx, &header);
    ck_assert_int_eq(r, BUN_MALFORMED);

    bun_close(&ctx);
}
END_TEST

// A file with two assets where the second one has an empty name.
// Makes sure we validate ALL asset records, not just the first one.
START_TEST(test_second_asset_empty_name) {
    BunParseContext ctx = {0};
    BunHeader header    = {0};

    bun_result_t r = bun_open(fixture("invalid/11-second-asset-empty-name.bun"), &ctx);
    ck_assert_int_eq(r, BUN_OK);

    r = bun_parse_header(&ctx, &header);
    ck_assert_int_eq(r, BUN_OK);

    r = bun_parse_assets(&ctx, &header);
    ck_assert_int_eq(r, BUN_MALFORMED);

    bun_close(&ctx);
}
END_TEST

// An asset in a file where sections overlap AND the name is non-printable.
// The overlap should have already been caught in the header stage.
START_TEST(test_overlapping_with_nonprintable) {
    BunParseContext ctx = {0};
    BunHeader header    = {0};

    bun_result_t r = bun_open(fixture("invalid/10-overlapping-with-nonprintable.bun"), &ctx);
    ck_assert_int_eq(r, BUN_OK);

    // Overlapping sections must be caught in the header stage
    r = bun_parse_header(&ctx, &header);
    ck_assert_int_eq(r, BUN_MALFORMED);

    bun_close(&ctx);
}
END_TEST


/* =========================================================
   TCASE 3: compression-tests
   Tests related to compression fields in asset records.
   ========================================================= */

// A valid RLE-compressed asset should parse correctly and return BUN_OK.
START_TEST(test_valid_rle) {
    BunParseContext ctx = {0};
    BunHeader header    = {0};

    bun_result_t r = bun_open(fixture("valid/06-rle-valid.bun"), &ctx);
    ck_assert_int_eq(r, BUN_OK);

    r = bun_parse_header(&ctx, &header);
    ck_assert_int_eq(r, BUN_OK);

    r = bun_parse_assets(&ctx, &header);
    ck_assert_int_eq(r, BUN_OK);

    bun_close(&ctx);
}
END_TEST

// An RLE-compressed asset where a (count, byte) pair has count == 0
// is malformed — a zero count pair is explicitly forbidden by the spec.
START_TEST(test_rle_zero_count) {
    BunParseContext ctx = {0};
    BunHeader header    = {0};

    bun_result_t r = bun_open(fixture("invalid/14-rle-zero-count.bun"), &ctx);
    ck_assert_int_eq(r, BUN_OK);

    r = bun_parse_header(&ctx, &header);
    ck_assert_int_eq(r, BUN_OK);

    r = bun_parse_assets(&ctx, &header);
    ck_assert_int_eq(r, BUN_MALFORMED);

    bun_close(&ctx);
}
END_TEST

// An RLE asset where the on-disk size is an odd number of bytes is malformed.
// RLE data must consist of whole (count, byte) pairs — always an even number of bytes.
START_TEST(test_rle_truncated) {
    BunParseContext ctx = {0};
    BunHeader header    = {0};

    bun_result_t r = bun_open(fixture("invalid/16-rle-truncated.bun"), &ctx);
    ck_assert_int_eq(r, BUN_OK);

    r = bun_parse_header(&ctx, &header);
    ck_assert_int_eq(r, BUN_OK);

    r = bun_parse_assets(&ctx, &header);
    ck_assert_int_eq(r, BUN_MALFORMED);

    bun_close(&ctx);
}
END_TEST

// An RLE asset where the uncompressed_size in the record doesn't match
// the actual decompressed size must be rejected as malformed (spec section 5.1 rule 4).
START_TEST(test_rle_bomb) {
    BunParseContext ctx = {0};
    BunHeader header    = {0};

    bun_result_t r = bun_open(fixture("invalid/15-rle-bomb.bun"), &ctx);
    ck_assert_int_eq(r, BUN_OK);

    r = bun_parse_header(&ctx, &header);
    ck_assert_int_eq(r, BUN_OK);

    r = bun_parse_assets(&ctx, &header);
    // uncompressed_size mismatch = BUN_MALFORMED
    ck_assert_int_eq(r, BUN_MALFORMED);

    bun_close(&ctx);
}
END_TEST


/* =========================================================
   Assemble test suites
   ========================================================= */

static Suite *bun_suite(void) {
    Suite *s = suite_create("bun-suite");

    // --- Header tests ---
    TCase *tc_header = tcase_create("header-tests");
    tcase_add_test(tc_header, test_valid_minimal);
    tcase_add_test(tc_header, test_valid_alt_empty);
    tcase_add_test(tc_header, test_bad_magic);
    tcase_add_test(tc_header, test_unsupported_version);
    tcase_add_test(tc_header, test_bad_offset_alignment);
    tcase_add_test(tc_header, test_section_past_eof);
    tcase_add_test(tc_header, test_overlapping_sections);
    tcase_add_test(tc_header, test_misaligned_section_size);
    tcase_add_test(tc_header, test_truncated_file);
    tcase_add_test(tc_header, test_open_nonexistent_file);
    suite_add_tcase(s, tc_header);

    // --- Asset record tests ---
    TCase *tc_assets = tcase_create("asset-tests");
    tcase_add_test(tc_assets, test_valid_one_asset);
    tcase_add_test(tc_assets, test_valid_binary_asset);
    tcase_add_test(tc_assets, test_valid_multi_assets_slack);
    tcase_add_test(tc_assets, test_asset_name_past_string_table);
    tcase_add_test(tc_assets, test_asset_name_nonprintable);
    tcase_add_test(tc_assets, test_asset_name_oob);
    tcase_add_test(tc_assets, test_asset_empty_name);
    tcase_add_test(tc_assets, test_second_asset_empty_name);
    tcase_add_test(tc_assets, test_overlapping_with_nonprintable);
    suite_add_tcase(s, tc_assets);

    // --- Compression tests ---
    TCase *tc_compression = tcase_create("compression-tests");
    tcase_add_test(tc_compression, test_valid_rle);
    tcase_add_test(tc_compression, test_rle_zero_count);
    tcase_add_test(tc_compression, test_rle_truncated);
    tcase_add_test(tc_compression, test_rle_bomb);
    suite_add_tcase(s, tc_compression);

    return s;
}

int main(void) {
    Suite   *s  = bun_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    int failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}