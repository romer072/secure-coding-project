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

In unsigned 64-bit arithmetic, this wraps around to `8`. Since the declared data section size in the generated file is small, the vulnerable check can incorrectly treat the wrapped result as being inside the data section. This condition is false, so the parser incorrectly accepts the malformed asset even though the original `data_offset` is far outside the declared data section.

A second overflow can also occur when the parser calculates the real file offset of the asset data:

```c
u64 real_data_offset = header->data_section_offset + asset.data_offset;
```

In the reproduced output, this causes the calculated file offset to wrap around into an earlier section of the file. As a result, the parser reads bytes from the string table instead of the data section.

**Expected behaviour**

A correct parser should reject this file as malformed. The asset data range is invalid because the asset claims to start at byte offset `18446744073709551608` inside a very small data section. The parser should detect that the data range cannot fit inside the declared data section and return `BUN_MALFORMED` without attempting to print or read the asset payload.

The expected error should be equivalent to:

```text
Asset data range exceeds data section
```

or another clear malformed-file error indicating that the asset data range is outside the data section.

**Actual behaviour**

The parser accepts the malformed file and prints a normal BUN file summary. The output shows that the parser accepts an impossible data range:

```text
Data offset:           18446744073709551608 bytes
Data size:             16 bytes
```

The parser also prints asset data from the wrong file location:

```text
Data (text):           a_offsetABCDEFGH
```

The printed text begins with part of the asset name, `a_offset`, followed by payload bytes. This confirms that the overflow caused the parser to read from the wrong section of the file instead of rejecting the malformed asset.

**Reproduction steps**

The following steps reproduce the issue from a clean checkout of the target parser.

1. Build the target parser with the default project build command:

   ```sh
   make
   ```

   If compiling manually, use:

   ```sh
   gcc -std=c11 -Wall -Wextra -g -o bun_parser bun_parse.c
   ```

2. Modify `bunfile_generator.py` so that it writes the malicious output file:

   ```python
   out_path = Path("overflow_data_offset.bun")
   ```

3. In `bunfile_generator.py`, define the malicious asset data range values:

   ```python
   malicious_data_offset = 0xfffffffffffffff8
   malicious_data_size   = 16
   ```

4. Use the malicious values in the asset record:

   ```python
   write_asset_record(
       f,
       name_offset = 0,
       name_length = len(asset_name),
       data_offset = malicious_data_offset,
       data_size   = malicious_data_size,
       uncompressed_size = 0,
       compression = COMPRESS_NONE,
   )
   ```

5. Generate the malformed BUN file:

   ```sh
   python3 bunfile_generator.py
   ```

   This should create:

   ```text
   overflow_data_offset.bun
   ```

6. Run the target parser on the generated file:

   ```sh
   ./bun_parser overflow_data_offset.bun
   ```

Expected outcome: The parser should reject the file as malformed, returning `BUN_MALFORMED` or printing an error such as `Asset data range exceeds data section`.

Actual outcome: The parser accepts the malformed file and prints a BUN summary containing the impossible data offset `18446744073709551608`. It also prints data from the wrong file section.

The important reproduced output is:

```text
=== BUN File Summary ===

--- HEADER ---
Magic:                 BUN0
Version:               1.0
Asset count:           1
Asset table offset:    60 bytes
String table offset:   108 bytes
String table size:     20 bytes
Data section offset:   128 bytes
Data section size:     16 bytes
Reserved:              0x0

--- Asset 1/1 ---
Name:                  overflow_data_offset
Name offset:           0 bytes
Name length:           20 bytes
Data offset:           18446744073709551608 bytes
Data size:             16 bytes
Uncompressed size:     0 bytes
Compression:           0 (None)
Type:                  0
Checksum:              0 (Unused)
Flags:                 0 (None)
Data (text):           a_offsetABCDEFGH
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

After applying these checks, the same `overflow_data_offset.bun` file should be rejected instead of parsed successfully.


### Finding F-02

- ID: F-02
- Category: Crash / excessive memory use
- Spec reference: Section 9.2 -- asset names must lie within the declared string table
- Assumptions: The `name_offset` and `name_length` fields are interpreted as values pointing into the string table. Although the name range must be inside the string table, a parser should also enforce a practical maximum asset name length before allocating memory to store the name.

**Description**

The target parser allocates the asset name using a variable-length array on the stack:

```c
char name[asset.name_length + 1];
```

This is unsafe because `asset.name_length` is controlled by the BUN file. The parser checks whether the name range lies inside the string table, but it does not appear to enforce a practical maximum name length before allocating the stack buffer. A malicious BUN file can therefore declare a very large string table and a very large `name_length`, making the range check pass while still causing the parser to allocate a huge buffer on the stack.

For this test case, the malicious BUN file uses a very large asset name:

```text
name_offset = 0
name_length = 16000000
string_table_size = 16000000
```

The name is technically within the declared string table, so the parser may accept the bounds check. However, when it reaches the stack allocation, it attempts to allocate approximately 16 MB for the asset name on the stack. This can exhaust the process stack and cause a crash.

**Expected behaviour**

A correct parser should reject this file as malformed before allocating the asset name buffer. Even if the name range is inside the declared string table, the parser should enforce a reasonable maximum asset name length.

The expected error should be equivalent to:

```text
Asset name length is too large
```

or another clear malformed-file error indicating that the asset name length exceeds an implementation-defined safe limit.

**Actual behaviour**

The parser prints the header successfully, but then crashes before printing the asset record. This shows that the header and section layout are accepted, and the failure occurs while parsing the asset record. The crash is consistent with the parser attempting to allocate a stack buffer based on the file-controlled `name_length` value.

In the reproduced test, the file declares a string table size of `16000000` bytes and an asset name length of `16000000` bytes. Because the declared name range fits inside the string table, the parser proceeds past the bounds check. It then reaches the vulnerable stack allocation and terminates with a segmentation fault.

The vulnerable operation is:

```c
char name[asset.name_length + 1];
```

Since `asset.name_length` is taken from the input file, the parser should not use it directly for stack allocation without enforcing a maximum.

The important reproduced output is:

```text
=== BUN File Summary ===

