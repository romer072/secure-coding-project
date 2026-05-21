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
---
# CITS3007 Phase 2 Findings Report

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

- We assume the CITS3007 SDE (Ubuntu 20.04, x86-64) represents the intended execution environment and that all findings must be reproducible in this environment.
- We assume Group-06's parser correctly handles all valid BUN files conforming to the specification, and therefore focus our testing on malformed and adversarial inputs designed to trigger edge cases and vulnerabilities.
- We assume unsigned integer arithmetic in C follows the standard well-defined behavior where overflow wraps modulo 2^n for n-bit types.
- We assume the parser is compiled with default C11 flags and standard C library functions (fread, malloc, etc.) behave as documented.
- We interpret the BUN specification to require strict validation of all header fields, section boundaries, and data ranges before accepting and processing a file.


### Testing approach and tools

We used code review, targeted malformed input generation, and repeatable reproduction to evaluate the parser.

- Reviewed `bun_parse.c`, `bun.h`, and `main.c` for unsafe arithmetic, unchecked bounds, variable-sized allocation, and compression validation.
- Used `bunfile_generator.py` to produce custom malformed fixtures for each finding.
- Packaged all fixtures in the reproduction package and added dedicated Makefile targets for each test.

Systematic Test Input Generation

- Modified `bunfile_generator.py` to craft malformed BUN files targeting specific vulnerabilities

Coverage by finding:

- F-01: `01overflow_data_offset.bun`
- F-02: `02huge_name_stack_crash.bun`
- F-03: `03bad_reserved.bun`
- F-04: `04test_rle_size_mismatch_too_high.bun`, `04test_rle_size_mismatch_too_low.bun`
- F-05: `05test_rle_zero_uncompressed.bun`

Sanitizer validation:

- Built the parser with AddressSanitizer and UndefinedBehaviorSanitizer for all findings.
- Verified F-01, F-03, F-04, and F-05 against the provided fixtures; these cases behaved as expected and did not trigger sanitizer failures.
- Only F-02 triggered sanitizer-detected behaviour: a stack overflow on the insecure name allocation path.

## Findings


### Finding F-01 Data Offset Integer Overflow

- ID: F-01
- Category: Incorrect output
- Spec reference: Section 9, Rule 5 (Asset names and data lie within respective sections)
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

**Option 1: Using the Makefile**

The test file `01overflow_data_offset.bun` is already provided and included in the reproduction package.

1. Navigate to the reproduction package directory.

2. Build the target parser:

   ```sh
   make
   ```

3. Run the reproduction test:

   ```sh
   make reproduce_f01
   ```

**Option 2: Generating the test file with bunfile_generator.py**

If you need to generate the test file from scratch:

1. Build the target parser:

   ```sh
   make
   ```

2. Modify `bunfile_generator.py` to generate the overflow test file:

   ```python
   out_path = Path("overflow_data_offset.bun")
   malicious_data_offset = 0xfffffffffffffff8  # UINT64_MAX - 7
   malicious_data_size = 16

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

3. Generate the BUN file:

   ```sh
   python3 bunfile_generator.py
   ```

4. Run the target parser on the generated file:

   ```sh
   ./bun_parser overflow_data_offset.bun
   ```

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

### Finding F-02 Name Allocation Stack Overflow

- ID: F-02
- Category: Crash / excessive memory use
- Spec reference: Section 5 (Asset Entry Table), Section 6 (String Table), and Section 9, Rule 5 (Asset names and data lie within respective sections)
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

In the reproduced test, the file declares a string table size of `16000000` bytes and an asset name length of `16000000` bytes. Because the declared name range fits inside the string table, the parser proceeds past the bounds check and then triggers a stack overflow during asset parsing.

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

AddressSanitizer:DEADLYSIGNAL
=================================================================
==2557==ERROR: AddressSanitizer: stack-overflow on address 0x7ffec8e08c08 (pc 0x6337ce2e89cf bp 0x7ffec9606e30 sp 0x7ffec8e07c10 T0)
    #0 0x6337ce2e89cf in bun_parse_assets /mnt/c/Users/rohma/Desktop/cits3007 secure coding/proj/phase 2/proj p2/PHASE 2/target/bun_parse.c:269
    #1 0x6337ce2e4cf0 in main /mnt/c/Users/rohma/Desktop/cits3007 secure coding/proj/phase 2/proj p2/PHASE 2/target/main.c:44
    #2 0x7cf13cc2a1c9  (/lib/x86_64-linux-gnu/libc.so.6+0x2a1c9)
    #3 0x7cf13cc2a28a in __libc_start_main (/lib/x86_64-linux-gnu/libc.so.6+0x2a28a)
    #4 0x6337ce2e4524 in _start (/mnt/c/Users/rohma/Desktop/cits3007 secure coding/proj/phase 2/proj p2/PHASE 2/target/bun_parser+0xb524)

SUMMARY: AddressSanitizer: stack-overflow /mnt/c/Users/rohma/Desktop/cits3007 secure coding/proj/phase 2/proj p2/PHASE 2/target/bun_parse.c:269 in bun_parse_assets
==2557==ABORTING
```

