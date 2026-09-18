# Governance Review Remediation

Date: 2026-09-18
Status: Queued plan only; 0/8 phases complete. Implementation has not started.
Owner: Runtime domain owners, Physics compatibility, and repository validation
Impact: App, Planning, Replay, Automation, Render, tests, policy and review skills
Commit name: `GOVERNANCE_REPAIR`

## Goal and source

Resolve all ten findings in the [governance review](../../Audits/governance-review-2026-09-18.md)
with source repair, applicable policy/gate updates and a test or review that
would detect recurrence. Clean include graphs and compiler checks supplement
semantic ownership review; they do not substitute for it.

The review sampled a dirty c686f653b tree before later integration fixes. This
plan is based on main ff65964ba. GV0 must reconcile each claim with current source
and retained regressions; an already-fixed mismatch needs evidence, not another
implementation. The request is to create and publish plans on main. It does not
start implementation, approve baseline changes or accept any phase.

## Finding-to-phase map

| Review finding | Phase | Owning repair and recurrence proof |
|---|---|---|
| 1. Snapshot compatibility authorities disagree | GV1 | Physics predicates; edit/capture/save/load/restore/resume and future-version rejection |
| 2. Run retains product decisions/state | GV2 | Comparison-session and Physics-controls owners; independent transition tests |
| 3. Planning exposes mutable velocity workflow | GV3 | Command-owned state machine; invalid-order and immutable-observation checks |
| 4. Operator features live in Replay | GV3 | Move filters/gestures to Planning; artifact independence and direction checks |
| 5. Style 14 silently selects unrelated features | GV4 | Explicit geometry/AA/overlay choices; independent combinations and preset parity |
| 6. Retained regressions lack gate routes | GV6 | One executable case manifest; invocation, missing-case and child-failure tests |
| 7. Skarness uses ordinal payload slots | GV5 | Typed payload/results; compile-time dispatch and protocol compatibility checks |
| 8. Incremental scans imply exhaustive coverage | GV6 | Complete local file selection and an explicit all-first-party closure scan |
| 9. Renderer transactions retain participant borrows | GV4 | Values/cursor retained; synchronous borrows and snapshot/phase tests |
| 10. Single-borrow diagnostic wrapper | GV4/GV6 | Narrow const/value boundary; compiler candidate discovery and semantic review |

## Ownership and coordination

This plan owns comparison and Physics-controls extraction, including the shared
maintainability finding in [source quality](source-code-quality-remediation.md).
That plan owns parser finite conversion, replay file/decoded-memory budgets and
singleton chunk uniqueness. GV1 coordinates codec edits with SQ2/SQ3 but does
not duplicate their work. Preserve [Physics A/B](physics-ab-comparison.md)
acceptance and viewer behavior; extraction does not complete its open phases.

The [renderer plan](render-and-shader-audit-remediation.md) owns timing accuracy,
hull/UI/binding optimization and shader experiments. GV4 owns render contract
repairs and explicit preset resolution. Settle those seams before renderer
phases RS2-RS4 modify the same owners; independent measurement work can proceed.
Run and shared validation resources must have one writer at a time.

Extend the existing AGENTS.md clauses described in the review. For every rule
delegated to review, update both `Agentic/Skills/rubber-duck/SKILL.md` and
`Agentic/Skills/carmack-test/SKILL.md` in the same repair commit. Do not apply
policy prose now or close a finding on wording alone. Keep Runtime/App as
composition, Planning as product owner, Physics as snapshot-policy owner,
Rendering generic, and UI below Runtime and independent of Rendering.

No callback packs, retained host pointers, forwarding headers, compatibility
aliases, broad state bags, frozen counts or spelling budgets may hide an upward
edge or an unchanged owner. If an edge cannot be inverted, record its owner,
reason and deletion condition in this plan before changing it; none are approved
here. Rule-data edits require their fixtures and generated dependency proof.
Audit Replay reserve registrations when touching Replay; no new growth privilege
or capacity increase is approved by this plan.

## Phases

- [ ] GV0: Reconcile all ten dated findings with current main and tests.
- [ ] GV1: Unify snapshot compatibility policy and prove production restoration.
- [ ] GV2: Move comparison and Physics-controls decisions out of Run.
- [ ] GV3: Give Planning operator workflows and legal velocity transitions.
- [ ] GV4: Repair rendering choices, transaction borrows and diagnostic access.
- [ ] GV5: Preserve command/result types across the Skarness boundary.
- [ ] GV6: Make regression reachability and source-scan scope executable.
- [ ] GV7: Close all repairs with cumulative gates and independent review.

