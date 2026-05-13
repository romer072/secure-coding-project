#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>

#include "bun.h"

/**
 * Example helper: convert 4 bytes in `buf`, positioned at `offset`,
 * into a little-endian u32.
 */

 static u16 read_u16_le(const u8 *buf, size_t offset) {
  return (u16)buf[offset]
     | (u16)buf[offset + 1] << 8;
}

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

static int checked_mul_u64(u64 a, u64 b, u64 *out) {
  if (a != 0 && b > UINT64_MAX / a) {
    return 0;
  }

  *out = a * b;
  return 1;
}

static int range_within_file(u64 offset, u64 size, u64 file_size) {
  if (offset > file_size) {
    return 0;
  }

  if (size > file_size - offset) {
    return 0;
  }

  return 1;
}

static void add_error(BunParseContext *ctx, const char *msg) {
  if (ctx->error_count < MAX_ERRORS) {
    ctx->error_msgs[ctx->error_count++] = msg;
  }
}


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

  if (ctx->file_size < (long)BUN_HEADER_SIZE) {
    add_error(ctx, "File is too short");
    return BUN_MALFORMED;
  }

  // slurp the header into `buf`
  if (fread(buf, 1, BUN_HEADER_SIZE, ctx->file) != BUN_HEADER_SIZE) {
    add_error(ctx, "Failed to read header");
    return BUN_ERR_IO;
  }
  
  // Populate `header` from `buf`.

    header->magic = read_u32_le(buf,0);
    header->version_major = read_u16_le(buf,4);
    header->version_minor = read_u16_le(buf,6);
    header->asset_count = read_u32_le(buf,8);
    header->asset_table_offset = read_u64_le(buf,12);
    header->string_table_offset = read_u64_le(buf,20);
    header->string_table_size = read_u64_le(buf,28);
    header->data_section_offset = read_u64_le(buf,36);
    header->data_section_size = read_u64_le(buf,44);
    header->reserved = read_u64_le(buf,52);

  // Magic must match (BUN0)
  if (header->magic != BUN_MAGIC) {
    add_error(ctx, "BUN files must begin with BUN0");
    return BUN_MALFORMED;
  }

  // Check that version major is 1 and minor is 0
  if (header->version_major != BUN_VERSION_MAJOR) {
    add_error(ctx, "Version major must be 1");
    return BUN_UNSUPPORTED;
  }

  if (header->version_minor != BUN_VERSION_MINOR) {
    add_error(ctx, "Version minor must be 0");
    return BUN_UNSUPPORTED;
  }

  // Check to ensure offsets and sizes are divisible by 4
  if ((header->asset_table_offset % 4) != 0 || 
      (header->string_table_offset % 4) != 0 || 
      (header->data_section_offset % 4) != 0 || 
      (header->string_table_size % 4) != 0 || 
      (header->data_section_size % 4) != 0) {

    add_error(ctx, "Offsets and sizes must be divisible by 4");
    return BUN_MALFORMED;
  }

  // Check to ensure that the sections is within file boundary
  u64 max_file_size = (u64)ctx->file_size;
  u64 asset_table_size;

  if (!checked_mul_u64((u64)header->asset_count, BUN_ASSET_RECORD_SIZE, &asset_table_size)) {
    add_error(ctx, "Asset table size overflow");
    return BUN_MALFORMED;
  }

  if (!range_within_file(header->asset_table_offset, asset_table_size, max_file_size)) {
    add_error(ctx, "Asset table is outside file bounds");
    return BUN_MALFORMED;
  }

  if (!range_within_file(header->string_table_offset, header->string_table_size, max_file_size)) {
    add_error(ctx, "String table is outside file bounds");
    return BUN_MALFORMED;
  }

  if (!range_within_file(header->data_section_offset, header->data_section_size, max_file_size)) {
    add_error(ctx, "Data section is outside file bounds");
    return BUN_MALFORMED;
  }

  // Check to ensure that section do not overlap
  // Calculate the end of each section (asset_table, string_table and data_section)
  u64 asset_table_end = header->asset_table_offset + asset_table_size;  
  u64 string_table_end = header->string_table_offset + header->string_table_size;
  u64 data_section_end = header->data_section_offset + header->data_section_size;

  if(header->asset_table_offset < BUN_HEADER_SIZE && asset_table_end > 0) {
    add_error(ctx, "Asset table overlaps with the header");
    return BUN_MALFORMED;
  } 

  if(header->string_table_offset < BUN_HEADER_SIZE && string_table_end > 0) {
    add_error(ctx, "String table overlaps with the header");
    return BUN_MALFORMED;
  }
  
  if(header->data_section_offset < BUN_HEADER_SIZE && data_section_end > 0) {
    add_error(ctx, "Data section overlaps with the header");
    return BUN_MALFORMED;
  }

  if(header->asset_table_offset < string_table_end && header->string_table_offset < asset_table_end) {
    add_error(ctx, "Asset table overlaps with the string table");
    return BUN_MALFORMED;
  }

  if(header->asset_table_offset < data_section_end && header->data_section_offset < asset_table_end) {
    add_error(ctx, "Asset table overlaps with the data section");
    return BUN_MALFORMED;
  }

  if(header->string_table_offset < data_section_end && header->data_section_offset < string_table_end) {
    add_error(ctx, "String table overlaps with the data section");
    return BUN_MALFORMED;
  }

  return BUN_OK;
}

