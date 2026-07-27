# Allocator Foreign Pointer Safety — AF0 Ownership Probe

Date: 2026-07-27
Branch: `nightrunner-26th-JUL-26`
Plan phase: AF0

## Result

`FreeTrackedMemory` now confines its candidate-header magic read to a small
MSVC structured-exception guard. A foreign pointer at the first byte of a
committed page therefore cannot fault when its candidate header lies in an
inaccessible predecessor page. A matching, readable magic value remains the
only admission to tracker-owned header fields.

The owned-pointer path adds no registry lookup, lock, allocation, system call,
or extra header read. MSVC implements the selected guard with table-based
exception handling, so the successful path remains the original address
calculation and magic load plus the guard's control flow.

AF0 deliberately does not settle the readable-but-nonmatching case. AF1 owns
the ratified Debug/Profile fatal and Release counted fallback, so AF0 retains
the existing fallback for that case and returns safely for an unreadable
candidate header.

## Option Ruling

| Option | Ruling | Reason |
|---|---|---|
| MSVC structured-exception guard | Selected | Isolates the single hazardous read; table-based success path; no allocation, registry maintenance, lock, or OS query. |
| `VirtualQuery` before each free | Rejected | Adds an OS query to every owned free and page commitment still does not prove that the header belongs to this tracker. |
| Hook-owned allocation registry | Rejected | Adds synchronization and registry traffic to every allocation and free, requires a bounded-capacity failure policy inside the no-allocation hook, and duplicates the existing header ownership token. |

## Focused Proof

`Runtime allocation tracker: foreign page-boundary delete does not read
inaccessible predecessor` reserves two inaccessible pages, commits only the
second, passes its first byte to global delete, and verifies that the process
survives, the page remains committed, and its sentinel byte is unchanged.

- Focused test: PASS, 1/1 case and 7/7 assertions.
- `tools\validate_tests.bat`: PASS; 419/419 cases and 2,410,166 assertions.
- `tools\validate_format.bat`: PASS; 569 implementations and 316 headers.
- `tools\validate_perf.bat`: PASS; clean allocation guard, selected-path
  structural proof, DX12 and Physics benchmark absolute budgets, and no
  baseline regressions.

The first performance attempt exposed an SR3 ownership-move bookkeeping defect:
the allocation-policy allowlist still named
`SceneLoadTransaction.Preparation.cpp` while the ruled browser vectors had
moved to `SceneNavigationModel.Browser.cpp`. The stale mapping was corrected
without changing the exception count, policy, or thresholds; policy self-test
and repository scan then passed before the full performance rerun.
