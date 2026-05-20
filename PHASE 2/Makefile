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

.PHONY: all clean reproduce reproduce_valid reproduce_stack reproduce_name_overflow

all: $(OUTPUT)

$(OUTPUT): $(SOURCE)
	$(PANDOC) $(PFLAGS) -o $@ $<

reproduce: reproduce_valid reproduce_stack reproduce_name_overflow

reproduce_valid:
	@echo "Running valid boundary test: test_max_name.bun"
	cd $(TARGET_DIR) && ./bun_parser ../tests/fixtures/valid/test_max_name.bun

reproduce_stack:
	@echo "Running vulnerability test: stack overflow"
	cd $(TARGET_DIR) && ./bun_parser ../tests/fixtures/invalid/test_vuln_stack_overflow.bun || true

reproduce_name_overflow:
	@echo "Running invalid test: name offset + length overflow"
	cd $(TARGET_DIR) && ./bun_parser ../tests/fixtures/invalid/test_vuln_name_overflow.bun || true

clean:
	rm -f $(OUTPUT)
