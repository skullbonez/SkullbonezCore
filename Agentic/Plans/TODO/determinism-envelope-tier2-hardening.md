# Determinism Envelope Tier-2 Hardening

Date: 2026-08-15
Status: Active. 3/9 tasks complete.
Impact area: Maths transcendentals, Physics pose integration, ragdoll neck
constraint, determinism tooling, portable CPU test target, CI lanes
Owner: Physics determinism envelope
Priority: Active — T5 through T8 are unblocked; T3 and T4
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

### Approved deterministic-rotation direction (2026-08-16)

Follow Erin Catto's modern Box2D/Box3D practice at the determinism boundary:
physics-visible angle construction uses repository-owned deterministic
`ComputeCosSin` and `Atan2` routines rather than CRT transcendentals. These are
fixed approximations with explicit range reduction, evaluation order, and
bit-pattern tests; they are not lookup tables and must not depend on fast-math,
FMA contraction, or processor dispatch.

Preserve SkullbonezCore's existing exponential-map integration shape. The
authoritative integrator currently applies one axis-angle delta for the fixed
step, left-multiplied because angular velocity is expressed in world space. For
angular displacement `theta`, that update represents the frozen-angular-velocity
rotation directly; a normalized first-order quaternion update instead advances
by the smaller effective angle `2 * atan(theta / 2)`. Box3D controls that error
with solver substeps and a maximum angular displacement. SkullbonezCore has no
equivalent bound at this integration seam, so copying that update would trade
away useful accuracy, especially for fast rotation and partial-time collision
steps. Adopt Catto's deterministic math ownership, not a mismatched integration
approximation.

This ruling settles the T1, T3, and T4 design direction only. It does not approve
the resulting baseline transitions and grants no baseline-refresh authority.

### Owner Baseline Artifact Ruling — 2026-08-16

A physics-baseline mismatch in a Determinism phase is evidence to preserve, not
a stop condition for this orchestration run. When a phase produces that
mismatch, copy every executable used by the failing gate before another build
can overwrite it. Name each copy with the plan and phase (for example,
`SKULLBONEZ_CORE_TIER2_DETERMINISM_T3.exe`), record the baseline diff and saved
artifact path in the phase evidence, and continue to the next phase. Do not
refresh a golden and do not treat the mismatch as accepted behavior; the owner
will compare the preserved candidates and decide which transitions to accept.
Build failures, crashes, invariant failures, and non-physics correctness
failures remain blocking.

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
live correctness defect independent of the determinism work. T4 removes the
domain hazard entirely by constructing the vector angle as
`Atan2(length(cross(a, b)), clamp(dot(a, b), -1, 1))`; no repository-owned
`acos` is needed.

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