bun_result_t bun_parse_assets(BunParseContext *ctx, const BunHeader *header) {
  if (fseek(ctx->file, header->asset_table_offset, SEEK_SET) != 0) {
    add_error(ctx, "Failed to seek to asset table");
    return BUN_ERR_IO;
  }

  for (u32 i = 0; i < header->asset_count; i++) {
    u8 buf[BUN_ASSET_RECORD_SIZE];

    //Error handling if one asset record cannot be read completely.
    if (fread(buf, 1, BUN_ASSET_RECORD_SIZE, ctx->file) != BUN_ASSET_RECORD_SIZE) {
      add_error(ctx, "Reading asset record failed");
      return BUN_ERR_IO;
    }

    BunAssetRecord asset;
    //Extract numbers from buffer by reading the bytes in little-endian order and populating the asset record.
    asset.name_offset = read_u32_le(buf, 0);
    asset.name_length = read_u32_le(buf, 4);
    asset.data_offset = read_u64_le(buf, 8);
    asset.data_size = read_u64_le(buf, 16);
    asset.uncompressed_size = read_u64_le(buf, 24);
    asset.compression = read_u32_le(buf, 32);
    asset.type = read_u32_le(buf, 36);
    asset.checksum = read_u32_le(buf, 40);
    asset.flags = read_u32_le(buf, 44);

    // Validating the length of the name

    if (asset.name_length == 0) {
      add_error(ctx, "Name length cannot be zero.");
      return BUN_MALFORMED;
    }

     //Validating namebounds to ensure that the name is within the string table.
    if ((u64)asset.name_offset + (u64)asset.name_length > header->string_table_size) {
      add_error(ctx, "Invalid name length.");
      return BUN_MALFORMED;
    }

    if (asset.data_offset + asset.data_size > header->data_section_size) {
      add_error(ctx, "Asset data range exceeds data section");
      return BUN_MALFORMED;
    }

    //Validating data bounds to ensure that the data is within the data section.
    u64 real_data_offset = header->data_section_offset + asset.data_offset;
    if (!range_within_file(real_data_offset, asset.data_size, ctx->file_size)) {
      add_error(ctx, "Data is not within file bounds");
      return BUN_MALFORMED;
    }

    long current_pos = ftell(ctx->file);
    if (current_pos < 0) {
      add_error(ctx, "Failed to save current file position");
      return BUN_ERR_IO;
    }

    if (fseek(ctx->file, (header->string_table_offset + asset.name_offset), SEEK_SET) != 0) {
      add_error(ctx, "Failed to move to name in the string table");
      return BUN_ERR_IO;
    }

    char name[asset.name_length + 1]; // +1 for null terminator

    if (fread(name, 1, asset.name_length, ctx->file) != asset.name_length) {
      add_error(ctx, "Failed to read the name from string table");
      return BUN_ERR_IO;
    }
    name[asset.name_length] = '\0';

    for (u32 j = 0; j < asset.name_length; j++) {
      if (name[j] < 0x20 || name[j] > 0x7E) {
        add_error(ctx, "Name contains non-printable ASCII characters");
        return BUN_MALFORMED;
      }
    }

    //Validating compression
    if (asset.compression == 0) {
      if (asset.uncompressed_size != 0) {
        add_error(ctx, "Uncompressed size must be 0 when compression is 0");
        return BUN_MALFORMED;
      }
    } else if (asset.compression == 1) {
      // Ensure that the RLE compressed data is an even number of bytes
      if (asset.data_size % 2 != 0) {
        add_error(ctx, "RLE compressed data size must be divisible by 2");
        return BUN_MALFORMED;
      }

      if (fseek(ctx->file, real_data_offset, SEEK_SET) != 0) {
        add_error(ctx, "Failed to seek to RLE payload");
        return BUN_ERR_IO;
      }

      // Calculate how many pairs there are
      u64 num_pairs = asset.data_size / 2;

      // Counter for total uncompressed size
      u64 total_uncompressed_size = 0;

      // Read pairs and validate
      for (u64 j = 0; j < num_pairs; j++) {
        u8 rle_pair[2];
        if (fread(rle_pair, 1, 2, ctx->file) != 2) {
          add_error(ctx, "Failed to read RLE pair");
          return BUN_ERR_IO;
        }
        if (rle_pair[0] == 0) {
          add_error(ctx, "RLE count cannot be 0");
          return BUN_MALFORMED;
        }
        total_uncompressed_size += rle_pair[0];
      }

      if (total_uncompressed_size != asset.uncompressed_size) {
        add_error(ctx, "Mismatch between RLE contents and the expected uncompressed size");
        return BUN_MALFORMED;
      }
    } else {
      add_error(ctx, "Unsupported compression method");
      return BUN_UNSUPPORTED;
    }

    //Validating checksum
    if (asset.checksum != 0) {
      add_error(ctx, "Checksum is not supported");
      return BUN_UNSUPPORTED;
    }

    //Validating flags
    u32 allowed_flags = BUN_FLAG_ENCRYPTED | BUN_FLAG_EXECUTABLE;
    if (asset.flags & ~allowed_flags) {
      add_error(ctx, "Invalid flags set");
      return BUN_UNSUPPORTED;
    }

    size_t data_preview_size = (size_t)(asset.data_size < MAX_PREVIEW_SIZE ? asset.data_size : MAX_PREVIEW_SIZE);
    u8 data_preview_bytes[MAX_PREVIEW_SIZE] = {0};
    if (data_preview_size > 0) {
      if (fseek(ctx->file, real_data_offset, SEEK_SET) != 0) {
        add_error(ctx, "Failed to seek to payload preview");
        return BUN_ERR_IO;
      }
      if (fread(data_preview_bytes, 1, data_preview_size, ctx->file) != data_preview_size) {
        add_error(ctx, "Failed to read payload preview");
        return BUN_ERR_IO;
      }
    }

    bun_print_asset_record(&asset, i, header->asset_count, name, data_preview_bytes, data_preview_size);

    if (fseek(ctx->file, current_pos, SEEK_SET) != 0) {
      add_error(ctx, "Failed to return to asset table");
      return BUN_ERR_IO;
    }
  }
  return BUN_OK;
}

