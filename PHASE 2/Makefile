# Makefile for CITS3007 group project phase 2 report.
#
# Replace "XX" in the filenames below with your group number.
# e.g. if your group is 3, change group-XX to group-03.

SOURCE = group-23-phase-2-report.md
OUTPUT = group-23-phase-2-report.pdf

PANDOC  = pandoc
PFLAGS  = --shift-heading-level-by=-1 --table-of-contents --pdf-engine=typst

.PHONY: all clean

all: $(OUTPUT)

$(OUTPUT): $(SOURCE)
	$(PANDOC) $(PFLAGS) -o $@ $<

clean:
	rm -f $(OUTPUT)
