---
title: |
  CITS3007 Secure Coding \
  group 23 phase 2 report
date: Semester 1, 2026
colorlinks: true
fontsize: 12pt
margin:
  x: 2.0cm
  y: 2.5cm
lang: en
papersize: a4
section-numbering: "1.1.1."
header-includes: |
  ```{=typst}
  // if you are using Typst, we provide some rules here which can slightly improve
  // the appearance -- but you can remove them or comment them out with C style comments
  // if you wish.

  // more space under headings
  #show heading: set block(below: 1.2em)

  // use en-dashes for all bullets
  #set list(marker: [--])

  // blue links
  #let blueish = rgb("#0000ff")
  #show link: set text(fill: blueish)

  #set text(
    historical-ligatures: false,
  )

  // if we shift heading by -1, slightly less loud heading sizes
  #show heading.where(level: 1): set text(size: 14pt)
  #show heading.where(level: 2): set text(size: 12pt)

  ```
---

<!--
  Replace all instances of "XX" with your group number.
  Replace all instances of "YY" with the group number of the codebase you tested.
  Delete or replace placeholder text in [square brackets] before submission.
-->

**Group 23 members:**

- Rohma Rehman, 23845362, \@romer072,
- Ritch Elbert Rayawang, 23940731, \@Ritch-Wang,
- Kavya Anil, 24217516, \@kvn776,
- Alan Ling, 23843412, \@AlanLingYS


## Introduction

**Target Selection: Group 6**

We selected Group 6 after comparing both codebases. Group 6 presented a significantly easier and more practical attack surface for vulnerability discovery. During initial testing, we were able to create a crafted `.bun` file that reliably caused a segmentation fault (exit code 139), demonstrating a clear and reproducible failure condition. This aligns strongly with Phase 2’s requirement for reproducible findings that can be consistently triggered and observed.

A major contributing factor is Group 6’s unsafe name buffer handling (line ~327: `char name[asset.name_length + 1]`), which allocates stack memory directly from an unchecked `u32` field. By supplying a maliciously large `name_length` value (for example, 1MB), an attacker can trigger immediate stack overflow behaviour. Combined with the relatively smaller 642-line codebase and fewer defensive layers, this provided a manageable scope for systematic vulnerability analysis and testing.

In contrast, Group 19 appeared substantially more robust. The same crafted `.bun` file executed normally with exit code 0, indicating stronger resilience against malformed input. Additionally, Group 19 includes more comprehensive testing utilities, helper code, structured error handling, overflow protections, and generally safer memory-management practices. While this reflects stronger engineering and defensive programming, it also significantly increases the difficulty of discovering exploitable flaws within the project timeframe, with a realistic possibility of identifying no vulnerabilities despite extensive effort.

For Phase 2, we prioritised a target that balanced realistic exploitability with achievable project scope. Group 6 therefore represented the most suitable choice for demonstrating concrete security findings and completing all mandatory specification requirements effectively.

## Assumptions and method

### General assumptions

- Assuming that chosen group's codefile works for all valid files, hence we are only testing on invalid files


### Testing approach and tools

[Describe how you systematically covered the mandatory parts of the BUN specification.
Explain what techniques and tools you used -- for example: manual crafting of test
inputs, fuzzing, Valgrind, sanitizers, GDB, static analysis. Note that tools are a
legitimate part of your process, but each finding must ultimately be reproducible via
normal parser execution.]


## Findings

<!-- Repeat the subsection below for each finding. If you found no flaws, replace this
     section with a description of the tests you ran and their outcomes, and present a
     well-argued case for the codebase's correctness. See the conclusion guidance. -->

### Finding F-01

- ID: F-01
- Category: [Crash / Excessive memory use / Hang / Incorrect output]
- Spec reference: [e.g. Section 9.2 -- Sections lie within file]
- Assumptions: [e.g. Any interpretation of the spec required for this to succeed]

**Description**

[A clear, concise description of the flaw.]

**Expected behaviour**

[What a correct parser should do in this case, with reference to the spec.]

**Actual behaviour**

[What the target parser actually does.]

**Reproduction steps**

[Step-by-step instructions. A marker must be able to follow these on the SDE and
observe the failure. Include:]

1. Build the target parser with the following flags: `[flags, or "default"]`
2. Run: `./bun_parser [input_file]`
3. [Additional steps if needed, e.g. memory limit via Docker]


Expected outcome: [e.g. exit with status 1 (`BUN_MALFORMED`)]

Actual outcome: [e.g. segmentation fault (exit status 139)]

<!-- You may provide Makefile targets for individual findings if desired -->

Alternatively, run `make reproduce_f1` in the reproduction package to execute this test
automatically.

<!-- Add further findings below by copying the subsection above. -->


### Finding F-01

- ID: F-01
- Category: Incorrect output
- Spec reference: Sect. 9.2 - sections and asset data ranges must lie within the declared file sections
- Assumptions: The `data_offset` and `data_size` fields are interpreted as unsigned 64-bit values. The asset payload range is expected to be fully contained within the declared data section. A parser should reject an asset if its data range starts outside the data section or extends past the end of the data section.

**Description**

The target parser contains an integer overflow vulnerability in its asset data range validation. The vulnerable check attempts to verify that an asset's payload is contained within the declared data section by adding `asset.data_offset` and `asset.data_size` directly:

```c
if (asset.data_offset + asset.data_size > header->data_section_size) {
    add_error(ctx, "Asset data range exceeds data section");
    return BUN_MALFORMED;
}
```

This validation is unsafe because the addition can overflow when `asset.data_offset` is close to the maximum value of an unsigned 64-bit integer. If the addition wraps around to a small value, the parser may incorrectly treat an impossible asset range as valid.

