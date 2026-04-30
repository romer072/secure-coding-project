#!/usr/bin/env bash
set -u

mkdir -p docs/evidence

echo "=== AddressSanitizer run ===" > docs/evidence/asan-all.log

make clean
make CFLAGS="-std=c11 -Wall -Wextra -Wpedantic -g -O1 -fsanitize=address -fno-omit-frame-pointer" all

for f in tests/samples/valid/*.bun tests/samples/invalid/*.bun; do
  echo "" >> docs/evidence/asan-all.log
  echo "=== Testing $f ===" >> docs/evidence/asan-all.log
  ./bun_parser "$f" >> docs/evidence/asan-all.log 2>&1
  echo "exit code: $?" >> docs/evidence/asan-all.log
done

echo "" >> docs/evidence/asan-all.log
echo "=== AddressSanitizer run completed ===" >> docs/evidence/asan-all.log


echo "=== UndefinedBehaviorSanitizer run ===" > docs/evidence/ubsan-all.log

make clean
make CFLAGS="-std=c11 -Wall -Wextra -Wpedantic -g -O1 -fsanitize=undefined -fno-sanitize-recover=undefined" all

for f in tests/samples/valid/*.bun tests/samples/invalid/*.bun; do
  echo "" >> docs/evidence/ubsan-all.log
  echo "=== Testing $f ===" >> docs/evidence/ubsan-all.log
  ./bun_parser "$f" >> docs/evidence/ubsan-all.log 2>&1
  echo "exit code: $?" >> docs/evidence/ubsan-all.log
done

echo "" >> docs/evidence/ubsan-all.log
echo "=== UndefinedBehaviorSanitizer run completed ===" >> docs/evidence/ubsan-all.log

echo "Done."
echo "ASan log:  docs/evidence/asan-all.log"
echo "UBSan log: docs/evidence/ubsan-all.log"