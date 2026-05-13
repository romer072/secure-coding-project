#include <stdio.h>
#include <stdlib.h>

#include "bun.h"

static const char *result_summary(bun_result_t result) {
  switch (result) {
    case BUN_OK:
      return "no error";
    case BUN_MALFORMED:
      return "malformed BUN file";
    case BUN_UNSUPPORTED:
      return "unsupported BUN feature";
    case BUN_ERR_IO:
      return "I/O error (file not found, read failure, or seek failure)";
    case BUN_ERR_ARGS:
      return "invalid command-line arguments";
    case BUN_ERR_OVERFLOW:
      return "integer overflow while validating metadata";
    case BUN_ERR_TOOBIG:
      return "value exceeds implementation limits";
    default:
      return "Unknown error";
  }
}

static void print_parser_error(FILE *stream,
                               bun_result_t result,
                               const BunParseContext *ctx) {
  fprintf(stream, "Error: %s", result_summary(result));
  if (ctx->error_detail != NULL) {
    fprintf(stream, ": %s", ctx->error_detail);
  }
  if (ctx->error_offset_valid) {
    fprintf(stream,
            " (byte offset %llu)",
            (unsigned long long)ctx->error_offset);
  }
  fputc('\n', stream);
}

static int byte_is_text(u8 byte) {
  return (byte >= 0x20 && byte <= 0x7e)
      || byte == '\n'
      || byte == '\r'
      || byte == '\t';
}

static int data_prefix_is_text(const BunParsedAsset *asset) {
  u64 idx = 0;

  for (idx = 0; idx < asset->data_prefix_size; idx++) {
    if (!byte_is_text(asset->data_prefix[idx])) {
      return 0;
    }
  }

  return 1;
}

static void print_escaped_bytes(FILE *stream, const u8 *data, u64 size) {
  u64 idx = 0;

  for (idx = 0; idx < size; idx++) {
    switch (data[idx]) {
      case '\n':
        fputs("\\n", stream);
        break;
      case '\r':
        fputs("\\r", stream);
        break;
      case '\t':
        fputs("\\t", stream);
        break;
      case '\\':
        fputs("\\\\", stream);
        break;
      case '"':
        fputs("\\\"", stream);
        break;
      default:
        fputc((int)data[idx], stream);
        break;
    }
  }
}

static void print_hex_bytes(FILE *stream, const u8 *data, u64 size) {
  u64 idx = 0;

  for (idx = 0; idx < size; idx++) {
    fprintf(stream, "%s%02x", idx == 0 ? "" : " ", data[idx]);
  }
}

static void print_header_summary(const BunParseContext *ctx,
                                 const BunHeader *header) {
  printf("BUN parser summary\n");
  printf("File size: %ld bytes\n", ctx->file_size);
  printf("Header:\n");
  printf("  magic: 0x%08x\n", header->magic);
  printf("  version_major: %u\n", header->version_major);
  printf("  version_minor: %u\n", header->version_minor);
  printf("  asset_count: %u\n", header->asset_count);
  printf("  asset_table_offset: %llu\n",
         (unsigned long long)header->asset_table_offset);
  printf("  string_table_offset: %llu\n",
         (unsigned long long)header->string_table_offset);
  printf("  string_table_size: %llu\n",
         (unsigned long long)header->string_table_size);
  printf("  data_section_offset: %llu\n",
         (unsigned long long)header->data_section_offset);
  printf("  data_section_size: %llu\n",
         (unsigned long long)header->data_section_size);
  printf("  reserved: %llu\n", (unsigned long long)header->reserved);
}

static void print_asset_summary(const BunParsedAsset *asset, u32 index) {
  printf("Asset[%u]:\n", index);
  printf("  name_offset: %u\n", asset->record.name_offset);
  printf("  name_length: %u\n", asset->record.name_length);
  printf("  data_offset: %llu\n", (unsigned long long)asset->record.data_offset);
  printf("  data_size: %llu\n", (unsigned long long)asset->record.data_size);
  printf("  uncompressed_size: %llu\n",
         (unsigned long long)asset->record.uncompressed_size);
  printf("  compression: %u\n", asset->record.compression);
  printf("  type: %u\n", asset->record.type);
  printf("  checksum: 0x%08x\n", asset->record.checksum);
  printf("  flags: 0x%08x\n", asset->record.flags);

  printf("  name_prefix: \"");
  print_escaped_bytes(stdout, (const u8 *)asset->name_prefix, asset->name_prefix_length);
  printf("\"%s\n", asset->name_truncated ? "..." : "");

  if (asset->data_prefix_is_decompressed) {
    printf("  data_prefix_source: decompressed preview\n");
  } else {
    printf("  data_prefix_source: raw bytes\n");
  }

  if (data_prefix_is_text(asset)) {
    printf("  data_prefix: \"");
    print_escaped_bytes(stdout, asset->data_prefix, asset->data_prefix_size);
    printf("\"%s\n", asset->data_truncated ? "..." : "");
  } else {
    printf("  data_prefix: ");
    print_hex_bytes(stdout, asset->data_prefix, asset->data_prefix_size);
    printf("%s\n", asset->data_truncated ? " ..." : "");
  }
}

static void asset_callback(BunParseContext *ctx, const BunParsedAsset *asset, u32 asset_index) {
  (void)ctx;
  print_asset_summary(asset, asset_index);
}

int main(int argc, char *argv[]) {
  if (argc != 2) {
    fprintf(stderr, "Error: Incorrect number of arguments\n");
    fprintf(stderr, "Usage: %s <file.bun>\n", argv[0]);
    return BUN_ERR_ARGS;
  }
  const char *path = argv[1];

  BunParseContext ctx = {0};
  BunHeader header  = {0};

  bun_result_t result = bun_open(path, &ctx);
  if (result != BUN_OK) {
    fprintf(stderr, "Error: could not open '%s'\n", path);
    return result;
  }

  result = bun_parse_header(&ctx, &header);
  if (result != BUN_OK) {
    print_parser_error(stderr, result, &ctx);
    bun_close(&ctx);
    return result;
  }

  print_header_summary(&ctx, &header);

  /* Register the asset callback to print each asset as it's parsed */
  ctx.asset_callback = asset_callback;

  result = bun_parse_assets(&ctx, &header);

  printf("Assets parsed: %u/%u\n", ctx.parsed_asset_count, header.asset_count);

  if (result != BUN_OK) {
    print_parser_error(stderr, result, &ctx);
  }

  bun_close(&ctx);
  return result;
}
