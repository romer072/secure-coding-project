#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "bun.h"

// Reads a 16-bit unsigned integer from buffer in little-endian format.
u16 read_u16_le(const u8 *buf, size_t offset) {
  return (u16)buf[offset]
     | (u16)buf[offset + 1] << 8;
}

// Reads a 32-bit unsigned integer from buffer in little-endian format.
u32 read_u32_le(const u8 *buf, size_t offset) {
  return (u32)buf[offset]
     | (u32)buf[offset + 1] << 8
     | (u32)buf[offset + 2] << 16
     | (u32)buf[offset + 3] << 24;
}

// Reads a 64-bit unsigned integer from buffer in little-endian format.
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


// Safely stores one validation error message in the parse context.
// Allows main.c to collect and print all violations rather than
// immediately printing to stderr.

void bun_add_violation(BunParseContext *ctx, const char *message) {
  if (ctx->violation_count >= BUN_MAX_VIOLATIONS) {
    return;
  }

  strncpy(ctx->violations[ctx->violation_count].message,
          message,
          BUN_MAX_VIOLATION_LEN - 1);

  ctx->violations[ctx->violation_count].message[BUN_MAX_VIOLATION_LEN - 1] = '\0';
  ctx->violation_count++;
}

// Performs checked 64-bit unsigned integer addition.
static int checked_add_u64(u64 a, u64 b, u64 *result) {
  if (UINT64_MAX - a < b) {
    return 0; // overflow
  }
  *result = a + b;
  return 1; // success
}

// Performs checked 64-bit unsigned integer multiplication.
static int checked_mul_u64(u64 a, u64 b, u64 *result) {
  if (a != 0 && UINT64_MAX / a < b) {
    return 0; // overflow
  }
  *result = a * b;
  return 1; // success
}

// Checks whether a memory range [offset, offset+size] fits within a file.
static int range_within_file(u64 offset, u64 size, u64 file_size) {
  u64 end;
  if (!checked_add_u64(offset, size, &end)) {
    return 0; // overflow
  }
  return end <= file_size;
}

// Checks whether two memory ranges overlap.
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

bun_result_t bun_open(const char *path, BunParseContext *ctx) {
  // Open file in binary read mode
  ctx->file = fopen(path, "rb");
  if (!ctx->file) { // Check if file opened successfully
    return BUN_ERR_IO;
  }

  // Seek to end of file to get its size
  if (fseek(ctx->file, 0, SEEK_END) != 0) {
    fclose(ctx->file);
    return BUN_ERR_IO;
  }
  ctx->file_size = ftell(ctx->file); // Get file size in bytes
  if (ctx->file_size < 0) { // Check if ftell succeeded
    fclose(ctx->file);
    return BUN_ERR_IO;
  }
  rewind(ctx->file); // Reset file position to beginning

  return BUN_OK;
}

// Parses and validates the BUN file header.
// Reads header fields from file, validates magic number, version,
// alignment requirements, and checks that all table/data ranges
// are within file bounds and don't overlap.

