# Unit Test Coverage Campaign — Tiered Floors Over A Measured Baseline

Date: 2026-07-16
Status: ACTIVE — reordered ahead of the SoA/SIMD cutover by the 2026-07-16
owner instruction ("we need to make the unit tests first"). 6/10 tasks
complete; begin at U6. The SoA/SIMD campaign is PAUSED at 6/9 until this
campaign closes at U9. Rationale for the reorder: byte-exact gates cannot
verify behavior across the S7 regeneration (the cutover redefines the
goldens), but the behavioral/property suites built here are tolerance-based
and survive it — authored against today's scalar path, they become the only
independent oracle for the SIMD kernels, and S7's preconditions now include
these suites passing in BOTH toggle states. U5's single mega-gate invocation
cannot collide with SoA/SIMD while it is paused. U0's registration sub-step
(a) is satisfied; U0 begins at the tooling bring-up (b).
Impact area: `SkullbonezTests/*`, `tools/` (new coverage lane), coverage
tooling bring-up, `FIRST_TIME_SETUP.md`, per-subsystem unit coverage across
Maths/Core/Physics/Startup/Runtime logic owners
Owner: engine testing

---

## Problem And Evidence

The engine has world-class *system* oracles and an almost empty *unit* layer:

1. `SkullbonezTests/` is ~8,400 lines across 38 files (~202 doctest cases,
   ~12,600 assertions) against ~197,000 lines of product source — roughly 4%.
   The system gates (44,401-line byte-exact CSV, DX12 screenshot diffs, the
   200-box replay mega gate, bounded stress) carry nearly all verification.
2. System gates only cover states the gated content reaches. Proven twice in
   one week: the `ff6e780e` manifold knife-edge flip passed the full
   byte-exact baseline (the varied scene never parks boxes on that boundary),
   and finding R3-F1's artifact nondeterminism sat invisible until the first
   byte-compare of two artifacts.
3. Golden gates localize nothing: a CSV diff at tick 31,882 is a debugging
   session; a failing unit case is a line number.
4. Recovery paths are largely unexecuted by gated content: math Try-API
   fallbacks (survey: 12 reachable-degenerate rows), the >512-body gravity
   serial path (tested only since round 5), artifact version-rejection
   branches, CLI error paths.
5. There is no coverage measurement at all — no tooling, no baseline, no
   floors. The 2026-07-15/16 decomposition campaigns created the seams this
   plan needs: physics stage owners with typed inputs/outputs, isolated
   `Startup/` CLI units, and owner-partitioned replay codecs.

Owner direction 2026-07-16: no single global percentage target. Tiered
per-subsystem floors, ratified from a measured baseline; the global number is
an output, not a goal.

## Goal

Coverage is measured mechanically on every test run; tiered floors are
ratified and enforced by a repository gate; Tier-1/Tier-2 subsystems meet
their floors with behavioral (not implementation-mirroring) assertions; every
documented fallback and degeneracy path has at least one test proving it
fires; and `validate_tests` stays fast enough that nobody is ever tempted to
skip it.

### Ratification-Default Tier Map (U0 confirms or amends; owner signs)

| Tier | Scope (product lines) | Floor |
|---|---|---|
| 1 | `Maths/*`, `Core/SbResult`, core containers/trackers (`WorkerPool` task ring, allocation trackers) | 85% line |
| 2 | `Physics/` stores + `Physics/Stages/*` + solver helpers; replay artifact codecs (`ReplayV2Artifact`, `ReplayPredictionArchive` product paths); `Runtime/Startup/*`; `Core/Config*` + schema migration paths | 70% line |
| 3 | Runtime logic owners: `InputRouter` policy, `RuntimeInteractionController` transitions, scene controller logic, replay owner value seams | 50% line |
| 4 | DX12 backend, UI drawing, Win32 platform/window, presentation submission, automation harness | No unit floor — owned by screenshot/InfoQueue/stress/mega gates; excluded from floor math and documented as such |

## Non-Goals

- **No global percentage target.** The whole-engine number is reported, never
  gated.
- **No mocking framework, no DI retrofits.** The decomposition seams (stage
  owners with value contexts, isolated startup units, store records) are the
  test seams; a subsystem that cannot be tested without inventing a mock
  layer stays at its gate-covered tier with a recorded reason.
- **No product refactors for testability** beyond what U-task evidence proves
  minimal (e.g. a missing const accessor). Anything larger is a recorded
  follow-up, not silent scope creep.
