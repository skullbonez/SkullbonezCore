# Determinism Envelope Tier-2 Hardening

Date: 2026-08-15
Status: Active. 0/9 tasks complete.
Impact area: Maths transcendentals, Physics pose integration, ragdoll neck
constraint, determinism tooling, portable CPU test target, CI lanes
Owner: Physics determinism envelope
Priority: Active — T0 through T2 and T5 through T8 are unblocked; T3 and T4
require an explicit owner baseline decision before any source edit.

## Owner Direction

This plan raises the certified determinism envelope from tier 1 (one binary on
one machine) toward tier 2 (one binary, any x64 Windows machine) by removing
implementation-defined transcendentals from physics-visible paths, gating their
return, and building the portable test target and cross-machine evidence lane
that would actually observe a tier-2 violation.

**This plan grants no baseline-refresh authority.** T3 and T4 change physics
output bits by construction. Each requires a separate explicit owner approval of
that exact candidate transition, reviewed as behavior, before its source edit
lands. An agent must not regenerate `TestOutput/baselines/physics_regression_varied.csv`
or any other physics golden under this plan without that approval recorded in
the owning commit body.

## Problem And Evidence

Dated 2026-08-15, measured against the current tree.

`Agentic/Reference/physics-overview.md` certifies byte-exact physics for "one
binary built inside the repository's pinned Windows x64 MSVC toolchain envelope."
The pinning is real and thorough: no `__cpuid` or `IsProcessorFeaturePresent`
anywhere in `SkullbonezSource/`, no `/arch:` override (SSE2 baseline), static CRT
(`/MT`, `/MTd`) across all five projects, `/fp:precise` in every configuration,
`SkullbonezSource/Core/FloatingPointContract.h` force-included per translation
unit, and worker-count invariance asserted at 0/1/4 workers.

Two gaps remain.

### Gap 1 — implementation-defined transcendentals on physics paths

IEEE 754 mandates correct rounding only for `+`, `−`, `×`, `÷`, `sqrt`, `fma`,
and `remainder`. Every `<cmath>` transcendental is implementation-defined, may
differ between CRT versions, and may dispatch internally on processor features
even inside one statically linked binary.

A scan of `SkullbonezSource/Physics` alone reports 47 `sqrtf` (correctly rounded,
no hazard) and one `acosf`. That scan understates the exposure, because the
remaining trig is one call level down in `SkullbonezSource/Maths`. The full
physics-reachable set is three call sites in two functions:

| Site | Function | Reached from |
|---|---|---|
| `SkullbonezSource/Maths/Quaternion.cpp:94` | `sinf` | `Quaternion::RotateAboutAxis` |
| `SkullbonezSource/Maths/Quaternion.cpp:95` | `cosf` | `Quaternion::RotateAboutAxis` |
| `SkullbonezSource/Physics/Ragdoll.cpp:253` | `acosf` | neck swing limit |

`Quaternion::RotateAboutAxis` is called from `SkullbonezSource/Physics/Ragdoll.cpp:257`
and, critically, from `IntegrateBodyRecordPose` at
`SkullbonezSource/Physics/PhysicsBodyStore.cpp:1007` — the authoritative pose
integrator. `sinf` and `cosf` therefore execute for every body whose angular
velocity exceeds the `omegaMag > 0.0001f` gate, every tick. This is the most
physics-visible path in the engine.

The remaining `Maths` trig is not physics-reachable and is expected to produce
`retain-owner` rulings rather than repairs:

- `SkullbonezSource/Maths/RotationMatrix.h:88-89` (`sinf`/`cosf` in
  `RotatePointAboutArbitrary`) — callers are only
  `SkullbonezSource/Runtime/Camera/Camera.cpp` and
  `SkullbonezSource/Runtime/Editor/EditorGizmoTools.cpp`.
- `SkullbonezSource/Maths/Matrix4.cpp:73` (`tanf`) — perspective projection.

`Quaternion::GetOrientationMatrix` is algebraic, so `TerrainContactManifold`
reaches no trig despite constructing rotation matrices per contact query.

### Gap 2 — no cross-machine byte comparison exists

`SkullbonezTests/TestDeterminism.cpp` is self-comparing: it proves two engines
in one process agree, and that 0/1/4 workers agree, on whatever machine runs it.
It never compares against a committed golden.

The committed byte-exact oracle is compared only by `tools\validate_physics.bat`,
which is not part of the hosted lane. `.github/workflows/mandatory-cpu-validation.yml`
runs `validate_fast --preflight-only` and `validate_all_cpu_tests.bat`, and the
latter runs tests, coverage, interaction policy, scene parser, UI boundary, and
DX12 arch gates. No physics baseline diff executes on any machine except the
owner's.

