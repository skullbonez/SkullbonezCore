# Allocator Foreign Pointer Safety — AF2 Blocker

Date: 2026-07-27
Branch: `nightrunner-26th-JUL-26`
Plan phase: AF2
Verdict: BLOCKED pending owner ruling

## Safety Review

The first independent review found two blocking bypasses in the AF0/AF1
candidate:

1. A foreign candidate could expose readable matching magic while the rest of
   its header remained inaccessible. The magic probe would succeed, followed
   by unguarded `raw`, counter, or Tracy-field access.
2. A fully readable shaped header with public matching magic could place a
   `VirtualAlloc` or private-heap address in `raw`, reaching `std::free` in
   Debug/Profile as if the tracker owned it.

The AF2 remediation closes both:

- `TryCopyAllocationHeader` snapshots the complete candidate inside the MSVC
  exception guard. No candidate field is consumed after a partial copy.
- A 64-bit process-specific provenance cookie binds the exact user pointer,
  raw pointer, size, phase, flags, owner, reserved field, magic, and Tracy
  generation where present.
- Only a complete snapshot with matching magic and cookie reaches
  `RecordFree`, Tracy free, the live-header clear, or `std::free(raw)`.
- A partial-header child exposes readable magic while `raw` remains
  inaccessible and proves `header=unreadable`.
- A fully readable shaped-header child carries matching magic and a
  `VirtualAlloc` raw address and proves `header=bad_provenance`.

Independent re-review found no remaining non-adversarial bypass outside the
owner-ruled Release fallback. It recorded two scoped residuals: the cookie is
not authentication against malicious same-process code with source and module
address knowledge, and non-MSVC builds retain a direct copy. The shipping
Win32/MSVC, accidental/third-party foreign-pointer threat model does not include
either.

## Blocking Conflict

The same re-review correctly found that genuine provenance cannot satisfy the
plan's literal happy-path clauses:

- “the ownership test must not become more expensive for owned pointers”
- “No happy-path cost added”

The remediation adds eight header bytes, cookie computation on allocation, a
complete 40/48-byte header snapshot on free, and cookie verification on free.
Every stronger alternative already evaluated by AF0 also adds owned-path cost:
`VirtualQuery` adds an OS query and a registry adds synchronization/table
traffic. Returning to magic-only would restore the two safety bypasses and fail
the plan's primary goal.

An owner ruling is therefore required. The safe default is to keep the
provenance remediation and replace the literal zero-instruction clause with
“no measurable regression in `validate_perf.bat`.” Without that ruling AF2
cannot close, even if the performance gate passes.

## Validation Before Block

- `tools\validate_tests.bat`: PASS; 418/418 cases and
  2,410,186/2,410,186 assertions.
- Focused Profile runtime contracts: PASS; 1/1 case and 129/129 assertions.
- `tools\validate_format.bat`: PASS; 569 implementations and 316 headers.
- Independent re-review: safety bypasses resolved; BLOCKED solely on the
  explicit happy-path-cost requirement.

The post-remediation full and performance gates were intentionally stopped
after the ruling conflict became binding. They remain AF2 work after an owner
decision; no baseline or threshold was changed.