- **No coverage gaming.** Floors are necessary, not sufficient: assertion
  rules below govern quality, and a test that executes lines without
  asserting behavior fails review.
- **No golden-mirror unit tests.** Tests assert behavioral contracts
  (invariants, documented fallbacks, boundary outcomes), never transcribed
  implementation outputs that rot into change-detectors.
- **No flaky inputs.** All randomized/property loops use fixed seeds and
  bounded iteration; wall-clock never influences a test outcome.

## Binding Design Rules (every task must respect all of these)

1. **Tests live in the main doctest runner** (`SKULLBONEZ_TESTS`) unless a
   task records why a standalone CPU executable is required; any new
   standalone executable must join `validate_all_cpu_tests.bat`,
   `tools/README.md`, and the AGENTS file-to-gate mapping in the same commit
   (existing repository rule).
2. **Mapped gates per task**: `SkullbonezTests/*` changes run
   `tools\validate_tests.bat`. Tasks that add `TestReplay*` or replay
   artifact/presentation tests additionally run
   `tools\validate_replay_visual_fidelity.bat` once per the mapping table —
   which means U5 must not run while another live plan holds the mega gate
   (see Dependencies).
3. **Speed budget**: `validate_tests` stays under ~60 seconds end-to-end.
   Per-case budget ~50 ms except explicitly marked slow cases; a task that
   blows the budget splits or optimizes before it closes.
