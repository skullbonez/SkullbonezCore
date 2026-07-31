# Runtime Contract Hygiene — CH2 And Plan Closure

Date: 2026-07-31
Plan: `Agentic/Plans/TODO/runtime-contract-hygiene.md`
Branch: `nightrunner-30th-JUL-26`

## Outcome

`PhysicsFixedList` now requires every element type to be nothrow move
constructible. All live engine and test instantiations compile under that
constraint. Trivially copyable rows retain their existing `memcpy` relocation
path. Non-trivial rows now construct the replacement prefix with direct moves,
then destroy the old prefix after every replacement exists.

The `_CPPUNWIND` try/catch cleanup and its final rethrow are deleted. Allocation
still happens before relocation, and the element contract makes the remaining
relocation work unable to unwind, so no partial destination can escape and no
new Lane F path is necessary.

## Focused Coverage

The retired fixture deliberately supplied throwing copy and move constructors,
which is no longer a valid fixed-list element contract. Its replacement is a
non-trivial type with deleted copy operations and a `noexcept` move constructor.
The focused case verifies:

- exactly two relocation move constructions;
- capacity growth from three to five;
- both values and the two-row live prefix survive;
- clearing and destruction return the live-instance count to zero;
- the allocator capacity row publishes capacity five, live count zero, and
  session high-water two.

The focused filter passes 1 case / 12 assertions. The complete Profile test
harness passes 457 cases / 2,424,712 assertions.

## Zero-Throw And Allocation Evidence

A source search across engine `.cpp`, `.h`, `.hpp`, `.inl`, and `.hlsl` files
finds zero throw expressions. The remaining eight `throw` tokens in
`SkullbonezSource` are explanatory comments about non-throwing behavior.

The allocation-policy synthetic suite passes. The repository scan covers 467
files and reports 37 direct-heap findings, 85 dynamic-STL-member findings, and
625 STL-growth findings, all accounted for with zero allowlist errors. No
allocation owner, phase, cap, allowlist row, or capacity reason changed.

## Physics Exactness

`tools\validate_physics.bat` passes its Debug build, standalone lifecycle smoke,
regression scene, committed-baseline comparison, and ready-build checks. The
retained two-run artifact is exactly the 44,401-row committed baseline repeated
twice:

- baseline SHA-256:
  `7f6b88b290f102e57345f894a4c27c2a9201eed74ccfc1e5d213488031b72572`;
- two-run artifact SHA-256:
  `c85de5beccd0b4ed8129da8f502c1a1223ed2f8d0c201481c9ed6e1680a40e7f`;
- rows/bytes: 88,802 / 12,740,176.

No baseline, golden, schema, configuration, allowlist, or committed runtime
artifact changed.

## Validation

| Command | Result |
|---|---|
| `tools\validate_format.bat` | PASS; 575 source files and 320 headers clean |
| focused non-trivial relocation filter | PASS; 1 case / 12 assertions |
| `tools\validate_tests.bat` | PASS; 457 cases / 2,424,712 assertions |
| `tools\validate_physics.bat` | PASS; committed Physics baseline byte-exact |
| `python tools\check_allocation_policy.py --self-test` | PASS |
| `python tools\check_allocation_policy.py --repo .` | PASS; 467 files, zero allowlist errors |
| `tools\validate_full.bat` | PASS in 652.3 seconds; CPU, coverage, DX12/runtime, and Physics lanes |

## Comment Audit

Both 2/2 touched source-bearing files were inspected against the comment style
guide. `PhysicsFixedList.h` states the nothrow element contract in its learning
header and explains the allocation/relocation lifetime invariant beside the
non-trivial loop. `TestReserveAllocator.cpp` records the non-trivial relocation
and exact-retirement invariant. No file is deferred.

## Independent Review

The read-only closure review returned **ACCEPT/CLEAR** with no blocker or
non-blocking observation. It confirmed the class constraint, relocation
lifetime proof, test sensitivity, zero engine throw-expression claim, unchanged
allocation and trivial byte-transfer paths, and comment quality.

## Plan Closure

Runtime Contract Hygiene is complete at 3/3:

- CH0 made a silently successful frame failure exit unrepresentable;
- CH1 made the Quaternion public contract truthful and mechanically visible in
  Debug;
- CH2 made the engine's zero-throw inventory true while preserving byte-exact
  Physics.