For this test case, the malicious BUN file uses the following asset values:

```text
data_offset = 18446744073709551608
data_size   = 16
```

The `data_offset` value is equivalent to `UINT64_MAX - 7`. When the parser computes `asset.data_offset + asset.data_size`, the calculation becomes:

```text
18446744073709551608 + 16
```

In unsigned 64-bit arithmetic, this wraps around to `8`. Since the declared data section size in the generated file is `20`, the vulnerable check effectively behaves as if it is checking:

```c
if (8 > 20)
```

This condition is false, so the parser incorrectly accepts the malformed asset even though the original `data_offset` is far outside the declared data section.

A second overflow can also occur when the parser calculates the real file offset of the asset data:

```c
u64 real_data_offset = header->data_section_offset + asset.data_offset;
```

In the reproduced output, `header->data_section_offset` is `116`. Adding this to the malicious `data_offset` value wraps around to `108`, which is the string table offset. As a result, the parser reads bytes from the string table instead of the data section.

**Expected behaviour**

A correct parser should reject this file as malformed. The asset data range is invalid because the asset claims to start at byte offset `18446744073709551608` inside a data section that is only `20` bytes long. The parser should detect that the data range cannot fit inside the declared data section and return `BUN_MALFORMED` without attempting to print or read the asset payload.

The expected error should be equivalent to:

```text
Asset data range exceeds data section
```

or another clear malformed-file error indicating that the asset data range is outside the data section.

**Actual behaviour**

The parser accepts the malformed file and prints a normal BUN file summary. The output shows that the parser accepts an impossible data range:

```text
Data section size:     20 bytes
Data offset:           18446744073709551608 bytes
Data size:             16 bytes
```

The parser also prints asset data bytes:

```text
Data (hex):            68 65 6c 6c 6f 00 00 00 48 65 6c 6c 6f 2c 20 42
```

The first five bytes, `68 65 6c 6c 6f`, decode to the ASCII string `hello`. This is the asset name stored in the string table, not the intended asset payload. This confirms that the overflow caused the parser to read from the wrong section of the file instead of rejecting the malformed asset.

**Reproduction steps**

The following steps reproduce the issue from a clean checkout of the target parser.

1. Build the target parser with the default project build command:

   ```sh
   make
   ```

   If compiling manually, use:

   ```sh
   gcc -std=c11 -Wall -Wextra -g -o bun_parse bun_parse.c
   ```

2. Modify `bunfile_generator.py` so that it writes the malicious output file:

   ```python
   out_path = Path("overflow_data_range.bun")
   ```

3. In `bunfile_generator.py`, define the malicious asset data range values:

   ```python
   malicious_data_offset = 0xfffffffffffffff8
   malicious_data_size   = 16
   ```

4. In the call to `write_asset_record`, use the malicious values for `data_offset` and `data_size`:

   ```python
   write_asset_record(
       f,
       name_offset = 0,
       name_length = len(asset_name),
       data_offset = malicious_data_offset,
       data_size   = malicious_data_size,
   )
   ```

5. Generate the malformed BUN file:

   ```sh
   python3 bunfile_generator.py
   ```

   This should create:

   ```text
   overflow_data_range.bun
   ```

6. Run the target parser on the generated file:

   ```sh
   ./bun_parse overflow_data_range.bun
   ```

Expected outcome: The parser should reject the file as malformed, returning `BUN_MALFORMED` or printing an error such as `Asset data range exceeds data section`.

Actual outcome: The parser accepts the malformed file and prints a BUN summary containing the impossible data offset `18446744073709551608`. It also prints data bytes from the wrong file section.

The important reproduced output is:

```text
=== BUN File Summary ===

--- HEADER ---
Magic:                 BUN0
Version:               1.0
Asset count:           1
Asset table offset:    60 bytes
String table offset:   108 bytes
String table size:     8 bytes
Data section offset:   116 bytes
Data section size:     20 bytes
Reserved:              0x0

--- Asset 1/1 ---
Name:                  hello
Name offset:           0 bytes
Name length:           5 bytes
Data offset:           18446744073709551608 bytes
Data size:             16 bytes
Uncompressed size:     0 bytes
Compression:           0 (None)
Type:                  0
Checksum:              0 (Unused)
Flags:                 0 (None)
Data (hex):            68 65 6c 6c 6f 00 00 00 48 65 6c 6c 6f 2c 20 42
```

Alternatively, run `make reproduce_f1` in the reproduction package if the submitted package includes this target.

**Suggested fix**

The vulnerable addition-based check should be replaced with an overflow-safe range check that avoids adding `asset.data_offset` and `asset.data_size` directly:

```c
if (asset.data_offset > header->data_section_size ||
    asset.data_size > header->data_section_size - asset.data_offset) {
    add_error(ctx, "Asset data range exceeds data section");
    return BUN_MALFORMED;
}
```

The later real file offset calculation should also be protected before performing the addition:

```c
if (asset.data_offset > UINT64_MAX - header->data_section_offset) {
    add_error(ctx, "Real data offset overflow");
    return BUN_MALFORMED;
}

u64 real_data_offset = header->data_section_offset + asset.data_offset;
```

After applying these checks, the same `overflow_data_range.bun` file should be rejected instead of parsed successfully.

## Conclusion

[Summarise your findings. If flaws were found, briefly characterise their nature -- are
they clustered around a particular part of the spec, or a particular failure category?

If no flaws were found, present a well-argued "clean bill of health": describe what
behaviours your tests covered and confirm that in each case the parser's actual
behaviour matched the expected behaviour. A thorough and well-evidenced conclusion of
this kind can receive full marks.

Recommendations to the target group are welcome but not required.]


