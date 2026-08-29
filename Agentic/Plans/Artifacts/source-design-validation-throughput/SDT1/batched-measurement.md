# SDT1 Batched Query Measurement

Date: 2026-08-30

SDT1 executes all four existing Clang Query matchers in one session for each
source/context pair. Matcher text, binding locations, source selection, compile
contexts, compiler arguments, Tidy configuration, thresholds, and exit classes
remain unchanged.

## Exact Reference Workload

- Git base: `7376c69ae6aa32c570dc1ee95fa96e958ba1de42`
- Git head: `26baa61e8b92173db46200c9bd306a344b31364f`
- Selected files: 79
- Distinct contexts: 623
- Tidy launches: 623
- Batched Query launches: 623
- Total LLVM launches: 1,246
- Findings: 0
- Infrastructure errors: 0
- Elapsed: 1,480.681 seconds
- Context discovery: 32.189 seconds
- Tidy aggregate: 678.497 seconds
- Query aggregate: 769.943 seconds

The same workload took 3,274.505 and 3,275.521 seconds in SDT0 with 3,115
launches. Batching alone is 2.21 times faster than the first serial reference
run while retaining the exact 79/623 selection.

## Parity And Negative Controls

A five-context clean source scan retained exit 0, zero findings, and the same
source/context counts while Query launches fell from 20 to 5 and elapsed time
fell from 15.856 to 7.350 seconds.

One planted source triggers Tidy plus all four Query rules across 14 contexts.
The pre-change and batched checkers both return policy exit 1. After excluding
only the appended volatile summary, stdout is byte-identical and stderr is
byte-identical at SHA-256
`43e42af4a712ca0a920cef9785c2cec28a661dbd2afb5bb8cf7934709acc4404`.

The self-test now covers every Query rule independently, two rules in one file,
all four rules in the planted parity file, a missing command inventory, fewer
completed rule sections, truncated bound locations, and a real malformed Query
command. Missing or partial work returns infrastructure exit 2 instead of a
clean result.

No source, context, matcher, threshold, test, coverage, Physics evidence, or
golden baseline changed.