Tier 2 is therefore architecturally supported and never observed. The gap is
evidence, not design.

### Secondary defect

`SkullbonezSource/Physics/Ragdoll.cpp:253` passes an unclamped dot product to
`acosf`. A dot product of two normalized vectors routinely exceeds 1.0 by an ULP
after rounding, and `acos` of a value outside [-1, 1] returns NaN. This is a
live correctness defect independent of the determinism work.

## Determinism Tiers

Terms used by this plan, so it stays readable without the originating session.

- **Tier 1** — same binary, same machine, same input reproduces byte-exactly.
- **Tier 2** — same binary reproduces byte-exactly on any machine of the same
  architecture and operating system.
- **Tier 3** — byte-exact across compilers, architectures, and platforms. Out of
  scope for this plan.

## Goal

Remove every implementation-defined transcendental from physics-visible paths by
replacing them with routines built only from correctly-rounded IEEE 754
operations; gate their reintroduction mechanically; build a portable CPU test
target so physics and maths logic can run under a second compiler and under
sanitizers; and add the cross-machine byte comparison that would observe a tier-2
violation if one exists.

## Non-Goals

- Do not pursue tier 3. Cross-compiler byte-exactness is not a goal, and a Linux
  build must never be compared against a Windows-generated golden.
- Do not refresh any physics, replay, SkullScope, or visual baseline without an
  explicit owner decision on that exact transition.
- Do not port the renderer, Runtime, UI, or the DX12 gates to Linux.
- Do not add an external math dependency. The physics-reachable surface is three
  call sites; a vendored library is disproportionate and widens the envelope.
- Do not convert any inventory produced here into a count threshold, ratio, or
  budget. The correctly-rounded operation set is a fixed property of IEEE 754,
  not a tunable ceiling.
- Do not weaken `omegaMag`, sleep, or contact thresholds to make a candidate
  transition produce a smaller diff.
- Do not fold the Linux lane into the mandatory Windows gate. It is an additional
  signal, never a substitute for `validate_physics`.
- Do not add a second retained trig implementation. One deterministic owner
  serves both physics call sites.

## Phases

- [ ] **T0 — Establish the pre-change envelope and CRT dispatch evidence.**
  Record the current physics regression hashes and the exact toolchain from one
  authoritative Debug build. Determine empirically whether the statically linked
  UCRT `sinf`, `cosf`, and `acosf` dispatch on processor features, by
  disassembling the linked routines in the shipped binary or by executing the
  same binary on two machines with different microarchitectures and comparing
  results. Record the finding either way: it decides whether Gap 1 is a live
  tier-2 break or a latent one, and that distinction belongs in the plan before
  any behavior changes. No source edit.

- [ ] **T1 — Add the deterministic transcendental owner with a byte-exact oracle.**
  Introduce one owner under `SkullbonezSource/Maths` providing the half-angle
  rotation coefficients and `acos`, implemented using only `+`, `−`, `×`, `÷`,
  and `sqrtf`, with a fixed evaluation order and no reliance on contraction.
  Establish the input domain from measured data, not assumption: instrument the
  regression scenes to report the observed distribution of
  `omegaMag * deltaSeconds` and fit over that domain with a stated maximum error
  in ULP. A naive Taylor series is insufficient at the upper end of a realistic
  angular-velocity range and must not be assumed adequate. Commit an
  input-to-output bit-pattern table and assert byte-equality against it, so a
  toolchain upgrade that re-associates the polynomial fails loudly. Adopt
  nothing in this phase; physics output must not change.

- [ ] **T2 — Add the determinism math policy gate.**
  Add `tools/check_determinism_math_policy.py` and
  `tools/determinism_math_rulings.json`, modelled on
  `tools/check_allocation_policy.py` and `tools/allocation_policy_allowlist.json`,
  including their `--self-test` and `--repo` invocations and their
  unruled-fails / ruled-passes contract. Scan roots must include both
  `SkullbonezSource/Physics` **and** `SkullbonezSource/Maths`; a Physics-only
  scope is exactly what hid Gap 1. Permit only correctly-rounded or exact
  routines: arithmetic, comparison, `sqrtf`, `fabsf`, `floorf`, `ceilf`,
  `truncf`, `roundf`, `copysignf`, `ldexpf`, `frexpf`, `fmodf`. Flag every other
  `<cmath>` entry point. Require a ruling for explicit `fmaf` as well: it is
  correctly rounded and therefore permitted in principle, but the repository
  disables implicit contraction deliberately and an explicit fused call must
  state its intent. Land the rulings with the two physics sites as `repair-plan`
  naming this plan, and the camera, gizmo, and projection sites as
  `retain-owner` with their non-reachability as the stated reason. Wire the
  checker into `tools\validate_fast.bat`. No physics behavior change.

