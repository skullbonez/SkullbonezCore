# Allocator Foreign Pointer Safety — AF1 Foreign Fallback

Date: 2026-07-27
Branch: `nightrunner-26th-JUL-26`
Plan phase: AF1

## Result

Every unprovable delete now increments the process-lifetime
`RuntimeAllocationForeignFreeCount` before making the owner-ruled
configuration decision:

- Debug and Profile emit a no-allocation Lane F diagnostic containing pointer,
  lifecycle phase, reserve-owner handle, header state, and count, then stop.
- Release emits the same fields, leaves the process counter visible, and calls
  `std::free` on the pointer exactly as received.

The Release branch carries the required invariant/hazard comment: its fallback
assumes the foreign allocator is this process's CRT, and the lifetime counter
is the tripwire for that shipped risk. A guard-mode baseline preserves the
lifetime total while making each newly selected allocation-guard interval fail
if a foreign free occurs during that interval.

The counter is visible in the allocation summary, shutdown memory dump, cached
main-memory diagnostics, and the existing Memory tab. Zero keeps the committed
UI stream byte-exact; a non-zero Release count replaces the normal status line
with a red `FOREIGN FREES` warning.

## Overflow Boundary

The tracker now checks both alignment normalization and
`size + alignment - 1 + sizeof(AllocationHeader)` before calling CRT malloc.
Throwing global new routes an overflow to `FatalAllocationFailure` with
`reason=size_arithmetic_overflow`; nothrow allocation returns null without
wrapping the request.

## Focused Proof

- Profile inaccessible-predecessor child: stops in allocation Lane F with
  `phase=diagnostics owner=0 header=unreadable foreign_free_count=1`, proving
  the guarded read reaches the owned diagnostic rather than an access
  violation.
- Profile maximum-size child: stops with
  `reason=size_arithmetic_overflow size=18446744073709551615`.
- Release runtime-contract test: PASS, 1/1 case and 122/122 assertions. Its
  child uses a same-CRT malloc pointer and proves `FOREIGN_FREE`,
  `header=bad_magic`, count one, summary visibility, guard violation, and a
  normal child exit after the CRT fallback.
- `tools\validate_tests.bat`: PASS; 418/418 Profile cases and
  2,410,177/2,410,177 assertions. The committed production UI fingerprints,
  including the zero-count Memory tab, remain unchanged.
- `tools\validate_format.bat`: PASS; 569 implementations and 316 headers.
- `python tools\check_allocation_policy.py --self-test`: PASS.
- `python tools\check_allocation_policy.py --repo .`: PASS; 461 files scanned,
  36 direct-heap, 85 dynamic-member, and 612 growth findings, all ruled.

`SKULLBONEZ_TESTS` defines a narrow
`SKULLBONEZ_TEST_PROFILE_ALLOCATION_FATAL` only in its Profile configurations.
Using the production-wide Profile macro in this render-free test target would
compile unrelated DX12 timing paths and violate the target's link boundary.