This demonstrates a concrete stack overflow detected by AddressSanitizer when the parser is built with ASan/UBSan.

**Reproduction steps**

**Option 1: Using the Makefile**

The test file `huge_name_stack_crash.bun` is already provided and included in the reproduction package.

1. Navigate to the reproduction package directory.

2. Build the target parser with ASan/UBSan:

   ```sh
   make reproduce
   ```

3. Run the F-02 target:

   ```sh
   make reproduce_f02
   ```

**Option 2: Generating the test file with bunfile_generator.py**

If you need to generate the test file from scratch:

1. Build the target parser with ASan/UBSan:

   ```sh
   cd target
   make clean && make CFLAGS="-std=c11 -Wall -Wextra -g -fsanitize=address,undefined -fno-omit-frame-pointer"
   ```

2. Modify `bunfile_generator.py` to generate the crash test file:

   ```python
   out_path = Path("huge_name_stack_crash.bun")
   huge_name_length = 16_000_000
   asset_name = b"A" * huge_name_length
   asset_payload = b"DATA"
   asset_count = 1

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

3. Generate the malformed BUN file:

   ```sh
   python3 bunfile_generator.py
   ```

4. Run the target parser on the generated file:

   ```sh
   ./bun_parser huge_name_stack_crash.bun
   ```

### Finding F-03 Reserved Field Validation

- ID: F-03
- Category: Incorrect output / invalid file accepted
- Spec reference: Section 4.1, Note 6 (Reserved field contents are ignored)
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

**Option 1: Using the Makefile**

The test file `bad_reserved.bun` is already provided and included in the reproduction package.

1. Navigate to the reproduction package directory.

2. Build the target parser:

   ```sh
   make
   ```

3. Run the reproduction test:

   ```sh
   make reproduce_f03
   ```

**Option 2: Generating the test file with bunfile_generator.py**

If you need to generate the test file from scratch:

1. Build the target parser:

   ```sh
   make
   ```

2. Modify `bunfile_generator.py` to generate the reserved-field test file:

   ```python
   out_path = Path("bad_reserved.bun")
   bad_reserved_value = 0x12345678

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

3. Keep the asset record as a simple valid uncompressed asset:

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

4. Generate the malformed BUN file:

   ```sh
   python3 bunfile_generator.py
   ```

   This should create:

   ```text
   bad_reserved.bun
   ```

5. Run the target parser on the generated file:

   ```sh
   ./bun_parser bad_reserved.bun
   ```

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

### Finding F-04 Size Mismatch RLE Integer Overflow

- ID: F-04
- Category: Incorrect output / theoretical overflow
- Spec reference: Section 5.1 (RLE Compression Format)
- Assumptions: The parser validates RLE compressed data by accumulating count values from each (count, value) pair and comparing the total to the declared `uncompressed_size` field. The parser uses u64 arithmetic without explicit overflow checking. We interpret the spec to require that the parser correctly validate that the sum of RLE pair counts matches the declared uncompressed size.

**Description**