--- HEADER ---
Magic:                 BUN0
Version:               1.0
Asset count:           1
Asset table offset:    60 bytes
String table offset:   108 bytes
String table size:     16000000 bytes
Data section offset:   16000108 bytes
Data section size:     4 bytes
Reserved:              0x0

zsh: segmentation fault  ./bun_parser huge_name_stack_crash.bun
```

This demonstrates a concrete crash rather than only a theoretical issue.

**Reproduction steps**

The following steps reproduce the issue from a clean checkout of the target parser.

1. Build the target parser with the default project build command:

   ```sh
   make
   ```

   If compiling manually, use:

   ```sh
   gcc -std=c11 -Wall -Wextra -g -o bun_parser bun_parse.c
   ```

2. Modify `bunfile_generator.py` so that it writes the malicious output file:

   ```python
   out_path = Path("huge_name_stack_crash.bun")
   ```

3. In `bunfile_generator.py`, define a very large asset name and a small valid payload:

   ```python
   huge_name_length = 16_000_000
   asset_name = b"A" * huge_name_length
   asset_payload = b"DATA"
   asset_count = 1
   ```

4. Ensure the asset record uses the large name length and a normal uncompressed payload:

   ```python
   write_asset_record(
       f,
       name_offset = 0,
       name_length = len(asset_name),
       data_offset = 0,
       data_size   = len(asset_payload),
       uncompressed_size = 0,
       compression = COMPRESS_NONE,
   )
   ```

5. Generate the malformed BUN file:

   ```sh
   python3 bunfile_generator.py
   ```

   This should create:

   ```text
   huge_name_stack_crash.bun
   ```

6. Run the target parser on the generated file:

   ```sh
   ./bun_parser huge_name_stack_crash.bun
   ```

Expected outcome: The parser should reject the file as malformed before allocating the name buffer, returning `BUN_MALFORMED` or printing an error such as `Asset name length is too large`.

Actual outcome: The parser prints the header and then terminates with a segmentation fault:

```text
zsh: segmentation fault  ./bun_parser huge_name_stack_crash.bun
```

Alternatively, run `make reproduce_f2` in the reproduction package if the submitted package includes this target.

**Suggested fix**

The parser should enforce a maximum asset name length before allocating memory for the name. For example:

```c
#define MAX_NAME_LENGTH 4096