- [x] **T0 — Establish the pre-change envelope and CRT dispatch evidence.**
  Record the current physics regression hashes and the exact toolchain from one
  authoritative Debug build. Determine empirically whether the statically linked
  UCRT `sinf`, `cosf`, and `acosf` dispatch on processor features, by
  disassembling the linked routines in the shipped binary or by executing the
  same binary on two machines with different microarchitectures and comparing
  results. Record the finding either way: it decides whether Gap 1 is a live
  tier-2 break or a latent one, and that distinction belongs in the plan before
  any behavior changes. No source edit.

  Evidence (2026-08-17): the authoritative Debug executable at commit
  `cea7683ea3efa8ac787fdecbf85408b98450b882` is
  `B088D21FABA5E3BBDF9346635D09C720BCF0C6980A85F0D28EB8241133287D5D`
  (SHA-256); its PDB is
  `C836776A4A51CB286D25084C74945A0D4E5AAE187CE01FAD420126427681F38F`.
  The varied-scene gate emitted two byte-identical 6,328,076-byte runs. Each
  canonical run and the committed 44,401-line baseline hash to
  `4DCE1BE8AD1DDE337281C7F37C25FCF3FD7B9268BCFE0B382FEFB4F85DFE69AA`;
  the repeated output file hashes to
  `4C7F3533A14A8D60F6E64B2B8244E262C929FD97C18E70695C3BCF133E1784C6`.
  The authored scene hash is
  `1D73FEF58565EB71C655780220DB771B88285DB35901F6BD6F3304A8C669355C`.

  The build used Visual Studio 18.7.8/MSBuild 18.7.8.30822, platform toolset
  v145, MSVC tools 14.51.36231, compiler 19.51.36248.0, linker
  14.51.36248.0, Windows SDK 10.0.26100.0, `/MTd`, `/fp:precise`, and no
  `/arch:` override. The linked `libucrtd.lib` hashes to
  `082A3E21FF32BEEEE86864DA52FCAF861B80B09D80B4F77FDC0A39FF549AD0CB`.
  The observation host is Windows 11 build 26200 on an AMD Ryzen Threadripper
  3970X (32 cores, 64 logical processors).

  LLVM 22.1.3 PDB symbols plus executable disassembly answer the dispatch
  question affirmatively. The linked `__acrt_initialize_fma3` executes
  `cpuid`, `xgetbv`, and `cpuid(7)` and stores processor-dependent values in
  `__use_fma3_lib`. Linked `sinf` and `cosf` test that value: zero branches to
  `sinf_sse2`/`cosf_sse2`, while nonzero stays in bodies containing explicit
  `vfmadd*` instructions. Linked `acosf` masks the same value and dispatches
  value 3 to `acosf_fma`, whose body contains `vfmsub*`/`vfmadd*`; other values
  use the non-FMA body. This is a live tier-2 exposure, not merely a latent CRT
  portability concern: one shipped binary chooses a different arithmetic
  implementation from host CPU/OS feature state before the physics-reachable
  call sites execute. T1 therefore owns removal even though this host's current
  varied-scene output matches the tier-1 golden exactly.

- [x] **T1 — Add the deterministic transcendental owner with a byte-exact oracle.**
  Introduce one owner under `SkullbonezSource/Maths` providing
  `ComputeCosSin(angle)` and `Atan2(y, x)`, following the ownership and numerical
  strategy of modern Box2D/Box3D: fixed repository-controlled approximations,
  explicit range reduction, stable normalization where required, and a fixed
  evaluation order. Do not add `acos`, a lookup table, an external dependency,
  fast-math, or explicit FMA. The implementation may use only `+`, `−`, `×`,
  `÷`, comparison, and `sqrtf`.

  Establish the rotation input domain from measured data, not assumption:
  instrument the regression scenes to report the observed distribution of
  `omegaMag * deltaSeconds`, then verify `ComputeCosSin` over both that domain
  and the complete range-reduction boundaries. Verify `Atan2` in every quadrant,
  on both axes, around signed zero if the contract distinguishes it, and near
  parallel and anti-parallel vector inputs. State maximum angular and coefficient
  error; do not describe a large-domain approximation only in ULP near zero.
  Commit input-to-output bit-pattern tables and assert byte-equality against
  them, so a toolchain upgrade that re-associates either approximation fails
  loudly. Adopt nothing in this phase; physics output must not change.

  Evidence (2026-08-17): the existing varied-scene regression instrumentation
  already emits `omegaMag` for every body/frame row, so T1 derived the input
  distribution from all three committed omega-bearing gated CSVs without
  touching a Physics source file: varied (44,400 rows), shooting-reaction (640
  rows), and three-body chaos (360 rows). Across the 45,400 rows, 22,570 rotate;
  at the fixed 1/120-second step, `omegaMag * deltaSeconds` has p50 `0`, p90
  `0.0190475`, p99 `0.0366666667`, p99.9 `0.0416666667`, and maximum
  `4.2022033333` radians. The maximum is `target_box_08` at shooting-reaction
  frame 0 (`omegaMag = 504.2644`); the three-body baseline has zero angular
  speed throughout. The three bullet-sweep goldens contain collision-time rows
  rather than body-state/omega samples and therefore do not contribute values
  to this measured distribution.

  `Maths/DeterministicMath` now owns `ComputeCosSin` and `Atan2`, adapted from
  Box3D commit `30c67b5e6d0a3a66f0f506c69ce9e9e0587e3b7c` with retained MIT terms.
  SkullbonezCore replaces `remainderf` with bounded subtraction and certifies
  `ComputeCosSin` over the closed interval `[-64*pi, 64*pi]`; the measured
  regression maximum is about 2.1% of the positive certified limit. The owner
  uses only arithmetic, comparison, and `sqrtf`, and the Maths project's forced
  floating-point contract keeps contraction disabled.

  A 1,000,001-point MSVC binary32 sweep over the certified cosine/sine domain
  measured maximum coefficient error `0.00166492605` and maximum angular error
  `0.00170106891` radians. A 1,000,001-point full-circle Atan2 sweep measured
  maximum angular error `0.0000277435148` radians. The committed focused tests
  enforce slightly wider `0.00167`, `0.00171`, and `0.000028` bounds, exercise
  all 64 actual adjacent repeated-subtraction transition pairs and their outer
  neighboring floats, cover the measured input envelope, all quadrants and
  axes, the signed-zero canonicalization, and near-parallel/anti-parallel
  inputs. Fifteen cosine/sine rows and twenty Atan2 rows pin exact input/output
  binary32 bits, including the true measured maximum.

  `tools\validate_tests.bat` passed 569 cases and 2,479,868 assertions in
  Profile. The focused Debug run passed 6 cases and 251 assertions. The strict
  glossary inventory reports 1,001 unique definitions with no duplicate or
  unruled term, and the build-configuration inventory reports 0 dropped
  inheritance and 0 blocking diagnostics. The touched-source comment audit
  inspected 3/3 files with 0 deferred. No Physics file or call site changed, so
  the byte-exact physics baseline remains untouched in T1.

