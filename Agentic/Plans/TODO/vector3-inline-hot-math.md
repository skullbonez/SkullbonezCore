# Vector3 Inline Hot Math — Header-Inline Ops, Trivial Copies, No Layout Change

Date: 2026-07-15
Status: Active — 0/3 tasks complete
Impact area: `Maths/Vector3`, every physics/render hot path that does vector
arithmetic, physics determinism baselines
Owner: maths

## Problem And Evidence

`Vector3` — the hottest type in the engine — pays call overhead for basic
arithmetic:

1. Constructors, copy constructor, `operator=`, and all arithmetic operators
   are declared in `SkullbonezSource/Maths/Vector3.h:52-77` but defined
   out-of-line in `SkullbonezSource/Maths/Vector3.cpp:33-240`. Without
   link-time codegen every `a + b` in the solver is a cross-TU call — and the
   physics determinism gate runs Debug builds, where nothing inlines.
2. The user-provided copy constructor and `operator=` make `Vector3`
   non-trivially-copyable, pessimizing every `std::vector<Vector3>`
   copy/move and blocking memcpy paths.
3. `const Vector3 ZERO_VECTOR` at namespace scope in the header
   (`Vector3.h:79`) creates one internal-linkage copy per translation unit.

Adversarial review 2026-07-15, finding #4. Owner decision: inline-only for
now — no 16-byte padding, no SSE intrinsics, no SoA kernels in this plan, so
float semantics are unchanged under `/fp:precise` and the byte-exact physics
baselines must not move.

## Goal

All `Vector3` member operations are defined inline in the header; copy
construction and assignment are compiler-defaulted (type stays
non-trivially-default-constructible only because of the Debug NaN poison);
`ZERO_VECTOR` becomes a single shared constant; physics baselines pass
byte-exact with zero refresh.

## Non-Goals

- No SIMD intrinsics, no 16-byte padding/alignment, no `Vector4`, no SoA
  restructuring (deliberately deferred; revisit after this lands).
- No API renames and no behavior change to `Normalise`/divide fatals — that is
  `math-fatal-removal.md`.

## Tasks

- [ ] T1 — Move every `Vector3` member definition from `Vector3.cpp` into
      `Vector3.h` as `inline` (or defaulted where possible): default ctor
      keeps the `_DEBUG` NaN poison; copy ctor and `operator=` become
      `= default`; all arithmetic/compound operators inline. Replace the
      per-TU `ZERO_VECTOR` with `inline constexpr`-compatible storage
      (`inline const Vector3 ZERO_VECTOR{ 0.0f, 0.0f, 0.0f };`) so one entity
      exists program-wide. `Vector3.cpp` shrinks to whatever cannot be inlined
      (possibly deleted; update the vcxproj in the same commit if so).
- [ ] T2 — Static assertions and unit coverage: `static_assert` for
      `std::is_trivially_copyable_v<Vector3>` and `sizeof( Vector3 ) == 12`
      next to the type with a `Why:` comment (locks the ABI this plan promises
      not to change); extend `SkullbonezTests/TestVector3.cpp` for the
      defaulted-copy semantics.
- [ ] T3 — Final gates. This touches a header included by every subsystem:
      `tools\validate_full.bat` per the multiple-areas mapping row. The
      44,401-line varied physics baseline must pass byte-exact with no
      refresh — under `/fp:precise` inlining must not change per-operation
      IEEE semantics, and the gate is the proof. Then `tools\validate_perf.bat`
      to record the hot-path improvement (or at minimum no regression) as
      closure evidence.

## Dependencies And Decisions

- Owner decision 2026-07-15: option "inline-only" chosen over padded-SIMD and
  SoA-kernel alternatives; those remain candidate future plans and their
  baseline-refresh cost is the recorded reason for deferral.
- Should land before `math-fatal-removal.md` to avoid editing the same
  function bodies twice in conflicting shapes.

## Acceptance

- No `Vector3` member function bodies remain out-of-line.
- `Vector3` is trivially copyable; size remains 12 bytes.
- `validate_full` passes with byte-exact physics baselines (no refresh), and
  a perf gate result is recorded.

## Validation

- `tools\validate_full.bat` then `tools\validate_perf.bat`, output pasted at
  closure. Explicitly state that no baseline file changed.
