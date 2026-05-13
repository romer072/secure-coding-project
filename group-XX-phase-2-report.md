---
title: |
  CITS3007 Secure Coding \
  group XX phase 2 report
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

**Group XX members:**

- Firstname Lastname, 12345678, \@username,
- Firstname Lastname, 12345678, \@username,
- Firstname Lastname, 12345678, \@username,
- Firstname Lastname, 12345678, \@username,


## Introduction

[Identify which codebase you chose to test (group number) and briefly explain why you
chose it over the alternative assignment. Include any general observations about the
codebase -- its structure, apparent coding practices, use of libraries -- that are
relevant to your testing approach.]


## Assumptions and method

### General assumptions

[State any general assumptions made during testing. For example: the compilation flags
used, any interpretations of the BUN specification that informed your approach.]

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


## Conclusion

[Summarise your findings. If flaws were found, briefly characterise their nature -- are
they clustered around a particular part of the spec, or a particular failure category?

If no flaws were found, present a well-argued "clean bill of health": describe what
behaviours your tests covered and confirm that in each case the parser's actual
behaviour matched the expected behaviour. A thorough and well-evidenced conclusion of
this kind can receive full marks.

Recommendations to the target group are welcome but not required.]


