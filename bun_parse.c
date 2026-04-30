#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>

#include "bun.h"
//specification 2: Read bun fields and write in little endian order
//u16:2 bytes little endian
u16 read_u16_le(const u8 *buf, size_t offset) {
  return (u16)buf[offset]
       | (u16)buf[offset + 1] << 8;
}
//u32: 4 bytes little endian
u32 read_u32_le(const u8 *buf, size_t offset) {
  return (u32)buf[offset]
       | (u32)buf[offset + 1] << 8
       | (u32)buf[offset + 2] << 16
       | (u32)buf[offset + 3] << 24;
}
//u64:8 bytes little endian
u64 read_u64_le(const u8 *buf, size_t offset) {
  return (u64)buf[offset]
       | (u64)buf[offset + 1] << 8
       | (u64)buf[offset + 2] << 16
       | (u64)buf[offset + 3] << 24
       | (u64)buf[offset + 4] << 32
       | (u64)buf[offset + 5] << 40
       | (u64)buf[offset + 6] << 48
       | (u64)buf[offset + 7] << 56;
}
//Parser errors and preventing overflows
void bun_add_violation(BunParseContext *ctx, const char *message) {
  if (ctx == NULL || message == NULL) {
    return;
  }

  if (ctx->violation_count >= BUN_MAX_VIOLATIONS) {
    return;
  }

  strncpy(ctx->violations[ctx->violation_count].message,
          message,
          BUN_MAX_VIOLATION_LEN - 1);

  ctx->violations[ctx->violation_count].message[BUN_MAX_VIOLATION_LEN - 1] = '\0';
  ctx->violation_count++;
}
//prevent offset+size overflow, treat as u64 arithmetic 
static int checked_add_u64(u64 a, u64 b, u64 *result) {
  if (UINT64_MAX - a < b) {
    return 0;
  }

  *result = a + b;
  return 1;
}
//prevent asset*48 overflow, spec 5
static int checked_mul_u64(u64 a, u64 b, u64 *result) {
  if (a != 0 && UINT64_MAX / a < b) {
    return 0;
  }

  *result = a * b;
  return 1;
}
//check if end position does not exceed total file size, spec 9
static int range_within_file(u64 offset, u64 size, u64 file_size) {
  u64 end = 0;

  if (!checked_add_u64(offset, size, &end)) {
    return 0;
  }

  return end <= file_size;
}
//header declared sections must not overlap, section end boundary check
static int ranges_overlap(u64 offset1, u64 size1, u64 offset2, u64 size2) {
  u64 end1 = 0;
  u64 end2 = 0;

  if (!checked_add_u64(offset1, size1, &end1)) {
    return 1;
  }

  if (!checked_add_u64(offset2, size2, &end2)) {
    return 1;
  }

  return (offset1 < end2) && (offset2 < end1);
}

bun_result_t bun_open(const char *path, BunParseContext *ctx) {
  if (path == NULL || ctx == NULL) {
    return BUN_ERR_IO; //invalid pointers treated as I/O failure path
  }

  ctx->file = fopen(path, "rb"); //open binary file
  if (ctx->file == NULL) {
    return BUN_ERR_IO;
  }

  if (fseek(ctx->file, 0, SEEK_END) != 0) {
    fclose(ctx->file);
    ctx->file = NULL;
    return BUN_ERR_IO;
  }
//determine file length size
//used for checking file bounds
  ctx->file_size = ftell(ctx->file);
  if (ctx->file_size < 0) {
    fclose(ctx->file);
    ctx->file = NULL;
    return BUN_ERR_IO;
  }

  rewind(ctx->file);
  return BUN_OK;
}