bun_result_t bun_parse_header(BunParseContext *ctx, BunHeader *header) {
  u8 buf[BUN_HEADER_SIZE]; // Buffer to hold header data

  // Check if file is large enough to contain header
  if (ctx->file_size < (long)BUN_HEADER_SIZE) {
    bun_add_violation(ctx, "File too short: less than header size (60 bytes)");
    return BUN_MALFORMED;
  }

  // Read header bytes from file into buffer
  if (fread(buf, 1, BUN_HEADER_SIZE, ctx->file) != BUN_HEADER_SIZE) {
    return BUN_ERR_IO;
  }

  // Parse header fields from buffer in little-endian format
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

  // Validate magic number (must match BUN_MAGIC)
  if (header->magic != BUN_MAGIC) {
    bun_add_violation(ctx, "Invalid magic number: expected 0x304E5542");
    return BUN_MALFORMED;
  }

  // Check if version is supported (must be 1.0)
  if (header->version_major != BUN_VERSION_MAJOR ||
    header->version_minor != BUN_VERSION_MINOR) {
    bun_add_violation(ctx, "Unsupported version: only 1.0 is supported");
    return BUN_UNSUPPORTED;
  }

    // Verify all offsets and sizes are 4-byte aligned
   if (header->asset_table_offset % 4 != 0 ||
    header->data_section_offset % 4 != 0 ||
     header->data_section_size % 4 != 0 ||
     header->string_table_offset % 4 != 0 ||
     header->string_table_size % 4 != 0) {
   bun_add_violation(ctx, "Offsets/sizes must be divisible by 4");
   return BUN_MALFORMED;
 }
  
  u64 file_size = (u64)ctx->file_size; // Convert file size to u64
  u64 asset_table_size = 0;

  // Calculate total asset table size with overflow check
  if (!checked_mul_u64((u64)header->asset_count,
                        (u64)BUN_ASSET_RECORD_SIZE,
                        &asset_table_size)) {
    bun_add_violation(ctx, "Asset table size overflow");
    return BUN_MALFORMED;
  }

  // Verify all sections are within file bounds
  if (!range_within_file(header->asset_table_offset, asset_table_size, file_size) ||
      !range_within_file(header->string_table_offset, header->string_table_size, file_size) ||
      !range_within_file(header->data_section_offset, header->data_section_size, file_size)) {
    bun_add_violation(ctx, "Table offset/size extends beyond file end");
    return BUN_MALFORMED;
  }

  // Verify no sections overlap each other
  if (ranges_overlap(header->asset_table_offset, asset_table_size,
                      header->string_table_offset, header->string_table_size) ||
      ranges_overlap(header->asset_table_offset, asset_table_size,
                      header->data_section_offset, header->data_section_size) ||
      ranges_overlap(header->string_table_offset, header->string_table_size,
                      header->data_section_offset, header->data_section_size)) {
    bun_add_violation(ctx, "Sections overlap in file");
    return BUN_MALFORMED;
  }

  return BUN_OK;
}

// Parses and validates all asset records in the BUN file.
// Iterates through each asset, validates that name and data offsets
// are within their respective sections, and checks for unsupported