### GV0 — Current finding inventory

Use CodeGraph where available, then verify current files and call paths. Record
for findings 1-10: affected current symbols, all decision/write sites, current
status, prior fix commit where present, owning test and missing recurrence proof.
Inspect the logical Run surface, not one translation unit. Inventory affected
first-party files with `git ls-files`, keeping the list tied to implementation
scope instead of starting an unrelated full-engine rewrite.

Acceptance: no stale line number is treated as current proof; every finding has
an actionable remaining change or tested evidence that the source defect is
already repaired. Capture baseline behavior/identity without refreshing goldens.

### GV1 — One Physics compatibility policy

Inspect `PhysicsSolverSnapshot.h`, `PhysicsWorld.cpp`, `PhysicsEngine.cpp`,
`Runtime/App/ReplayRestoreOperations.h` and `Runtime/Replay/ReplayV2Artifact.cpp`
under `SkullbonezSource`. Give Physics the predicates for decode, inspection and
authoritative continuation. Readers and both normal/fallback App restore paths
consume them; remove independent version ceilings and stale protocol comments.

Acceptance: change settings, record, save, load, restore a noncheckpoint tick and
resume through production boundaries; prove effective settings, identity and
continuation state. Supported legacy/current versions have explicit behavior;
unsupported future versions reject before mutation. A temporary obsolete-version
ceiling makes the integrated test fail. Codec round-trip alone is insufficient.
Update the Replay Boundary Rule and both review skills with the same change.

### GV2 — Domain-owned comparison and Physics controls

Move comparison load jobs, request identity, foreground/activation, saved camera
and return transitions into a concrete comparison-session owner in Planning.
Move edit/reset/save/defaults policy and settings feedback into its honest domain
owner, with Physics-owned validation and typed cross-owner effects. App constructs
owners, sequences calls and applies effects; it does not retain parallel feature
flags or forward every business method through Run.

Acceptance: owner tests cover load/cancel, late completion from a stale request,
workspace return, camera restoration and scene replacement; settings tests cover
edit/reset/save, failed persistence and prediction/recording coordination.
These tests do not require constructing Run. Native behavior remains identical.
Review all touched Run siblings and correct misleading ownership comments.
Extend the God-Object Closure Rule and both review skills for new feature work.

### GV3 — Planning owns product interaction

Replace mutable pending-edit/divergence references with begin/edit/release/ready/
accept/cancel commands and read-only observations. Keep the original frozen
prediction identity, modified readiness and terminal cleanup in one owner.
Move cause filters, chip/key-edge state, velocity drag and operator cause-tree
state from ReplayAuthoring into Planning. Recording identity, artifact provenance
and necessary immutable recording values stay in Replay.

Acceptance: release before original readiness, duplicate release, early accept,
cancel during build and scene reset produce the documented legal state without
publishing stale identities. Former mutable observations fail compilation.
Cause filtering/gestures exercise Planning, while Replay artifact tests remain
independent of operator implementation. Update the Invariant Ownership and
Replay-Family Placement rules, both review skills, and changed dependency rules,
fixtures and proof together. This phase follows GV2 where it touches App state.

### GV4 — Honest render boundaries

Resolve authored Split Future presets once in Runtime into independent generic
geometry, antialiasing and overlay values. Preserve the existing serialized
preset/style interpretation at that boundary and its authored appearance.
Rendering must not infer unrelated feature behavior from style number 14.

Change WorldOverlayTransaction to retain detached bounded values and its phase
cursor; participant owners are borrowed synchronously by phase operations.
Preserve abandonment/double-submit guards and existing memory limits. Inventory
raw ResourceLifecycle/DX12-owner getters by caller and replace broad access with
focused operations where appropriate. Remove RuntimeRenderBroadphaseDebugView's
behavior-free mutable Physics wrapper in favor of a const borrow or bounded
Physics-owned diagnostic values.

Acceptance: rounded/plain geometry and SMAA off/on vary independently; preset
parity passes. Mutating the original packet after capture does not change the
transaction's submitted values. Phase misuse remains rejected; observations
cannot mutate Physics. Review nested spans and helper aliases for retained
borrows, and prove zero new steady-frame allocation. Update Rendering neutrality,
Invariant Ownership and wrapper-review rules with both review skills. GV6 owns
compiler discovery support for wrapper candidates, not a blanket no-pointer ban.