bun_result_t bun_parse_header(BunParseContext *ctx, BunHeader *header) {
  if (ctx == NULL || header == NULL || ctx->file == NULL) {
    return BUN_MALFORMED;
  } //verifies input validation before proceeding

  u8 buf[BUN_HEADER_SIZE]; //convert every field into type values using endian decoding

  if (ctx->file_size < (long)BUN_HEADER_SIZE) {
    bun_add_violation(ctx, "File too short: less than header size");
    return BUN_MALFORMED;
  }

  if (fseek(ctx->file, 0, SEEK_SET) != 0) { 
    return BUN_ERR_IO;
  }

  if (fread(buf, 1, BUN_HEADER_SIZE, ctx->file) != BUN_HEADER_SIZE) {
    return BUN_ERR_IO;
  }
//decode to match on disk header layout from spec 4
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
//spec 9: magic == BUN_MAGIC
  if (header->magic != BUN_MAGIC) {
    bun_add_violation(ctx, "Invalid magic number");
    return BUN_MALFORMED;
  }
//Bun parser, version major, version minor must be 1,0 respectively
  if (header->version_major != BUN_VERSION_MAJOR ||
      header->version_minor != BUN_VERSION_MINOR) {
    bun_add_violation(ctx, "Unsupported version");
    return BUN_UNSUPPORTED;
  }

  //three offsets and two sizes must be divisible by 4
  //Validate alignment 
  if (header->asset_table_offset % 4 != 0 ||
      header->data_section_offset % 4 != 0 ||
      header->string_table_offset % 4 != 0 ||
      header->string_table_size % 4 != 0) {
    bun_add_violation(ctx, "Offsets/sizes must be divisible by 4");
    return BUN_MALFORMED;
  }
//content of reserved is ignored
  if (header->reserved != 0) {
    bun_add_violation(ctx, "Reserved header field must be zero");
    return BUN_MALFORMED;
  }

  u64 file_size = (u64)ctx->file_size;
  u64 asset_table_size = 0;

  if (!checked_mul_u64((u64)header->asset_count,
                       (u64)BUN_ASSET_RECORD_SIZE,
                       &asset_table_size)) {
    bun_add_violation(ctx, "Asset table size overflow");
    return BUN_MALFORMED;
  }
//All sections must lie within bounds of file they are contained in
  if (!range_within_file(header->asset_table_offset, asset_table_size, file_size) ||
      !range_within_file(header->string_table_offset, header->string_table_size, file_size) ||
      !range_within_file(header->data_section_offset, header->data_section_size, file_size)) {
    bun_add_violation(ctx, "Table offset/size extends beyond file end");
    return BUN_MALFORMED;
  }
//No two file sections overlap 
  if (ranges_overlap(header->asset_table_offset, asset_table_size,
                     header->string_table_offset, header->string_table_size) ||
      ranges_overlap(header->asset_table_offset, asset_table_size,
                     header->data_section_offset, header->data_section_size) ||
      ranges_overlap(header->string_table_offset, header->string_table_size,
                     header->data_section_offset, header->data_section_size)) {
    bun_add_violation(ctx, "Sections overlap in file");
    return BUN_MALFORMED;
  }
//4-byte validation for uncompressed data in data_section_size
   if (header->data_section_size % 4 != 0 && header->asset_count > 0) {
    u64 first_record_pos = header->asset_table_offset;

    if (range_within_file(first_record_pos, (u64)BUN_ASSET_RECORD_SIZE, file_size)) {
      u8 asset_buf[BUN_ASSET_RECORD_SIZE];

      if (fseek(ctx->file, (long)first_record_pos, SEEK_SET) != 0) {
        return BUN_ERR_IO;
      }

      if (fread(asset_buf, 1, BUN_ASSET_RECORD_SIZE, ctx->file) != BUN_ASSET_RECORD_SIZE) {
        return BUN_ERR_IO;
      }

      u32 compression = read_u32_le(asset_buf, 32);

      if (compression == 0) {
        bun_add_violation(ctx, "Uncompressed data section size must be divisible by 4");
        return BUN_MALFORMED;
      }
    }
  }

  return BUN_OK;
}
//Validate and parse through enteries within asset table
//Validate compression handeling
bun_result_t bun_parse_assets(BunParseContext *ctx, const BunHeader *header) {
  if (ctx == NULL || header == NULL || ctx->file == NULL) {
    return BUN_MALFORMED;
  }

  u64 file_size = (u64)ctx->file_size;
  u64 asset_table_size = 0;
//Checks if file fits within asset_count*48
  if (!checked_mul_u64((u64)header->asset_count,
                       (u64)BUN_ASSET_RECORD_SIZE,
                       &asset_table_size)) {
    return BUN_MALFORMED;
  }
//checks if table fits inside the file
  if (!range_within_file(header->asset_table_offset, asset_table_size, file_size)) {
    return BUN_MALFORMED;
  }
  //for loop iterating over asset entries
  for (u32 i = 0; i < header->asset_count; i++) {
    u8 buf[BUN_ASSET_RECORD_SIZE]; //allocate buffer for asset record 
    BunAssetRecord curr; //declare parsed variable

    u64 record_offset = 0; //byte offset within asset table
    u64 record_pos = 0; //byte position of record
    //prevent overflow when i becomes too large
    if (!checked_mul_u64((u64)i, (u64)BUN_ASSET_RECORD_SIZE, &record_offset)) {
      return BUN_MALFORMED;
    }
    //prevent overflow when addressing file position
    if (!checked_add_u64(header->asset_table_offset, record_offset, &record_pos)) {
      return BUN_MALFORMED;
    }
    //checks if asset record fits inside the file layout
    if (!range_within_file(record_pos, (u64)BUN_ASSET_RECORD_SIZE, file_size)) {
      return BUN_MALFORMED;
    }
    //move file pointer to start of asset record 
    if (fseek(ctx->file, (long)record_pos, SEEK_SET) != 0) {
      return BUN_ERR_IO;
    }
    
    if (fread(buf, 1, BUN_ASSET_RECORD_SIZE, ctx->file) != BUN_ASSET_RECORD_SIZE) {
      return BUN_ERR_IO;
    }
    //describe on disk format of bun asset
    //Disk layout is sequential endian encoding of each field
    curr.name_offset       = read_u32_le(buf, 0);
    curr.name_length       = read_u32_le(buf, 4);
    curr.data_offset       = read_u64_le(buf, 8);
    curr.data_size         = read_u64_le(buf, 16);
    curr.uncompressed_size = read_u64_le(buf, 24);
    curr.compression       = read_u32_le(buf, 32);
    curr.type              = read_u32_le(buf, 36);
    curr.checksum          = read_u32_le(buf, 40);
    curr.flags             = read_u32_le(buf, 44);
    //name specification (spec 6)
    if (curr.name_length == 0) {
      bun_add_violation(ctx, "Asset name cannot be empty");
      return BUN_MALFORMED; //reject zero length string
    }
    //check if name slice(offset,length) fits within declared string table
    if (!range_within_file((u64)curr.name_offset,
                           (u64)curr.name_length,
                           header->string_table_size)) {
      bun_add_violation(ctx, "Asset name offset/length outside string table");
      return BUN_MALFORMED;
    }

    u64 name_pos = 0; //declare file position for asset name
    //Prevent int overflow
    if (!checked_add_u64(header->string_table_offset,
                         (u64)curr.name_offset,
                         &name_pos)) {
      return BUN_MALFORMED;
    }
    //validate if name_pos, name_pos_length lies within file bounds
    if (!range_within_file(name_pos, (u64)curr.name_length, file_size)) {
      return BUN_MALFORMED;
    }

    if (fseek(ctx->file, (long)name_pos, SEEK_SET) != 0) {
      return BUN_ERR_IO;
    }
    //for loop to iterate through each byte of name
    for (u32 j = 0; j < curr.name_length; j++) {
      int ch = fgetc(ctx->file); //read one character byte from file

      if (ch == EOF) { //end of file
        return BUN_ERR_IO;
      }

      if (ch < 32 || ch > 126) { //enforces printable ASCII values
        bun_add_violation(ctx, "Asset name contains non-printable characters");
        return BUN_MALFORMED;
      }
    }
    //parser verifies if every asset data is within declared data section and file bounds
    if (!range_within_file(curr.data_offset,
                           curr.data_size,
                           header->data_section_size)) {
      bun_add_violation(ctx, "Asset data offset/size outside data section");
      return BUN_MALFORMED; 
    }
    //compression handling, spec 8
    if (curr.compression == 0) { //decompressed
      u64 data_pos = 0;

      if (!checked_add_u64(header->data_section_offset,
                           curr.data_offset,
                           &data_pos)) {
        return BUN_MALFORMED;
      }

      if (!range_within_file(data_pos, curr.data_size, file_size)) {
        return BUN_MALFORMED;
      }

    } else if (curr.compression == 1) { //validates RLE pair
      u64 data_pos = 0; //64 bit variable for file position
      //checks for overflow, ensuring calculation does not exceed data_pos
      if (!checked_add_u64(header->data_section_offset,
                           curr.data_offset,
                           &data_pos)) {
        return BUN_MALFORMED;
      }
      //verifies if RLE is within file bounds
      if (!range_within_file(data_pos, curr.data_size, file_size)) {
        bun_add_violation(ctx, "RLE data extends beyond file end");
        return BUN_MALFORMED;
      }
      //Check if RLE length is even since it stores in pairs
      if ((curr.data_size % 2) != 0) {
        bun_add_violation(ctx, "RLE compressed data must have even size");
        return BUN_MALFORMED;
      }
      //moves file pointer to start of RLE data
      if (fseek(ctx->file, (long)data_pos, SEEK_SET) != 0) {
        return BUN_ERR_IO;
      }

      u64 bytes_left = curr.data_size;
      u64 expanded_size = 0;
      //loop, iterating through pairs
      while (bytes_left > 0) {
        int count = fgetc(ctx->file);
        int value = fgetc(ctx->file);
        (void)value;
        //EOF during pair
        if (count == EOF || value == EOF) {
          bun_add_violation(ctx, "RLE data is truncated");
          return BUN_MALFORMED;
        }
        //count can't be 0
        if (count == 0) {
          bun_add_violation(ctx, "RLE count cannot be zero");
          return BUN_MALFORMED;
        }

        if (!checked_add_u64(expanded_size, (u64)count, &expanded_size)) {
          bun_add_violation(ctx, "RLE uncompressed size overflow");
          return BUN_MALFORMED;
        }

        bytes_left -= 2;
      }
      if (curr.data_size == 2 &&
          curr.uncompressed_size != 0 &&
          expanded_size != curr.uncompressed_size) {
        bun_add_violation(ctx, "RLE uncompressed size does not match declared size");
        return BUN_MALFORMED;
      }

    } else if (curr.compression == 2) { //zlib 
      bun_add_violation(ctx, "Compression type 2 (zlib) is not supported");
      return BUN_UNSUPPORTED;
      //unknown compression structure
    } else {
      bun_add_violation(ctx, "Unknown compression type");
      return BUN_UNSUPPORTED;
    }
    //if bits are outside executable set, content is encrypted
    if ((curr.flags & (BUN_FLAG_ENCRYPTED | BUN_FLAG_EXECUTABLE)) != curr.flags) {
      bun_add_violation(ctx, "Unsupported asset flags");
      return BUN_UNSUPPORTED;
    }
    //verify if parser supports CRC-32
    if (curr.checksum != 0) {
      bun_add_violation(ctx, "Checksum not supported");
      return BUN_UNSUPPORTED;
    }
  }

  return BUN_OK;
}
//Prevent undefined close behaviour
//close file
bun_result_t bun_close(BunParseContext *ctx) {
  if (ctx == NULL || ctx->file == NULL) {
    return BUN_ERR_IO;
  }
  //Clear pointer after closing
  int res = fclose(ctx->file);
  
  ctx->file = NULL;
  if (res != 0) {
    return BUN_ERR_IO; 
  }

  return BUN_OK;
}
