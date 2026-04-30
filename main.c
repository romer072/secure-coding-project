#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "bun.h"


// Check if data contains only printable ASCII characters (bytes 32-126).
 // Used to determine whether to display data as text or hex dump.

// @param data   Pointer to data buffer
// @param size   Number of bytes to check
// @return       1 if all bytes are printable ASCII, 0 otherwise

static int is_printable_ascii(const u8 *data, size_t size) {
    for (size_t i = 0; i < size; i++) {
        if (data[i] < 32 || data[i] > 126) {
            return 0;
        }
    }
    return 1;
}


// Print a hex dump of binary data (max 60 bytes).
// Used for non-printable data such as images or compressed content.

// @param data   Pointer to data buffer
// @param size   Number of bytes to print

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


// Print a text preview of data (max 60 bytes).
// Escapes special characters (", \, \n, \r, \t) for safe display.

// @param data   Pointer to data buffer
// @param size   Number of bytes to print

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


// Convert a bun_result_t to a human-readable string.

static const char *result_to_string(bun_result_t result) {
    switch (result) {
        case BUN_OK:          return "bun_ok";
        case BUN_MALFORMED:   return "bun_malformed";
        case BUN_UNSUPPORTED: return "bun_unsupported";
        case BUN_ERR_IO:      return "bun_err_io";
        default:              return "unknown";
    }
}

/**
 * Main entry point for the BUN file parser.
 * 
 * @param argc   Number of command-line arguments
 * @param argv   Array of command-line argument strings
 * @return       Exit code (0=success, 1=malformed, 2=unsupported, 3=error)
 */
int main(int argc, char *argv[]) {
  // Validate command-line arguments
  if (argc != 2) {
    fprintf(stderr, "Usage: %s <file.bun>\n", argv[0]);
    return BUN_ERR_IO;
  }
  const char *path = argv[1];

  // Initialize parse context and header structure
  BunParseContext ctx = {0};
  BunHeader header  = {0};

  // Open the BUN file
  bun_result_t result = bun_open(path, &ctx);
  if (result != BUN_OK) {
    fprintf(stderr, "Error: could not open '%s'\n", path);
    return result;
  }

    // Parse and validate the BUN header
    result = bun_parse_header(&ctx, &header);
    if (result != BUN_OK) {
        // Print error type and code
        if (result == BUN_MALFORMED) {
            printf(" BUN Malformed (error code: %d)\n", result);
        } else if (result == BUN_UNSUPPORTED) {
            printf(" BUN Unsupported (error code: %d)\n", result);
        } else {
            printf(" BUN Error (error code: %d)\n", result);
        }
        // Print all violations, one per line, prefixed with '- '
        for (size_t i = 0; i < ctx.violation_count; i++) {
            printf("- %s\n", ctx.violations[i].message);
        }
        bun_close(&ctx);
        return result;
    }

  // Parse asset records
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
    // The string table stores asset names as a contiguous buffer
    char *string_table = (char *)malloc((size_t)header.string_table_size + 1);
    if (string_table == NULL) {
        fprintf(stderr, "Error: could not allocate string table\n");
        bun_close(&ctx);
        return BUN_ERR_IO;
    }
        if (fseek(ctx.file, (long)header.string_table_offset, SEEK_SET) != 0) {
        fprintf(stderr, "Error: failed to seek to string table\n");
        free(string_table);
        bun_close(&ctx);
        return BUN_ERR_IO;
    }
    // This moves the file pointer to the start of the string table and checks whether the seek succeeded.

    if (fread(string_table, 1, (size_t)header.string_table_size, ctx.file)
        != (size_t)header.string_table_size) {
        fprintf(stderr, "Error: failed to read string table\n");
        free(string_table);
        bun_close(&ctx);
        return BUN_ERR_IO;
    }
    // This reads the string table and verifies that the exact expected number of bytes was read.

    string_table[header.string_table_size] = '\0';
    // This adds a null terminator after the bytes read from the string table. This makes later string-style printing safer because C string functions expect a terminating '\0'

    // Read data section into memory
    // The data section stores the actual payload data for each asset
    u8 *data_section = (u8 *)malloc((size_t)header.data_section_size + 1);
    if (data_section == NULL) {
        free(string_table);
        fprintf(stderr, "Error: could not allocate data section\n");
        bun_close(&ctx);
        return BUN_ERR_IO;
    }
    // This allocates memory for the data section and checks whether allocation succeeded.

    if (fseek(ctx.file, (long)header.data_section_offset, SEEK_SET) != 0) {
        fprintf(stderr, "Error: failed to seek to data section\n");
        free(data_section);
        free(string_table);
        bun_close(&ctx);
        return BUN_ERR_IO;
    }
    // Moves the file pointer to the start of the string table and check whether the seek succeeded.

    if (fread(data_section, 1, (size_t)header.data_section_size, ctx.file)
        != (size_t)header.data_section_size) {
        fprintf(stderr, "Error: failed to read data section\n");
        free(data_section);
        free(string_table);
        bun_close(&ctx);
        return BUN_ERR_IO;
    }
    // This reads the string table and verifies that the exact expected number of bytes was read.
    // === Re-read asset records and print ===
    // We re-read the asset table because bun_parse_assets() doesn't store
    // the parsed records for us to access. Each asset record is 48 bytes.
    printf("=== Asset Records ===\n");
    fseek(ctx.file, (long)header.asset_table_offset, SEEK_SET);

    for (u32 i = 0; i < header.asset_count; i++) {
        u8 buf[BUN_ASSET_RECORD_SIZE];
        if (fread(buf, 1, BUN_ASSET_RECORD_SIZE, ctx.file) != BUN_ASSET_RECORD_SIZE) {
            fprintf(stderr, "Error: failed to read asset %u\n", i);
            break;
        }

        // Parse asset record fields (little-endian)
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
        // Asset names are stored in the string table, referenced by offset/length
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
        // Display as text if printable ASCII, otherwise hex dump
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

    // Clean up allocated memory
    free(data_section);
    free(string_table);

    printf("\nResult: %s\n", result_to_string(result));
    return result;
    } else {
        // Print error type and code for invalid files
        if (result == BUN_MALFORMED) {
            printf(" BUN Malformed (error code: %d)\n", result);
        } else if (result == BUN_UNSUPPORTED) {
            printf(" BUN Unsupported (error code: %d)\n", result);
        } else {
            printf(" BUN Error (error code: %d)\n", result);
        }
        // Print all violations, one per line, prefixed with '- '
        for (size_t i = 0; i < ctx.violation_count; i++) {
            printf("- %s\n", ctx.violations[i].message);
        }
        bun_close(&ctx);
        return result;
    }
}