bun_result_t bun_parse_assets(BunParseContext *ctx, const BunHeader *header) {
  if (ctx == NULL || header == NULL || ctx->file == NULL) {
      return BUN_MALFORMED;
  }

  u64 file_size = (u64)ctx->file_size;
  u64 asset_table_size = 0;

  if (!checked_mul_u64((u64)header->asset_count,
                       (u64)BUN_ASSET_RECORD_SIZE,
                       &asset_table_size)) {
      return BUN_MALFORMED;
  }

  if (!range_within_file(header->asset_table_offset, asset_table_size, file_size)) {
      return BUN_MALFORMED;
  }

  if (fseek(ctx->file, (long)header->asset_table_offset, SEEK_SET) != 0) {
      return BUN_ERR_IO;
  }

  for (u32 i = 0; i < header->asset_count; i++) {
      u8 buf[BUN_ASSET_RECORD_SIZE];
      BunAssetRecord curr;
      u64 recordOff =0;
      u64 recordPos =0;
      if(!checked_mul_u64((u64)i,(u64)BUN_ASSET_RECORD_SIZE,&recordOff)){
        return BUN_MALFORMED;
      }
      if(!checked_add_u64(header->asset_table_offset,recordOff,&recordPos)){
        return BUN_MALFORMED;
      }
      if(!range_within_file(recordPos,(u64)BUN_ASSET_RECORD_SIZE,file_size)){
        return BUN_MALFORMED;
      }
      if(fseek(ctx->file,(long)recordPos,SEEK_SET)!=0){
        return BUN_ERR_IO;
      }

      if (fread(buf, 1, BUN_ASSET_RECORD_SIZE, ctx->file) != BUN_ASSET_RECORD_SIZE) {
          return BUN_ERR_IO;
      }

      curr.name_offset       = read_u32_le(buf, 0);
      curr.name_length       = read_u32_le(buf, 4);
      curr.data_offset       = read_u64_le(buf, 8);
      curr.data_size         = read_u64_le(buf, 16);
      curr.uncompressed_size = read_u64_le(buf, 24);
      curr.compression       = read_u32_le(buf, 32);
      curr.type              = read_u32_le(buf, 36);
      curr.checksum          = read_u32_le(buf, 40);
      curr.flags             = read_u32_le(buf, 44);
      if(curr.name_length==0){
        return BUN_MALFORMED;
      }
      u64 offsetName = 0;
      if(!checked_add_u64(header->string_table_offset,(u64)curr.name_offset,&offsetName)){
        return BUN_MALFORMED;
      }
      if(!range_within_file(offsetName,(u64)curr.name_length, file_size)){
        return BUN_MALFORMED;
      }
      if (fseek(ctx->file, (long)(header->string_table_offset+curr.name_offset),SEEK_SET)!=0){
        return BUN_ERR_IO;
      }
      for (u32 i=0; i<curr.name_length; i++) {
        int ch = fgetc(ctx->file);
        if (ch==EOF) {
          return BUN_ERR_IO;
        }
        if(ch<32 || ch>126){
          return BUN_MALFORMED;
        }
      }

      if (!range_within_file(curr.data_offset, curr.data_size, header->data_section_size)) {
          return BUN_MALFORMED;
      }
      if(curr.compression==0){
        if(curr.uncompressed_size != 0){
          return BUN_MALFORMED;
        }
      }else if(curr.compression==1){
         if((curr.data_size%2)!=0){
          return BUN_MALFORMED;
        }
        u64 validOffData = 0;
        if(!checked_add_u64(header->data_section_offset,curr.data_offset,&validOffData)){
          return BUN_MALFORMED;
        }
        if(!range_within_file(validOffData,curr.data_size,file_size)){
          return BUN_MALFORMED;
        }
        if(fseek(ctx->file,(long)validOffData,SEEK_SET)!=0){
          return BUN_ERR_IO;
        }
        u64 bytesCurr = curr.data_size;
        u64 bytesOut = 0;
        while(bytesCurr>0){
          int count = fgetc(ctx->file);
          int value = fgetc(ctx->file);
          (void)value;
          if(count==EOF||value==EOF){
            return BUN_MALFORMED;
          }
          if(count==0){
            return BUN_MALFORMED;
          }
          u64 newBytes =0;
          if(!checked_add_u64(bytesOut,(u64)(u8)count,&newBytes)){
            return BUN_MALFORMED;
          }
          bytesOut = newBytes;
          if(bytesOut>curr.uncompressed_size){
            return BUN_MALFORMED;
          }
          bytesCurr-=2;
        }
        if(bytesOut!=curr.uncompressed_size){
          return BUN_MALFORMED;
        }
      }else if(curr.compression ==2){
        return BUN_UNSUPPORTED;
      }else{
        return BUN_UNSUPPORTED;
      }
      if((curr.flags&(BUN_FLAG_ENCRYPTED|BUN_FLAG_EXECUTABLE))!=curr.flags){
        return BUN_UNSUPPORTED;
      }

      
      if (curr.checksum != 0) {
        return BUN_UNSUPPORTED;
      }
    }

  return BUN_OK;
}


// Closes the BUN file and cleans up resources.
// @param ctx Parse context with open file handle
// @return BUN_OK on successful close, BUN_ERR_IO on close failure

bun_result_t bun_close(BunParseContext *ctx) {
  assert(ctx->file); // Ensure file handle is valid

  // Close the file and check for errors
  int res = fclose(ctx->file);
  if (res) {
    return BUN_ERR_IO;
  } else {
    ctx->file = NULL; // Clear file handle to prevent reuse
    return BUN_OK;
  }
}