4. **Assertion quality**: every new case asserts (a) a documented contract or
   invariant, (b) at least one boundary/degenerate input where the subsystem
   has one, and (c) failure-path behavior where a documented fallback exists.
   Case names state the contract ("TryNormalise leaves value untouched on
   zero input"), not the function name alone.
5. **Coverage floors measure product lines only**: test code, third-party
   (`ThirdPtySource`), generated files, and Tier-4 exclusions are outside the
   denominator, and the exclusion list is versioned in the floors config, not
   hardcoded in the checker.
6. **Determinism**: property loops use named constant seeds; any tolerance
   compares against documented epsilons; no test depends on thread timing
   (worker-pool tests pin thread counts explicitly).
7. **Comment standard applies** to substantial new test files (learning
   header naming the subsystem contract under test); trivial case additions
   to existing files follow the existing file's style.
8. **One task, one commit, gate evidence pasted** — same discipline as every
   other campaign.

## Task Checklist

- [x] **U0 — Tooling, baseline, floors ratification, and governance ruling.**
      (a) Register this plan in `MASTER-PLAN.md` (ledger 0/10, campaign
      section, critical-path placement) and `SessionState.md` — skip if an
      activation governance commit already did it; verify, never duplicate.
      (b) Coverage tooling: bring up `OpenCppCoverage` over the Debug test
      executables (`--sources SkullbonezSource` filtered, Cobertura XML
      export under `TestOutput/coverage/`), wrapped as
      `tools\validate_coverage.bat`; add a `tools/check_coverage.py`
      summarizer that reads the XML plus a versioned
      `tools/coverage_floors.json` (tier map, per-subsystem floors, exclusion
      globs) and exits nonzero on any floor breach. Ship floors as
      REPORT-ONLY in U0 (all floors 0) so the lane runs green from day one.
      Document installation in `FIRST_TIME_SETUP.md`. New tool scripts follow
      the mapping row for `tools/*`: `validate_fast`, then run the changed
      script.
      (c) Measured baseline: run the lane on the unmodified tip and commit
      `Agentic/Reports/<date>/unit-coverage-baseline.md` with per-subsystem
      line coverage, the exact command, and tool version. Historical claims
      (the ~4% estimate) are superseded by this measurement.
      (d) Owner ratifies the tier map and floors (defaults above; amendments
      recorded with reasons), including the Tier-4 exclusion list.
      (e) Governance ruling, recorded in the ratification: coverage floors
      are a quality gate, not a frozen-count debt ratchet; the AGENTS.md ban
      on ratchets for migration vocabulary does not apply. This sentence
      exists so a future runner cannot refuse the gate on principle.
- [x] **U1 — Tier 1: Maths and core primitives to floor.** Fill
      `TestVector3`/`TestQuaternion`/`TestMatrix4`(create as needed)/
      `GeometricMath`/`Frustum` to the Tier-1 floor with contract cases:
      normalization idempotence, rotation round-trips (quaternion↔matrix,
      with documented epsilon), look-at degeneracies (coincident eye/target,
      zero up — asserting the survey's recorded fallbacks), reflection/
      projection identities, frustum plane boundary containment (point
      exactly on plane), `IsCloseToZero`/`Simplify` tolerance edges, and the
      trivially-copyable/12-byte static contracts already asserted staying
      pinned. Add `SbResult` success/failure/message propagation and
      allocation-tracker phase-scope cases. Gate: `validate_tests` +
      `validate_coverage` report showing Tier-1 at/above floor.
      Completed 2026-07-16: canonicalized the coverage wrapper so stale XML
      cannot mask capture failure; measured Maths at 585/677 (86.41%) and
      core primitives at 917/1,038 (88.34%), both above the ratified 85% floor.
- [x] **U2 — The Try-API degeneracy matrix.** One test per
      reachable-degenerate row of
      `Agentic/Reports/2026-07-15/math-fatal-call-site-survey.md` (12 Try
      rows + the 4 `TryDivided` call sites): each proves the documented
      fallback actually fires and produces the documented value (e.g.
      `LookAt` zero-up becomes world +Y; impulse application with zero mass
      absorbs the component and writes no NaN). Add the negative-space cases:
      plain `Normalise` Debug-assert contract documented via a
      death-test-equivalent note (doctest cannot catch asserts — record the
      manual verification once), and Release IEEE propagation behavior for
      one representative case. Update the survey report linking each row to
      its test name so the matrix is navigable both directions.
      Gate: `validate_tests`.
      Completed 2026-07-16: all 12 reachable rows (including the four
      `TryDivided` rows) map bidirectionally to named call-site tests. Profile
      proves representative IEEE propagation; the Debug assert contract is
      recorded without stalling doctest. `validate_tests` passed 237 cases and
      17,835 assertions with a zero-warning Profile build.
- [x] **U3 — Physics stores and stateless stage owners.** Synthetic-store
      unit tests for `PhysicsBodyStore` (handle lifecycle: create/retire/
      reuse, hole compaction, pending-impulse preservation across descriptor
      refresh — the documented invariants in its header), `ColliderStore`
      row alignment, `SpatialGrid` boundary behavior (insert/query exactly on
      cell edges, cell-size extremes, the documented O(n²) degenerate-cell
      hazard), and the broadphase/force/terrain stages driven with
      hand-built body records: candidate-pair generation at contact-skin
      boundaries, mutual-gravity receives-flag gating (fixed/sleeping/
      massless combinations), terrain candidate commit ordering. The stage
      owners take value contexts — no engine, no scene, no worker pool
      (thread count 0) required. Gate: `validate_tests` +
      `validate_physics` byte-exact (pure test additions must not touch
      product source; if a task needs a product seam, that diff is isolated,
      named in the commit, and rides the physics gate).
      Completed 2026-07-16: focused cases now lock descriptor-reorder pending
      impulse ownership, dense body/collider realignment, exact/minimum and
      crowded-cell grid behavior, contact-skin equality, gravity receive flags,
      and terrain candidate row order/eligibility. `validate_tests` passed 244
      cases / 17,876 assertions; `validate_physics` matched 44,401 lines
      byte-exact with zero-warning Debug/Profile builds and no baseline change.
- [x] **U4 — Sleep controller, narrowphase, and contact-solver stages.** The
      state-machine tier: sleep counter thresholds (including the uint8 clamp
      at 255), island merge/wake propagation across support edges,
      underwater-lock entry/exit, point-joint island relaxation flags;
      narrowphase island building (pair→island partition determinism,
      min-pair-index ordering predicate); solver row preparation invariants
      (warm-start carry, friction limit signs, restitution output bounded by
      incoming speed × coefficient). Include the knife-edge lesson: manifold
      feature-selection cases placed a documented tolerance band AWAY from
      boundaries, plus one boundary-band case asserting the choice is
      *stable across repeated evaluation* (same inputs → same feature, ten
      times) rather than asserting which side wins — the fp-envelope
      fixture-construction rule, now enforced by example. Gate:
      `validate_tests` + `validate_physics` byte-exact.
      Completed 2026-07-16: direct stage fixtures now lock uint8 sleep-policy
      clamping, chained fixed-anchor support, underwater dormancy entry/exit,
      and original pair-slot ordering across two 256-island parallel passes.
      Solver coverage adds signed friction-cone and bounded restitution checks;
      the exact face-boundary manifold repeats ten times with stable ordered
      feature ids. `validate_tests` passed 249 cases / 19,052 assertions;
      `validate_physics` matched 44,401 lines byte-exact with zero-warning
      Debug/Profile builds and no baseline change. The U0 temporary topple
      exclusion is removed, and heap-owned determinism fixtures keep the Debug
      PE loadable for full instrumentation (clean-link image 0x25CFF000 rather
      than the stale/static-heavy 0x7C046000). `validate_coverage` passed with
      all cases participating and 12,223 / 21,120 whole-product lines (57.87%);
      `validate_fast` also passed after the wrapper change.
- [x] **U5 — Artifact codec round-trips and adversarial decode.** CPU-only
      encode→decode round-trips for the replay artifact writer/readers:
      chunk-table integrity, every chunk tag present/absent combination the
      manifest schema allows, then adversarial inputs — truncated chunk,
      length field past EOF, zero-count sections, unknown future version
      (must produce the documented recoverable rejection, not a crash or
      fatal), duplicate chunk tags, corrupt digests caught by the fingerprint
      path in Automation builds. Extends the existing legacy/current/future/
      writer tests the versioning policy already requires. If R4b's
      canonicalization has landed, add the determinism assertion at unit
      scale: two encodes of identical in-memory state are byte-identical.
      Mapped gates: `validate_tests`, then ONE
      `tools\validate_replay_visual_fidelity.bat` invocation (TestReplay*
      mapping row) — see Dependencies for sequencing against the replay
      campaign.
      Complete 2026-07-17: `TestReplayArtifact` now drives the production v4
      writer/reader through byte-canonical two-encode output, presentation
      round-trip, all-optional-chunks-absent cleanup, empty-recorder rejection,
      truncated payload, future version, past-EOF chunk range, duplicate BODY
      tag, zero BODY count mismatch, and header file-size mismatch. The one
      permitted mega invocation produced one fresh 2,401-tick/200-body artifact
      and all causal, semantic-packet, byte-mutation, prediction-state, and ten
      determinism false-pass controls succeeded offline. Its first fresh check
      exposed S4's previously unrefreshed default-OFF `physics_simd_kernels`
      config provenance; only `configSha256` and the dependent visual-manifest
      hash were updated after proving every behavioral golden value unchanged.
      `validate_tests` passed 252 cases / 19,153 assertions in 6.2 s;
      `validate_coverage` passed report-only at 12,955 / 22,764 whole-product
      lines (56.91%), with replay artifact codecs newly measured at 643 / 1,644
      (39.11%) and explicitly remaining U9 floor work. The engine run took
      about 6m26s after a 23.25 s zero-warning Automation build; no physics
      baseline, tick, visual, causal, scene, shader, or authored value changed.
- [ ] **U6 — Startup command line and launch resolution.** The Init-split
      payoff: exhaustive table-driven cases over `StartupCommandLine`
      (tokenizer quoting/whitespace edges, every flag/value directive family,
      malformed values, duplicate flags, the exact error strings and
      `FailCommandLineParse` behavior — strings asserted verbatim because
      they are frozen probe schema) and `StartupLaunchResolution` (scene vs
      suite vs hero resolution, extension/path syntax detection, missing-file
      fallbacks). Assert exit-code contracts for the probe entry points at
      the seam level without launching the engine. Gate: `validate_tests`
      (+ `validate_fast` if any `tools/` script is touched).
- [ ] **U7 — Config, schema migration, and property invariants.** (a)
      `Config`/`engine.cfg` parse: every physics default clamped/validated
      path, v1→v2 migration determinism (already partially covered — fill to
      the Tier-2 floor), unknown-key and out-of-range handling. (b) The
      property-invariant suite (seeded, bounded loops in doctest): impulse
      equal-and-opposite momentum symmetry across random body pairs;
      quaternion normalize idempotence and rotation-matrix orthonormality
      under composition chains; mutual-gravity Newton-pair antisymmetry
      (already bitwise-paired by design — assert it); spatial-grid
      insert/query round-trip under random AABBs including degenerate
      zero-extent boxes; restitution/friction outputs bounded for random
      contact configurations. Each property names its invariant and seed.
      Gate: `validate_tests` + `validate_physics` byte-exact.
- [ ] **U8 — Tier 3: runtime logic owners.** `InputRouter` binding-context
      enforcement and edge-memory (press/release across context switches),
      `RuntimeInteractionController` frame-policy table (the
      `BuildFramePolicy` input→policy matrix: pause lock, step-held, replay
      states), scene-controller request/queue logic, and replay owner value
      seams that are pure CPU (timeline cursor arithmetic, scrubber
      hit-region math, HUD value packets). Avoid anything requiring a
      renderer or engine loop — Tier-4 stays gate-owned. Gate:
      `validate_tests`; if any test touches `TestReplay*` naming, it moves
      to U5's gate rules instead.
- [ ] **U9 — Floors armed, ratchet ruling honored, closure.** Flip
      `tools/coverage_floors.json` from report-only to the U0-ratified
      floors; `validate_coverage` joins the AGENTS file-to-gate mapping and
      `tools/README.md` in the same commit (new gate = registered gate);
      re-measure and commit the final per-subsystem report next to the U0
      baseline with deltas. Single independent review over the campaign:
      assertion quality vs the anti-gaming rules (sample audit of ~20 cases),
      no golden-mirror tests, no flaky seeds, speed budget held, exclusion
      list honest. Closure report, MASTER/SessionState updates, plan deleted
      per inventory rule 4.
- [ ] **U10 — (Reserved decision row, not counted until ruled).** If U0's
      baseline reveals a Tier-2 subsystem whose floor is unreachable without
      product-source seams (anticipated candidates: `ReplayRecorder`
      hot-path capture, `SceneController` deep paths), this row is where the
      owner either grants a bounded seam task with its own gates or lowers
      that subsystem's floor with a recorded reason. It exists so mid-course
      discoveries amend the plan by ruling instead of by silent drift.

## Dependencies And Decisions

- Written as a draft on 2026-07-16 per owner instruction (no MASTER change);
  activation order relative to `replay-mass-reduction` (active) and the
  queued `physics-soa-simd-1000-bodies` draft is the owner's call at
  activation. Natural fit: after the replay campaign closes (frees the mega
  gate for U5) and BEFORE the SoA/SIMD campaign (whose S1-S2 store rewrite
  would benefit enormously from U3/U4 existing first — tests written against
  stage contracts survive the layout change and byte-gate it at unit scale).
- U5 requires one mega-gate invocation (mapping row for TestReplay*); it must
  not run concurrently with another live plan's rule-11 budget. If the
  replay campaign is still open when this plan activates, U5 is deferred to
  last (after U8) rather than double-booking the gate.
- Coverage tooling choice: `OpenCppCoverage` (free, MSVC-native, no
  instrumentation rebuild). If it proves unusable on the runner (record the
  attempt), the fallback is MSVC `/PROFILE`-based coverage via
  `vsinstr`/`vsperfcmd` — a U0 decision row, not an improvisation.
- Floors config is data (`coverage_floors.json`), so future floor raises are
  one-line diffs with owner sign-off, mirroring the allocation-allowlist
  pattern.
- The U0(e) governance sentence is deliberate: AGENTS.md bans frozen-count
  ratchets for migration debt; the owner ruling records that quality-gate
  floors are a different instrument and are sanctioned.

## Acceptance

- `validate_coverage` runs green with armed floors; Tier-1 ≥ 85%, Tier-2 ≥
  70%, Tier-3 ≥ 50% (or U0-amended values), Tier-4 exclusions documented.
- Every reachable-degenerate survey row maps to a named test; the survey
  report cross-links them.
- Property suite covers the named invariants with fixed seeds; zero flaky
  reruns observed across the campaign's gate history.
- `validate_tests` total runtime ≤ ~60 s at closure; case count and runtime
  recorded in the final report next to the baseline.
- Independent review finds no golden-mirror tests, no assertion-free
  coverage padding, and no product-source changes outside U10-ruled seams.
- Physics baselines byte-exact and untouched for the entire campaign; the
  single U5 mega-gate invocation is the only replay-gate use.

## Validation

- Per task: `tools\validate_tests.bat` + the `validate_coverage` report;
  U3/U4/U7 add byte-exact `tools\validate_physics.bat`; U5 adds one
  `tools\validate_replay_visual_fidelity.bat`; U0/U9 tool changes run
  `validate_fast` then the changed scripts per the tools mapping row.
- Closure (U9): final coverage report with per-tier deltas, full
  `tools\validate_full.bat` from the final tree, independent review
  evidence. All outputs pasted per task.
