# Makefile for CITS3007 group project phase 2 report.
#
# Replace "XX" in the filenames below with your group number.
# e.g. if your group is 3, change group-XX to group-03.

SOURCE = group-23-phase-2-report.md
OUTPUT = group-23-phase-2-report.pdf

PANDOC  = pandoc
PFLAGS  = --shift-heading-level-by=-1 --table-of-contents --pdf-engine=typst

TARGET_DIR = target
PARSER = $(TARGET_DIR)/bun_parser

.PHONY: all clean reproduce reproduce_f01 reproduce_f02 reproduce_f03 reproduce_f04

all: $(OUTPUT)

$(OUTPUT): $(SOURCE)
	$(PANDOC) $(PFLAGS) -o $@ $<

reproduce: reproduce_f01 reproduce_f02 reproduce_f03 reproduce_f04

reproduce_f01:
	@echo "Running F-01: overflow data offset"
	cd $(TARGET_DIR) && ./bun_parser ../tests/fixtures/01overflow_data_offset.bun || true

reproduce_f02:
	@echo "Running F-02: huge name stack crash"
	cd $(TARGET_DIR) && ./bun_parser ../tests/fixtures/02huge_name_stack_crash.bun || true

reproduce_f03:
	@echo "Running F-03: bad reserved field"
	cd $(TARGET_DIR) && ./bun_parser ../tests/fixtures/03bad_reserved.bun || true

reproduce_f04:
	@echo "Running F-04: RLE mismatch tests"
	cd $(TARGET_DIR) && ./bun_parser ../tests/fixtures/04test_rle_zero_uncompressed.bun || true
	cd $(TARGET_DIR) && ./bun_parser ../tests/fixtures/04test_rle_size_mismatch_too_high.bun || true
	cd $(TARGET_DIR) && ./bun_parser ../tests/fixtures/04test_rle_size_mismatch_too_low.bun || true

clean:
	rm -f $(OUTPUT)
