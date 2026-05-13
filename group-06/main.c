#include <stdio.h>
#include <stdlib.h>

#include "bun.h"


static void print_errors(const char *prefix, const BunParseContext *ctx) {
  if (ctx->error_count > 0) {
    for (int i = 0; i < ctx->error_count; i++) {
      fprintf(stderr, "%s: %s\n", prefix, ctx->error_msgs[i]);
    }
  } else {
    fprintf(stderr, "%s: unknown error\n", prefix);
  }
}

int main(int argc, char *argv[]) {
  if (argc != 2) {
    fprintf(stderr, "Usage: %s <file.bun>\n", argv[0]);
    return 3;
  }
  
  const char *path = argv[1];

  BunParseContext ctx = {0};
  BunHeader header  = {0};

  bun_result_t result = bun_open(path, &ctx);
  if (result != BUN_OK) {
    print_errors("Open error", &ctx);
    return result;
  }

  result = bun_parse_header(&ctx, &header);
  if (result != BUN_OK) {
    print_errors("Header error", &ctx);
    bun_close(&ctx);
    return result;
  }

  // Header is valid; print it
  bun_print_header(&header);

  result = bun_parse_assets(&ctx, &header);
  if (result != BUN_OK) {
    print_errors("Asset error", &ctx);
    bun_close(&ctx);
    return result;
  }

  bun_close(&ctx);
  if (result != BUN_OK) {
    print_errors("Error", &ctx);
    return result;
  }
  
  return 0;
}