- [ ] **T3 — Adopt the deterministic rotation in the pose integrator.**
  **Owner baseline decision required before this phase begins.** Replace the
  `sinf`/`cosf` pair in `Quaternion::RotateAboutAxis`, or supply a dedicated
  integration entry point, so `IntegrateBodyRecordPose` composes the orientation
  delta from the T1 coefficients. Prefer the exponential-map form in which the
  vector part is built from the angular velocity scaled by a half-angle sinc
  term: it needs neither the axis normalisation division nor the
  `omegaMag > 0.0001f` guard, which removes a knife-edge branch of exactly the
  kind `AGENTS.md` warns about, and it is more accurate in the small-angle regime
  that dominates. Prove worker-count invariance is retained. Present the
  resulting baseline diff as a behavior change for owner review; do not refresh
  the golden until that approval is recorded.

- [ ] **T4 — Adopt deterministic `acos` and clamp the ragdoll neck input.**
  **Owner baseline decision required before this phase begins.** Replace
  `acosf` at `SkullbonezSource/Physics/Ragdoll.cpp:253` with the T1 routine and
  clamp its argument to [-1, 1] before the call. Land the clamp as its own
  reviewable change with a regression test that drives the dot product past 1.0,
  since the NaN is a correctness defect that should be fixed whether or not the
  determinism transition is approved. Update the T2 rulings to remove both
  `repair-plan` rows; after this phase the physics-reachable transcendental set
  is empty.

- [ ] **T5 — Build the portable CPU target.**
  Add a CMake build covering `Maths`, `Physics`, `Scene`, `World`, `Assets`, and
  the portable part of `Core`. The port surface is four files: Win32 file I/O in
  `SkullbonezSource/Core/AtomicTextFileWriter.cpp`, one
  `GetEnvironmentVariableA` in `SkullbonezSource/Core/TracyClientOwner.cpp`, and
  three `__debugbreak()` sites in `SkullbonezSource/Core/FatalError.cpp` and
  `SkullbonezSource/Core/Allocation/RuntimeAllocationTracker.cpp`. Keep every
  platform branch inside `SkullbonezSource/Core/PlatformWin32.h` and its new
  sibling; Physics and Maths must remain at zero platform includes, which is
  their current measured state and is worth preserving as an invariant.
  `SkullbonezSource/Core/FloatingPointContract.h` needs a compiler guard, since
  `#pragma fp_contract(off)` is MSVC spelling; the portable build must pass
  `-ffp-contract=off` and fail the build if it is absent rather than silently
  compiling with contraction enabled.

- [ ] **T6 — Split the portable test target.**
  24 of 58 files in `SkullbonezTests/` already have no Runtime or Rendering
  include, including `TestPersistentContactSolver.cpp`,
  `TestObjectContactManifold.cpp`, `TestSpatialGrid.cpp`,
  `TestPhysicsStageState.cpp`, and `TestSleepController.cpp`. Define that set as
  a portable target without duplicating source. `SkullbonezTests/TestDeterminism.cpp`
  is not currently in that set because it includes
  `SkullbonezSource/Runtime/Replay/ReplayRecorder.h` for snapshot round-trips;
  split its worker-invariance and fixed-step cases from its replay cases so the
  physics half travels. Name the split files for the subsystem they pin, never
  for the platform or the gate. The Windows doctest run must keep executing every
  case it does today.

- [ ] **T7 — Add the Linux lane with sanitizers.**
  Run the T6 target under Clang and GCC, and under ASan, UBSan, and TSan. TSan on
  `SkullbonezSource/Core/WorkerPool.cpp` and the parallel merge discipline is the
  highest-value item: worker-count invariance tests prove results agree, not that
  the accesses producing them are race-free. Treat second-compiler warnings as
  findings. This lane must never compare against a Windows-generated golden; if a
  determinism oracle is wanted here it is a separate Linux-envelope baseline with
  its own provenance.

  **ASan already exists on Windows; scope this task around what does not.**
  `.github/workflows/native-diagnostics.yml` runs MSVC AddressSanitizer against
  the full CPU suite every Monday, driven by
  `tools/validate_native_diagnostics.py`. It proves the detector with an injected
  use-after-free before running real code. This plan was written without
  referencing that lane, so read T7's ASan mention as a second opinion rather
  than new coverage: MSVC supports AddressSanitizer and nothing else, so **TSan
  and UBSan are the only sanitizers Linux uniquely provides**, and TSan is the
  reason the lane is worth building. Note also the coverage direction — T6's
  portable target is a *subset* of the suite the Windows lane already covers, so
  Linux ASan would test less code under a different ASan implementation. Decide
  that trade deliberately (Open Question 5) rather than inheriting it.

  Sequencing note: as of 2026-08-15 the existing ASan lane has failed four
  consecutive scheduled runs (last green 2026-07-13) with one doctest case and
  four assertions failing. Do not stand up a second sanitizer lane that copies
  its reporting approach; a scheduled sanitizer job whose red runs cannot be read
  manufactures the appearance of coverage.