### GV5 — Typed Skarness transport boundary

Replace ordinal number/integer slots in `Runtime/Automation/SkarnessProtocol.h`
and parser/dispatch consumers with command-specific alternatives. Use semantic
identity types for stamp components; named uint64 fields alone do not prevent
swaps. Return typed alternatives rather than independent result-presence flags.
Share structured capability/parser descriptors where this removes duplicate
contracts without creating a new general-purpose dispatch framework.

Acceptance: external request/reply protocol and supported capabilities remain
compatible. Camera payloads cannot invoke velocity handlers; malformed/missing
identities reject without mutation; each result serializes only valid fields.
Transport, cancellation, request deduplication, command coverage and state-stream
checks continue to pass. Extend the Skarness workflow rule and both review skills.
Coordinate any payload changes required by GV2-GV4 before this migration.

### GV6 — Reachable regressions and accurate compiler coverage

Classify Split Future lines/SMAA and unified scene/tools UI scripts as maintained
regressions or one-off evidence. Put maintained native cases behind one executable
manifest consumed by existing runners, update file-to-gate mapping, and preserve
platform requirements. Test selected-case invocation, missing scripts and child
failure propagation with small fixtures; do not deliberately break the engine.

Extend source-design selection/self-tests to cover tracked changes, staged adds,
untracked project-owned sources, new headers and missing compile contexts.
Report exact files and contexts. Keep ordinary checks incremental; wire explicit
`--all-first-party` into the designated whole-engine closure route and remove
unsupported exhaustive claims. A planted unchanged-file violation must be found
by that route. Inventory behavior-free single-borrow aggregates using Clang;
review semantics and distinguish strong values/real behavior owners. Do not
introduce a parallel regex policy scanner or per-site exemptions.

Acceptance: runner negative controls fail the enclosing lane correctly; chosen
cases appear in execution reports; compiler fixtures prove every selection case
and named-wrapper renaming does not hide a candidate. Update the existing
validation rules, scripts/workflow documentation and both review skills together.

### GV7 — Terminal closure

Resolve every material finding and run cumulative final gates once the bulk of
implementation is complete. Independent review must answer aggregate invariants,
capability scope, incomplete extraction, renamed equivalent designs, truthful
comments, downward Replay edges and registered allocation growth. Review the
whole affected owner surface, not line counts. Preserve exact failing evidence
and any focused recovery; do not represent a failed umbrella as a pass.

Record phase acceptance, commands/results and artifact locations in the owning
plan and commit notes, reconcile MASTER-PLAN/SessionState, and remove the completed
checklist only under repository policy. No product phase closes through policy
wording alone; all eight remain open until the combined obligations are met.

## Validation for later implementation

During iteration compile affected targets and use focused owner, transition,
codec and runner tests. Apply comments while writing source. Heavy review and
gates belong at terminal closure, selecting cumulatively from actual edits:

- `tools/validate_fast.bat`, `tools/validate_tests.bat`,
  `tools/validate_dependency_graph.bat` and `tools/validate_replay_allocation_policy.bat`.
- `tools/validate_physics.bat`, `tools/validate_replay_v2_artifact.bat` and
  `tools/validate_replay_visual_fidelity.bat` for snapshot/Replay paths.
- `tools/validate_automation.bat` and `tools/validate_skarness.bat` for native
  comparison, controls, velocity, transport and state observations.
- `tools/validate_dx12_renderer.bat`, `tools/run_graphics_stress.bat 1` and
  `tools/validate_perf.bat` for render lifetime, visual or hot-path changes.
- `python tools/check_source_design.py --repo . --self-test` and the explicit
  `python tools/check_source_design.py --repo . --all-first-party` at closure.
- `python tools/check_plain_language.py --repo .`; run
  `python tools/check_commit_message.py --self-test` if editing the commit contract.
- `tools/agent_validate.bat --plan-completion` once at final plan closure;
  avoid duplicating lanes already satisfied by the umbrella on the same source.

Discover Skarness capabilities, query stable identities and owning state after
every control, retain streams/screenshots beneath `TestOutput/skarness/governance/`,
and stop owned sessions. Add missing narrow capabilities under standing authority.
Preserve all accepted goldens and use the established approval/transition contract
if a later intentional visual change needs one. Leave Profile built and confirm
an unchanged no-compile/no-link rebuild before claiming F5 readiness.

Planning validation: documentation only; no source repair, policy edit, build,
runtime validation or completed phase is claimed by publication of this plan.