if (asset.name_length > MAX_NAME_LENGTH) {
    add_error(ctx, "Asset name length is too large");
    return BUN_MALFORMED;
}
```

If the parser needs to support longer names, it should still avoid variable-length stack allocation. A safer approach is to allocate on the heap after checking a reasonable maximum size:

```c
char *name = malloc((size_t)asset.name_length + 1);
if (name == NULL) {
    add_error(ctx, "Failed to allocate asset name");
    return BUN_ERR_IO;
}
```

## Finding F-03

- ID: F-03
- Category: Incorrect output / invalid file accepted
- Spec reference: Section 4.1 -- BUN header fields; reserved header field must be zero
- Assumptions: The BUN specification requires the header `reserved` field to be set to zero. A parser should reject files where reserved fields contain non-zero values, because reserved fields are reserved for future use and should not contain arbitrary data in a valid BUN file.

**Description**

The target parser accepts a BUN file where the header `reserved` field is set to a non-zero value. The generated test file `bad_reserved.bun` is otherwise structurally valid, but the header contains the following reserved value:

```text
reserved = 0x12345678
```

The parser correctly reads and prints the reserved field, but it does not validate that the value is zero. This means a malformed header is treated as valid input. If the BUN specification requires the reserved field to be zero, the parser should reject the file during header validation rather than continuing to parse and print the asset records.

The parser reads the reserved field from the header:

```c
header->reserved = read_u64_le(buf, 52);
```

However, there is no corresponding validation such as:

```c
if (header->reserved != 0) {
    return BUN_MALFORMED;
}
```

As a result, a file with a non-zero reserved field is still accepted and displayed as a valid BUN file.

This is an incorrect output issue because the parser produces a normal successful file summary for an input that should have been rejected as malformed. The parser does not crash, but it gives the wrong result by accepting and displaying an invalid BUN file as if it were valid.

**Expected behaviour**

A correct parser should reject `bad_reserved.bun` as malformed during header validation. Since the `reserved` field is non-zero, the parser should return `BUN_MALFORMED` and print an error indicating that the reserved header field must be zero.

The expected error should be equivalent to:

```text
Reserved field must be zero
```

or another clear malformed-file error indicating that the header contains an invalid reserved value.

**Actual behaviour**

The parser accepts the malformed file and prints a normal BUN file summary. This is incorrect because the parser should reject the header before printing asset information. The output shows that the parser reads and displays the non-zero reserved value:

```text
Reserved:              0x12345678
```

The parser then continues to parse the asset record and prints the asset data:

```text
--- Asset 1/1 ---
Name:                  bad_reserved
Name offset:           0 bytes
Name length:           12 bytes
Data offset:           0 bytes
Data size:             4 bytes
Uncompressed size:     0 bytes
Compression:           0 (None)
Type:                  0
Checksum:              0 (Unused)
Flags:                 0 (None)
Data (text):           DATA
```

This confirms that the parser accepts an invalid reserved header field instead of rejecting the malformed file.

**Reproduction steps**

The following steps reproduce the issue from a clean checkout of the target parser using the original `bunfile_generator.py` script.

1. Build the target parser with the default project build command:

   ```sh
   make
   ```

   If compiling manually, use:

   ```sh
   gcc -std=c11 -Wall -Wextra -g -o bun_parser bun_parse.c
   ```

2. Modify `bunfile_generator.py` so that it writes the malicious output file:

   ```python
   out_path = Path("bad_reserved.bun")
   ```

3. In `bunfile_generator.py`, define a non-zero reserved header value:

   ```python
   bad_reserved_value = 0x12345678
   ```

4. In the call to `write_header`, pass the non-zero reserved value:

   ```python
   write_header(
       f,
       asset_count         = asset_count,
       asset_table_offset  = asset_table_offset,
       string_table_offset = string_table_offset,
       string_table_size   = string_table_size,
       data_section_offset = data_section_offset,
       data_section_size   = data_section_size,
       reserved            = bad_reserved_value,
   )
   ```

5. Keep the asset record as a simple valid uncompressed asset. This ensures that the only malformed part of the file is the non-zero reserved header field:

   ```python
   asset_name = b"bad_reserved"
   asset_payload = b"DATA"

   write_asset_record(
       f,
       name_offset = 0,
       name_length = len(asset_name),
       data_offset = 0,
       data_size   = len(asset_payload),
       uncompressed_size = 0,
       compression = COMPRESS_NONE,
   )
   ```

6. Generate the malformed BUN file:

   ```sh
   python3 bunfile_generator.py
   ```

   This should create:

   ```text
   bad_reserved.bun
   ```

7. Run the target parser on the generated file:

   ```sh
   ./bun_parser bad_reserved.bun
   ```

Expected outcome: The parser should reject the file as malformed, returning `BUN_MALFORMED` or printing an error such as `Reserved field must be zero`.

Actual outcome: The parser accepts the malformed file and prints a normal BUN summary containing the invalid reserved value `0x12345678`.

The important reproduced output is:

```text
=== BUN File Summary ===

--- HEADER ---
Magic:                 BUN0
Version:               1.0
Asset count:           1
Asset table offset:    60 bytes
String table offset:   108 bytes
String table size:     12 bytes
Data section offset:   120 bytes
Data section size:     4 bytes
Reserved:              0x12345678

--- Asset 1/1 ---
Name:                  bad_reserved
Name offset:           0 bytes
Name length:           12 bytes
Data offset:           0 bytes
Data size:             4 bytes
Uncompressed size:     0 bytes
Compression:           0 (None)
Type:                  0
Checksum:              0 (Unused)
Flags:                 0 (None)
Data (text):           DATA
```

Alternatively, run `make reproduce_f3` in the reproduction package if the submitted package includes this target.

**Suggested fix**

The parser should validate the reserved header field immediately after reading the header fields and before accepting the header as valid:

```c
if (header->reserved != 0) {
    add_error(ctx, "Reserved field must be zero");
    return BUN_MALFORMED;
}
```

After applying this check, the same `bad_reserved.bun` file should be rejected during header parsing instead of being printed as a valid BUN file. This would change the parser behaviour from accepting an invalid header to correctly returning `BUN_MALFORMED`.

### Finding F- --

- ID: F- --
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

## Conclusion

[Summarise your findings. If flaws were found, briefly characterise their nature -- are
they clustered around a particular part of the spec, or a particular failure category?

If no flaws were found, present a well-argued "clean bill of health": describe what
behaviours your tests covered and confirm that in each case the parser's actual
behaviour matched the expected behaviour. A thorough and well-evidenced conclusion of
this kind can receive full marks.

Recommendations to the target group are welcome but not required.]