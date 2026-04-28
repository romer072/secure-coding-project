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

// Example test suite: header parsing

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


//added test file used the remiang test smpale file, eahc test file is preety much the same format

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

// Opening a file that does not exist should return an I/O error. sepcial case!!
START_TEST(test_open_nonexistent_file) {
    BunParseContext ctx = {0};

    bun_result_t r = bun_open(fixture("valid/does-not-exist.bun"), &ctx);
    ck_assert_int_eq(r, BUN_ERR_IO);
}
END_TEST



    // TCASE 2: asset-tests
    //Tests that bun_parse_assets() correctly reads and validates asset records, names, and data references.

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


START_TEST(test_overlapping_with_nonprintable) {
    BunParseContext ctx = {0};
    BunHeader header    = {0};

    bun_result_t r = bun_open(fixture("invalid/10-overlapping-with-nonprintable.bun"), &ctx);
    ck_assert_int_eq(r, BUN_OK);

    r = bun_parse_header(&ctx, &header);
    ck_assert_int_eq(r, BUN_MALFORMED);

    bun_close(&ctx);
}
END_TEST



   //TCASE 3: compression-tests
   //Tests related to compression fields in asset records.
 
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

START_TEST(test_rle_bomb) {
    BunParseContext ctx = {0};
    BunHeader header    = {0};

    bun_result_t r = bun_open(fixture("invalid/15-rle-bomb.bun"), &ctx);
    ck_assert_int_eq(r, BUN_OK);

    r = bun_parse_header(&ctx, &header);
    ck_assert_int_eq(r, BUN_OK);

    r = bun_parse_assets(&ctx, &header);
    ck_assert_int_eq(r, BUN_MALFORMED);

    bun_close(&ctx);
}
END_TEST

START_TEST(test_rle_big_file) {
    BunParseContext ctx = {0};
    BunHeader header = {0};

    bun_result_t r = bun_open(fixture("bigfile/rle-stress.bun"), &ctx);
    ck_assert_int_eq(r, BUN_OK);

    r = bun_parse_header(&ctx, &header);
    ck_assert_int_eq(r, BUN_OK);

    r = bun_parse_assets(&ctx, &header);
    ck_assert_int_eq(r, BUN_OK);

    bun_close(&ctx);
}
END_TEST

// Assemble a test suite from our tests

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
    tcase_add_test(tc_compression, test_rle_big_file);
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