The target parser contains a potential integer overflow weakness in its RLE validation logic. It accumulates RLE pair counts using unsigned 64-bit arithmetic without checking for wraparound. In theory, this could be exploited only by an RLE payload whose total count exceeds 2^64, which is not practically attainable in this project context.

Because the problematic condition cannot be reached with any realistic test file, the parser was assessed as having passed this finding based on available test cases and practical constraints.

**Expected behaviour**

A secure parser should detect or prevent overflow while accumulating RLE counts, and it should reject files whose computed total does not match `uncompressed_size`.

**Actual behaviour**

The parser correctly rejects the available RLE mismatch test fixtures:

- `04test_rle_size_mismatch_too_high.bun`: Declared uncompressed size is larger than the actual decompressed content, and the parser rejects it.
- `04test_rle_size_mismatch_too_low.bun`: Declared uncompressed size is smaller than the actual decompressed content, and the parser rejects it.

Because the only remaining weakness is theoretical and requires an impractical file size, our group
deemed this finding passed for the evaluated parser.

**Reproduction steps**

**Using the Makefile**

1. Navigate to the reproduction package directory.
2. Build the target parser:

   ```sh
   make
   ```
3. Run the reproduction tests:

   ```sh
   make reproduce_f04
   ```

   Practical exploitation of the theoretical overflow is not possible within the available test suite or the project’s realistic file-size constraints.

### F-05: RLE Zero Uncompressed Size

- ID: F-05
- Category: Incorrect output / mitigation verification
- Spec reference: Section 5.1 (RLE Compression Format)
- Assumptions: The BUN specification requires that RLE-compressed assets have a valid `uncompressed_size` > 0. We tested whether the parser rejects RLE assets when the declared `uncompressed_size` is zero.

**Description**

We evaluated a malformed RLE asset with `uncompressed_size = 0` to verify whether the parser treats it as invalid. This is a practical validation test rather than a theoretical overflow issue, and it checks whether the parser rejects clearly malformed RLE metadata.

**Expected behaviour**

A properly implemented parser should reject the file as malformed and return `BUN_MALFORMED` instead of accepting or processing the RLE data.

**Actual behaviour**

The parser rejects the available fixture `05test_rle_zero_uncompressed.bun`, demonstrating correct handling for this malformed RLE case. Because the parser already rejects this invalid input in the provided test suite, this finding is considered passed.

**Reproduction steps**

**Using the Makefile**

1. Navigate to the reproduction package directory.
2. Build the target parser:

   ```sh
   make
   ```
3. Run the reproduction tests:

   ```sh
   make reproduce_f05
   ```

## Conclusion

We identified three security vulnerabilities in the target parser across multiple categories and specification areas:
- F-01: Integer Overflow in Data Offset Validation (Incorrect output)
   - Vulnerability in offset arithmetic without overflow checking

- F-02: Stack Overflow via Unbounded Name Allocation (Crash)
   - Vulnerability in memory allocation without size bounds

- F-03: Missing Reserved Header Field Validation (Incorrect output)
   - Parser accepts non-zero reserved field values

And tested for two more security vulnerabilities that were accounted for by the target group's code:

- F-04: RLE Integer Overflow - Potential Overflow in Accumulation (Incorrect output)

   - Vulnerability in RLE pair count accumulation without overflow protection
   - Theoretical issue (would require 2^64 byte file); parser correctly validates all practical test cases

- F-05: RLE Zero Uncompressed Size

   - Verified mitigated: parser rejects zero uncompressed size RLE input in the provided test fixture

### Suggested fixes

- F-01: Replace overflow-prone data-range checks with boundary-safe comparisons and protect the real data offset calculation from unsigned wraparound.
- F-02: Enforce a maximum asset name length before allocation and avoid variable-length stack buffers for file-controlled sizes.
- F-03: Reject headers with non-zero reserved values during validation rather than accepting them as valid.
- F-04: Add explicit overflow checks when accumulating RLE run lengths, and validate RLE pair counts before accepting compressed asset metadata.
- F-05: Enforce RLE constraints such as non-zero uncompressed_size and reject malformed compressed payloads early in parsing.