- [ ] **T8 — Add cross-machine byte evidence to the hosted Windows lane.**
  Extend `.github/workflows/mandatory-cpu-validation.yml` so the physics
  regression comparison executes on the hosted `windows-latest` fleet, whose
  runners span multiple CPU vendors and generations. This is the only phase that
  produces direct tier-2 evidence. Note the confound and design around it: a
  CI-rebuilt binary conflates machine variation with hosted-image toolchain
  drift, so the lane must either pin and verify the toolchain as the existing
  clang-format and OpenCppCoverage steps already do, or build once and execute
  the same artifact on multiple runners. Record which form of evidence the lane
  actually produces; a passing run whose confound is undocumented is not proof.

## Validation

Per-phase, using the smallest gate in the `AGENTS.md` file-to-validation map.

| Phase | Required gate |
|---|---|
| T0 | None; evidence only, no source edit |
| T1 | `tools\validate_tests.bat` |
| T2 | `tools\validate_fast.bat`, then the new checker's `--self-test` and `--repo .` directly |
| T3 | `tools\validate_full.bat` — `Maths` is shared by Physics and Rendering — plus `tools\validate_physics.bat` after any approved baseline transition |
| T4 | `tools\validate_physics.bat`, plus `tools\validate_tests.bat` for the clamp regression |
| T5 | `tools\validate_fast.bat`; the Windows build must be unchanged |
| T6 | `tools\validate_all_cpu_tests.bat` |
| T7 | `tools\validate_fast.bat`; the Linux lane is additive and gates nothing on Windows |
| T8 | `tools\validate_fast.bat`, then one full hosted run observed green |

An owner-approved baseline transition in T3 or T4 must regenerate goldens only
from the final Debug executable, scenes, and config that will be committed, then
rerun the matching physics gate so the committed artifact is compared
byte-exactly rather than copied.

## Acceptance

- `tools/check_determinism_math_policy.py` reports zero unruled findings, and
  `tools/determinism_math_rulings.json` contains no `repair-plan` row.
- No implementation-defined transcendental is reachable from
  `SkullbonezSource/Physics`, proved by the checker rather than by a manual grep.
- The deterministic routines carry a committed bit-pattern oracle with a stated
  maximum error in ULP over a measured input domain.
- `Ragdoll.cpp` cannot pass an out-of-range argument to `acos`, with a regression
  test that fails without the clamp.
- Worker-count invariance at 0/1/4 workers still holds after T3 and T4.
- The portable target builds and passes on Linux under Clang and GCC, and is
  clean under ASan, UBSan, and TSan.
- `SkullbonezSource/Physics` and `SkullbonezSource/Maths` still contain zero
  platform includes.
- The hosted Windows lane performs a physics byte comparison on machines other
  than the owner's, and its evidence form — pinned rebuild or shared artifact —
  is recorded.
- `Agentic/Reference/physics-overview.md` states the envelope the repository can
  then actually defend, revised only to what T8 observed.

## Open Questions

Resolve these inside the owning phase; do not treat them as settled.

1. Does the statically linked UCRT dispatch on processor features for `sinf`,
   `cosf`, or `acosf`? T0 answers this and determines whether Gap 1 is live or
   latent.
2. What is the true upper bound of `omegaMag * deltaSeconds` across the gated
   regression content? T1's approximation domain depends on the measurement, and
   a spinning body at a realistic angular velocity produces an argument large
   enough that series truncation error is not automatically negligible.
3. Is `Quaternion::RotateAboutAxis` the right seam to change, or should physics
   integration call a dedicated entry point so camera and editor callers keep the
   general-purpose implementation? T3 decides by ownership, not convenience.
4. Does removing the `omegaMag > 0.0001f` branch change sleep or wake behavior
   for near-stationary bodies? This must be measured, not assumed, before the T3
   baseline transition is presented.
5. Should the Linux lane run ASan at all, given
   `.github/workflows/native-diagnostics.yml` already runs MSVC ASan over a
   superset of T6's portable target? The case for keeping it is that Clang's and
   MSVC's ASan implementations do not catch identical defects; the case against
   is runtime cost and a second red lane to watch for a marginal second opinion.
   T7 decides and records the reason. TSan and UBSan are not in question — MSVC
   provides neither, and they are the lane's actual justification.
