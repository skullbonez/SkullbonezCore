# Allocator Foreign Pointer Safety Closure

Date: 2026-07-27
Plan: `allocator-foreign-pointer-safety`
Phases: AF0-AF2
Result: COMPLETE — foreign candidate reads are guarded, owned provenance is
pointer-bound, and the measured happy-path ruling passes

## Owner Ruling

The owner accepted the recommended AF2 resolution:

- retain the complete guarded header snapshot and 64-bit pointer-bound
  provenance cookie;
- forbid a return to magic-only ownership;
- interpret "no happy-path cost added" as no measurable regression in
  `tools\validate_perf.bat`;
- do not relax a budget, threshold, or baseline to obtain that result.

This resolves the literal conflict recorded in
`allocator-foreign-pointer-safety-af2-blocker.md`. Genuine provenance
necessarily adds fixed owned-path work; instruction identity is not a coherent
safety requirement.

## Closed Safety Surface

- `TryCopyAllocationHeader` copies the complete candidate inside table-based
  MSVC SEH. No field is consumed after a partial or faulting read.
- Matching public magic is insufficient. The cookie binds the user pointer,
  raw pointer, size, phase, flags, owner, reserved field, magic, and Tracy
  generation where present.
- `raw` reaches owned cleanup only after complete-copy, magic, and cookie
  validation.
- Debug/Profile fail loud on an unprovable foreign free. Release increments the
  process-lifetime counter, reports the event, and uses the explicitly ruled CRT
  fallback.
- Allocation-size addition is checked before `malloc`; overflow routes through
  the existing lane-F allocation failure.

Permanent tests cover an inaccessible predecessor page, readable-magic/partial
header, a fully shaped `VirtualAlloc` raw address, the Release CRT fallback, and
maximum-size overflow.

## Review And Comment Audit

The complete `RuntimeAllocationTracker.cpp` comment surface was audited. Its
header, invariants, magic-is-not-provenance hazard, Release fallback assumption,
and process-lifetime tripwire match the final implementation.

Independent review verdict: **PASS — ZERO BLOCKERS**. It confirmed that an
inaccessible foreign candidate cannot escape as a fault, a shaped non-CRT raw
pointer cannot enter owned `std::free`, overflow is checked, and the added work
is bounded to eight header bytes, cookie mixing, and one 40/48-byte copy with no
loop, lock, allocation, or system call.

The reviewer retained two scoped non-blockers: the non-MSVC direct-copy fallback
and malicious same-process cookie forgery are outside the shipping Win32/MSVC
accidental/third-party foreign-pointer threat model.

## Validation

- Allocation-policy self-test: PASS.
- Allocation-policy repository scan: PASS; 461 files, 36 direct-heap findings,
  85 dynamic STL members, 612 growth findings, zero allowlist errors.
- `tools\validate_tests.bat`: PASS, 421/421 tests and 2,410,268 assertions.
- `tools\validate_perf.bat`: PASS; zero measurable gate regression and
  `foreign_frees=0`.
- `tools\validate_full.bat`: DEFAULT GATE PASSED with the 44,401-line physics
  regression byte-exact.
- `tools\run_graphics_stress.bat 1`: PASS; 60-second timed run completed and
  `latest_memory.json` reports `foreign_free_count: 0`.

No allocation budget, gate threshold, config, schema, golden, physics,
SkullScope, replay, visual, DX12, or performance baseline moved.