void bun_print_header(const BunHeader *header) {
  printf("\n=== BUN File Summary ===\n\n");
  printf("--- HEADER ---\n");
  printf("Magic:                 BUN0\n");
  printf("Version:               %u.%u\n", header->version_major, header->version_minor);
  printf("Asset count:           %u\n", header->asset_count);
  printf("Asset table offset:    %lu bytes\n", header->asset_table_offset);
  printf("String table offset:   %lu bytes\n", header->string_table_offset);
  printf("String table size:     %lu bytes\n", header->string_table_size);
  printf("Data section offset:   %lu bytes\n", header->data_section_offset);
  printf("Data section size:     %lu bytes\n", header->data_section_size);
  printf("Reserved:              0x%lx\n\n", header->reserved);
}

void bun_print_asset_record(const BunAssetRecord *record, int index, u32 asset_count, const char *name, const u8 *data_preview_bytes, size_t data_preview_size) {
  printf("--- Asset %u/%u ---\n", index + 1, asset_count);
  printf("Name:                  %.*s\n", MAX_PREVIEW_SIZE, name);
  printf("Name offset:           %u bytes\n", record->name_offset);
  printf("Name length:           %u bytes\n", record->name_length);
  printf("Data offset:           %lu bytes\n", record->data_offset);
  printf("Data size:             %lu bytes\n", record->data_size);
  printf("Uncompressed size:     %lu bytes\n", record->uncompressed_size);
  printf("Compression:           %u (%s)\n", record->compression, record->compression ? "RLE" : "None");
  printf("Type:                  %u\n", record->type);

  if (record->checksum == 0) {
    printf("Checksum:              0 (Unused)\n");
  } else {
    printf("Checksum:              0x%08x\n", record->checksum);
  }

  if (record->flags == (BUN_FLAG_ENCRYPTED | BUN_FLAG_EXECUTABLE)) {
    printf("Flags:                 %u (Encrypted, Executable)\n", record->flags);
  }
  else if (record->flags == BUN_FLAG_ENCRYPTED) {
    printf("Flags:                 %u (Encrypted)\n", record->flags);
  }
  else if (record->flags == BUN_FLAG_EXECUTABLE) {
    printf("Flags:                 %u (Executable)\n", record->flags);
  }
  else {
    printf("Flags:                 %u (None)\n", record->flags);
  }

  for (size_t j = 0; j < data_preview_size; j++) {
    if (data_preview_bytes[j] < 0x20 || data_preview_bytes[j] > 0x7E) {
      printf("Data (hex):           ");
      for (size_t k = 0; k < data_preview_size; k++) {
        printf(" %02x", (unsigned char)data_preview_bytes[k]);
      }
      printf("\n\n");
      return;
    }
  }
  printf("Data (text):           %.*s\n\n", MAX_PREVIEW_SIZE, data_preview_bytes);
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