- [x] **T2 — Add the determinism math policy gate.**
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

  Evidence: `tools/check_determinism_math_policy.py` scans all 93 tracked C++
  source/header files under both Physics and Maths. It reports 32 current
  non-certified references, 32 exact rulings, zero unruled findings, zero stale
  rulings, and zero blockers. The inventory includes the Planning-only
  `OrbitalMechanics` family that the original site sketch omitted. The two
  Quaternion calls and one Ragdoll call are `repair-plan` rows owned by T3/T4;
  projection and the shared Camera/Editor arbitrary-axis helper have current
  `retain-owner` reachability evidence.

  The self-test proves both scan roots, exact ruled/unruled/currentness joins,
  missing repair-plan rejection, explicit FMA classification, C++ special-math
  coverage, split-line calls, standard/global function references, namespace
  aliases, same-line and continued macro aliases, and member-access false-
  positive rejection. `validate_fast` runs both its self-test and repository
  mode. The gate also exposed and repaired the missing DeterministicMath project
  filter prefix and added exact reachability repair rows for the intentionally
  test-only T1 production seams.

  Validation: `tools\validate_fast.bat` passed end to end in about 5m48s,
  including Profile/Automation/Debug builds, 569 unit-test cases, and strict
  reachability with 83/83 rows ruled. Direct `--self-test` and `--repo .`
  invocations passed with the 32/32 inventory above. The touched-tool comment
  audit inspected 3/3 source-bearing files with zero deferred, and independent
  review closed every parser, ownership, and reachability finding. No Physics
  source, executable behavior, or baseline changed in T2.

