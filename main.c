#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "bun.h"

// Helper: check if data is printable ASCII
static int is_printable_ascii(const u8 *data, size_t size) {
    for (size_t i = 0; i < size; i++) {
        if (data[i] < 32 || data[i] > 126) {
            return 0;
        }
    }
    return 1;
}

// Helper: print hex dump (max 60 bytes)
static void print_hex_dump(const u8 *data, size_t size) {
    size_t len = size < 60 ? size : 60;
    printf("  Data (hex): ");
    for (size_t i = 0; i < len; i++) {
        printf("%02x ", data[i]);
    }
    if (size > 60) {
        printf("...");
    }
    printf("\n");
}

// Helper: print text preview (max 60 bytes)
static void print_text_preview(const u8 *data, size_t size) {
    size_t len = size < 60 ? size : 60;
    printf("  Data: \"");
    for (size_t i = 0; i < len; i++) {
        char c = (char)data[i];
        if (c == '"' || c == '\\') {
            printf("\\%c", c);
        } else if (c == '\n') {
            printf("\\n");
        } else if (c == '\r') {
            printf("\\r");
        } else if (c == '\t') {
            printf("\\t");
        } else {
            printf("%c", c);
        }
    }
    if (size > 60) {
        printf("...");
    }
    printf("\"\n");
}

int main(int argc, char *argv[]) {
  if (argc != 2) {
    fprintf(stderr, "Usage: %s <file.bun>\n", argv[0]);
    return BUN_ERR_IO;
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
    fprintf(stderr, "Error: header invalid or unsupported (code %d)\n", result);
    bun_close(&ctx);
    return result;
  }

  result = bun_parse_assets(&ctx, &header);

  if (result == BUN_OK) {
    // === Print all header fields ===
    printf("=== BUN File Header ===\n");
    printf("  Magic: 0x%08x (expected 0x%08x)\n", header.magic, BUN_MAGIC);
    printf("  Version: %d.%d\n", header.version_major, header.version_minor);
    printf("  Asset count: %u\n", header.asset_count);
    printf("  Asset table offset: %lu\n", (unsigned long)header.asset_table_offset);
    printf("  String table offset: %lu\n", (unsigned long)header.string_table_offset);
    printf("  String table size: %lu\n", (unsigned long)header.string_table_size);
    printf("  Data section offset: %lu\n", (unsigned long)header.data_section_offset);
    printf("  Data section size: %lu\n", (unsigned long)header.data_section_size);
    printf("  Reserved: %lu\n", (unsigned long)header.reserved);
    printf("\n");

    // Read string table into memory
    char *string_table = (char *)malloc((size_t)header.string_table_size + 1);
    if (string_table == NULL) {
        fprintf(stderr, "Error: could not allocate string table\n");
        bun_close(&ctx);
        return BUN_ERR_IO;
    }
    fseek(ctx.file, (long)header.string_table_offset, SEEK_SET);
    fread(string_table, 1, (size_t)header.string_table_size, ctx.file);
    string_table[header.string_table_size] = '\0';

    // Read data section into memory
    u8 *data_section = (u8 *)malloc((size_t)header.data_section_size + 1);
    if (data_section == NULL) {
        free(string_table);
        fprintf(stderr, "Error: could not allocate data section\n");
        bun_close(&ctx);
        return BUN_ERR_IO;
    }
    fseek(ctx.file, (long)header.data_section_offset, SEEK_SET);
    fread(data_section, 1, (size_t)header.data_section_size, ctx.file);

    // === Re-read asset records and print ===
    printf("=== Asset Records ===\n");
    fseek(ctx.file, (long)header.asset_table_offset, SEEK_SET);

    for (u32 i = 0; i < header.asset_count; i++) {
        u8 buf[BUN_ASSET_RECORD_SIZE];
        if (fread(buf, 1, BUN_ASSET_RECORD_SIZE, ctx.file) != BUN_ASSET_RECORD_SIZE) {
            fprintf(stderr, "Error: failed to read asset %u\n", i);
            break;
        }

        u32 name_offset       = read_u32_le(buf, 0);
        u32 name_length       = read_u32_le(buf, 4);
        u64 data_offset       = read_u64_le(buf, 8);
        u64 data_size         = read_u64_le(buf, 16);
        u64 uncompressed_size = read_u64_le(buf, 24);
        u32 compression       = read_u32_le(buf, 32);
        u32 type              = read_u32_le(buf, 36);
        u32 checksum          = read_u32_le(buf, 40);
        u32 flags             = read_u32_le(buf, 44);

        printf("\nAsset %u:\n", i);
        // Name (max 60 chars)
        printf("  Name: \"");
        size_t name_len = name_length < 60 ? name_length : 60;
        for (u32 j = 0; j < name_len; j++) {
            if (name_offset + j < header.string_table_size) {
                char c = string_table[name_offset + j];
                if (c == '"' || c == '\\') {
                    printf("\\%c", c);
                } else if (c == '\n') {
                    printf("\\n");
                } else if (c == '\r') {
                    printf("\\r");
                } else if (c == '\t') {
                    printf("\\t");
                } else {
                    printf("%c", c);
                }
            }
        }
        if (name_length > 60) {
            printf("...");
        }
        printf("\"\n");

        printf("  Name offset: %u, length: %u\n", name_offset, name_length);
        printf("  Data offset: %lu, size: %lu\n", (unsigned long)data_offset, (unsigned long)data_size);
        printf("  Uncompressed size: %lu\n", (unsigned long)uncompressed_size);
        printf("  Compression: %u (0=none)\n", compression);
        printf("  Type: %u\n", type);
        printf("  Checksum: 0x%08x\n", checksum);
        printf("  Flags: 0x%08x\n", flags);

        // Data preview (max 60 bytes)
        if (data_size > 0 && data_offset < header.data_section_size) {
            u64 data_end = data_offset + data_size;
            if (data_end <= header.data_section_size) {
                if (is_printable_ascii(data_section + data_offset, (size_t)data_size)) {
                    print_text_preview(data_section + data_offset, (size_t)data_size);
                } else {
                    print_hex_dump(data_section + data_offset, (size_t)data_size);
                }
            }
        }
    }

    free(data_section);
    free(string_table);

  } else if (result == BUN_MALFORMED) {
    fprintf(stderr, "Error: file is malformed\n");
  } else if (result == BUN_UNSUPPORTED) {
    fprintf(stderr, "Error: file uses unsupported features\n");
  } else if (result == BUN_ERR_IO) {
    fprintf(stderr, "Error: I/O error while parsing\n");
  }

  bun_close(&ctx);
  return result;
}