- [ ] **T3 — Adopt the deterministic rotation in the pose integrator.**
  **Owner baseline decision required before this phase begins.** Add a dedicated
  physics integration entry point and keep its current semantics explicit:
  angular velocity is world-space, the delta quaternion is left-multiplied, and
  the update is the exponential map of angular velocity held constant across
  `deltaSeconds`. Build the delta from T1's `ComputeCosSin` using the stable form
  `vectorPart = omega * (0.5 * deltaSeconds * sinc(halfAngle))`, where
  `halfAngle = 0.5 * length(omega) * deltaSeconds` and the zero limit of `sinc`
  is exact. Normalize the composed orientation using the existing deterministic
  normalization contract.

  Remove both the axis-normalization division and the arbitrary
  `omegaMag > 0.0001f` cutoff without introducing a new near-zero discontinuity.
  Do not replace this with Box3D's normalized first-order quaternion update:
  that approximation is appropriate only with a measured maximum angular
  displacement and substep policy that this engine does not currently have.
  Exercise zero, sub-threshold, ordinary, high-speed, and partial-time-step
  rotations, and prove worker-count invariance is retained. Present the
  resulting baseline diff as a behavior change for owner review; do not refresh
  the golden until that approval is recorded.

- [ ] **T4 — Adopt deterministic vector-angle construction for the ragdoll neck.**
  **Owner baseline decision required before this phase begins.** Replace
  `acosf(dot)` at `SkullbonezSource/Physics/Ragdoll.cpp:253` with T1's
  `Atan2(length(cross), clampedDot)`. Reuse the already-computed cross vector for
  both its magnitude and correction axis, clamp the dot product to [-1, 1] as a
  bounded geometric invariant, and preserve the existing deterministic fallback
  axis when the vectors are parallel or anti-parallel. The correction quaternion
  must use the same T1 `ComputeCosSin` owner as pose integration through
  `Quaternion::RotateAboutAxis`; no second trig implementation and no
  deterministic `acos` are allowed.

  Land the NaN repair as its own reviewable behavior change. Add focused tests
  for dot values just outside both ends of [-1, 1], aligned and opposed vectors,
  fallback-axis selection, and the maximum-correction cap. Update the T2 rulings
  to remove both `repair-plan` rows; after this phase the physics-reachable CRT
  transcendental set is empty.

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

  **Runner and trigger (owner ruling 2026-08-15).** Use the GitHub-hosted
  `ubuntu-latest` image — whatever standard hosted Linux is cheapest to stand up.
  No self-hosted runner, no container image to maintain, no third-party runner
  service. Add it as its own workflow file rather than a job inside
  `mandatory-cpu-validation.yml`, so a red Linux lane can never block a Windows
  merge; the existing non-goal forbidding that fold is the reason. Trigger on
  `schedule` plus `workflow_dispatch`, matching `native-diagnostics.yml`, and
  name an owner in the workflow header who is expected to act on a red run —
  the lane below went red for four weeks precisely because nobody owned it.

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
  maximum angular/coefficient error over measured and boundary input domains.
- Physics-visible angle construction uses the single repository-owned
  `ComputeCosSin` and `Atan2` implementation; no deterministic `acos` or lookup
  table exists.
- `Ragdoll.cpp` no longer has an `acos` domain failure, with regressions covering
  out-of-range rounded dot products and the anti-parallel fallback axis.
- Pose integration preserves the existing world-space, left-multiplied
  exponential-map semantics and has no arbitrary near-zero angular cutoff.
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
2. Resolved by T1: the true observed upper bound of `omegaMag * deltaSeconds`
   across all three omega-bearing gated regression CSVs is `4.2022033333`
   radians (`target_box_08`, shooting-reaction frame 0). The measurement
   validates range reduction and error bounds; it does not reopen the approved
   exponential-map integration shape.
3. Does removing the `omegaMag > 0.0001f` branch change sleep or wake behavior
   for near-stationary bodies? This must be measured, not assumed, before the T3
   baseline transition is presented.
4. Should the Linux lane run ASan at all, given
   `.github/workflows/native-diagnostics.yml` already runs MSVC ASan over a
   superset of T6's portable target? The case for keeping it is that Clang's and
   MSVC's ASan implementations do not catch identical defects; the case against
   is runtime cost and a second red lane to watch for a marginal second opinion.
   T7 decides and records the reason. TSan and UBSan are not in question — MSVC
   provides neither, and they are the lane's actual justification.
