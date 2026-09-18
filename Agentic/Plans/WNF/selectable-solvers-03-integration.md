# Selectable Solvers 3: Product Integration And Acceptance

Date: 2026-09-17
Status: WNF - owner-requested plan only; 0/6 phases complete.
Owner: Runtime composition, Physics settings, Planning Solver Lab, Replay
Impact area: settings/UI, scene lifecycle, prediction, replay, comparison, tests
Commit name: `SOLVER_INTEGRATION`
Estimate: Re-estimate for nine algorithms after the stage 2 state/format audit.
The former two-algorithm estimate is withdrawn.

## Owner Direction And Dependencies

Make Skullbonez and all eight new solvers selectable, persistent and comparable,
using [stage 1](selectable-solvers-01-foundation.md) and
[stage 2](selectable-solvers-02-tgs.md). The required choices are Skullbonez,
PGS, PGS NGS, PGS NGS Block, PGS Soft, TGS Sticky, TGS Soft, TGS NGS and XPBD.
Stage 1 owns the exact durable keys and names. Skullbonez is the existing custom
PGS-based solver; the new PGS is a separate implementation. XPBD and every named
variant are required scope, not possible later additions.

Keep this plan in WNF until explicit owner restoration to TODO. It changes no
active portfolio count and authorizes no current implementation, PR or merge.
After activation use `Agentic/Skills/orchestrator/SKILL.md`. Documentation-only
drafting requires no runtime validation.

## Evidence And Product Goal

Read-only inspection on 2026-09-17 at `e59e397f8` identified:

- `SkullbonezSource/Physics/PhysicsRuntimeSettings.h`: per-engine settings values.
- `SkullbonezSource/Physics/PhysicsEngine.cpp`: entry to world stepping.
- `SkullbonezSource/Runtime/Prediction/ReplayPrediction.cpp` and
  `SkullbonezSource/Runtime/Prediction/ContinuousPredictionProducer.cpp`:
  independent prediction paths that must inherit effective solver settings.
- `SkullbonezSource/Runtime/Planning/PhysicsComparison.h`: recorded comparison
  evidence; the viewer must not invoke a solver to invent missing frames.
- `Agentic/Plans/TODO/physics-ab-comparison.md`: existing comparison work and
  incomplete acceptance. Reconcile its status at activation and reuse the
  existing product rather than building another comparison implementation.

Users can select an implemented solver, understand applicable settings, restart
into that configuration, save it, predict with it and compare recordings with
clear provenance. Product acceptance requires the actual owning world to agree
with the controls and with published prediction/comparison output.

## Product And Ownership Decisions

- Skullbonez stays default. Show only implemented/supported choices; completion
  requires all nine. Label new algorithms experimental until their numerical
  and product acceptance pass. Never use a generic PGS label for Skullbonez or
  a generic TGS identity for its three distinct variants.
- Keep algorithm-specific settings distinct. Use the applicability table below;
  show effective normalized values and units. Invalid saved values follow the
  same policy as commands. Never show a control that is silently ignored.
- Use an explicit apply-and-restart action. A pending choice never mutates a
  running world. Restart invalidates incompatible contact/joint state, prediction
  jobs and pending publications in an ordered lifecycle transition.
- Settings are per Physics world. Independent comparison captures cannot affect
  one another through global configuration. Runtime composes typed commands and
  snapshots; UI owns presentation values and Physics owns numerical behavior.
- Propagate identity/settings to prediction clones, forecast engines, replay
  branches and save/load. Settings and each algorithm's retained state matter for
  continuation; saving one does not prove the other is preserved.
- Version formats explicitly when required. Legacy files without solver fields
  mean Skullbonez under documented legacy defaults. Historical explicit custom
  PGS spellings, if any, migrate by version/provenance as stage 1 requires.
  Unknown algorithms, incompatible state versions and incomplete caches or
  multipliers fail continuation clearly. Never silently resume as Skullbonez,
  new PGS or another available backend. Save durable keys and settings/state
  versions, not menu indices or enum ordinals.
- Preserve read-only comparison. An archived recording may remain viewable
  without its original solver if its presentation data is self-contained;
  distinguish viewing from restoring a resumable simulation.
- Planning owns comparison controls/findings, Replay owns artifact data and
  Prediction owns future execution/publication. No downward Physics dependency
  on these consumers. No new Replay growth privilege is assumed.

## Settings And Comparison Contract

Stage 2 fixes numerical defaults and bounds; this stage presents that contract.
Do not invent universal iteration, warm-start or stiffness settings to make the
nine choices fit one panel. Distinguish simulation Hz from constraint Hz.

| Solver | Controls to project from its accepted numerical contract |
|---|---|
| Skullbonez | Existing custom controls and defaults, preserving behavior |
| PGS | Velocity iterations, applicable impulse warm start, bias controls |
| PGS NGS | Velocity and position iterations; velocity-cache warm start |
| PGS NGS Block | PGS NGS controls and documented block diagnostics |
| PGS Soft | Velocity/relaxation counts, constraint frequency/damping, warm start |
| TGS Sticky | Substeps and declared relaxation/bias controls; anchor diagnostics; impulse warm start unavailable |
| TGS Soft | Substeps, declared solve/relaxation counts, frequency/damping, warm start |
| TGS NGS | Substeps, declared velocity/position counts, applicable warm start |
| XPBD | Substeps, declared position/velocity passes, compliance with units; multiplier behavior per stage 2 |

Only expose counts stage 2 defines as configurable. Explain fixed/inapplicable
behavior through help. Saved per-algorithm preferences cannot leak into another
solver. Pending edits, applied settings and effective values remain distinct.
Cold apply-and-restart clears incompatible state even when switching within a
family, such as PGS NGS to PGS NGS Block.

Solver Lab must accept any ordered pair from the nine choices, including A/A.
Use independent captures from the same authored body/material/joint state and
outer-tick schedule; do not seed one solver with another's warmed cache. Show
actual algorithm keys, settings/version, timestep, build/content identity,
initial-state hash, measurement units and diagnostic availability on both sides.
For steady-state comparisons, run and identify a separate settling interval for
each solver. Compare both declared work counts and measured CPU cost.

Acceptance includes all nine A/A repeatability captures and all eight additions
against Skullbonez. Exercise all 81 ordered identity pairs through selection,
capture metadata and save/load tests; the expensive native numerical matrix need
not repeat 81 times. Native sampling must additionally cover a new/new pair from
different families, such as TGS Sticky versus XPBD. Pair results retain direction
and never replace unavailable data with current-solver reconstruction.

## Phases And Acceptance

- [ ] **SI0 - Inventory integration and compatibility.** Trace config, scene
  overrides, normalization, reset, save/load, cloning, replay formats, both
  prediction producers and comparison capture. Record one precedence rule and
  producer/consumer compatibility matrix. Resolve stages 1/2 state gaps before
  exposing selection UI.
- [ ] **SI1 - Add selection and safe restart.** Expose supported choices and
  settings through existing UI and typed commands. Prove pending/applied values,
  cancel/no-op, explicit restart, legacy default and invalid input. Clear caches
  and prevent old workers from publishing into a restarted world. Exercise every
  supported choice and prove Skullbonez and new PGS dispatch independently.
- [ ] **SI2 - Persist and propagate configuration.** Save/load authored choices
  and version affected artifacts. Prove prediction/forecast/clone settings match
  their source. Test uninterrupted versus restored continuation, legacy Skullbonez,
  malformed/unknown formats and rejection without partial world replacement.
  Cover all nine identities, wrong-backend caches, sticky anchors and XPBD state.
- [ ] **SI3 - Integrate Solver Lab.** Capture any two algorithms from the same
  authored physical state and outer tick schedule. Label each side with solver,
  effective settings, timestep, build/content identity and diagnostic availability.
  Preserve identities, timing and findings on save/load; never retime recordings
  or simulate missing evidence in the viewer. Complete the nine A/A, eight
  Skullbonez comparisons and 81 ordered-pair product checks specified above.
- [ ] **SI4 - Complete native acceptance.** Use Skarness to exercise restart
  during prediction, cross-scene/reset behavior, process restart persistence,
  independent paired captures and normal/narrow UI layouts across all nine
  choices. Check scrolling, setting applicability and complete name visibility.
  Assert actual world
  and published target identities. Inspect screenshots of settings/help/labels.
- [ ] **SI5 - Terminal closure.** Complete cumulative numerical, replay, UI,
  serialization and performance gates, independent rubber-duck/ownership review
  and final fixes, then full plan-completion validation. Record compatibility,
  measured quality/cost and limits for all nine algorithms. Keep Skullbonez
  default unless separately directed; eight new labels alone do not close scope.

## Required Behavioral Proof

| Scenario | Required observation |
|---|---|
| New/legacy scene | Effective Skullbonez and unchanged existing numerical baseline behavior |
| Apply each of eight new solvers and restart | World reports exact selected identity, intended settings and clean state |
| Skullbonez versus new PGS | Distinct durable identities and actual selected implementations |
| Prediction after selection | Source and published prediction solver/settings agree |
| Switch while worker runs | Old generation cannot publish into restarted world |
| Save/load continuation for all nine | Required retained state survives; no fallback/stale impulses, anchors or multipliers |
| Unknown solver/corrupt artifact | Explicit error; valid existing session remains intact |
| All nine A/A comparisons | Same-envelope repeatability and correct identity/tick matching |
| Every new solver versus Skullbonez; new/new pair | Same initial physical state, explicit settings differences and measured outcomes |
| All 81 ordered identity pairs | Correct independent selection, capture metadata and save/load |
| Cross-algorithm or malformed cache restore | Explicit rejection without replacing the valid world |
| Finding reopen/archived viewing | Provenance retained; no current-solver reconstruction |

An acknowledgement or changed dropdown is insufficient. Observe owner settings,
body identities, ticks and published metadata. No improvement claim follows
merely from successful selection or a visually plausible frame.

## Validation And Evidence

Focused coverage belongs in Physics, Replay and comparison owners, including
`SkullbonezTests/TestReplayArtifact.cpp`,
`SkullbonezTests/TestReplayDeterminism.cpp` and
`SkullbonezTests/TestPhysicsComparison.cpp`. Add malformed metadata, generation
invalidation, restore and independent-world cases. Iteration uses affected builds
and focused checks. Heavy suites/review/final fixes belong to SI5, subject to
mandatory earlier push-boundary gates.

Terminal commands from the repository root:

```bat
tools\validate_fast.bat
tools\validate_dependency_graph.bat
tools\validate_physics.bat
tools\validate_physics_deep.bat
tools\validate_perf.bat
tools\validate_replay_v2_artifact.bat
tools\validate_replay_visual_fidelity.bat
tools\validate_replay_allocation_policy.bat
tools\validate_ui.bat
tools\validate_skarness.bat
tools\validate_full.bat --plan-completion
```

Extend and execute the existing comparison and solver-UI validation owners:
`tools/validate_physics_comparison.py`,
`tools/validate_physics_comparison_integrity.py`,
`tools/validate_unified_solver_lab_ui.py` and
`tools/validate_unified_physics_ui.py`. Use their documented arguments and
current-build fixtures, recording exact commands when implemented. Archived
asset incompatibility cannot substitute for testing new captures. Register new
assertions in ordinary gates. Reconcile the final changed-file map for additional
mandatory checks; avoid repeating identical checks already run by the umbrella.

Follow `Agentic/Skills/skarness/SKILL.md` for native work, extending missing solver
controls/observations under standing authorization. Preserve commands, settings,
executable/content hashes, streams, screenshots and measurements under
`TestOutput/solver-selection/integration/`; stop owned sessions. Repeat stage 2's
quantitative matrix on the integrated final build. Separate same-solver
repeatability from cross-solver quality/cost comparisons.

Keep existing Skullbonez numerical goldens stable. New-solver fixtures and UI/metadata changes
follow AGENTS.md's applicable baseline policy; never refresh to hide unexplained
numeric or identity differences. Review Replay growth inventory if representation
changes affect registered caps. Leave local Profile ready and verify a no-op build.

## Nine-Solver Completion

Close a nine-row acceptance record containing durable key, implementation,
settings/state versions, supported features, prediction/forecast, clone/restore,
save/load, UI controls/help, Skarness evidence, A/A comparison, CPU/memory results
and numerical fixtures. Every row needs observed evidence, not only a dropdown.

This campaign adds exactly the eight named solvers and preserves Skullbonez.
Extra algorithms and deformable-body systems are outside scope. The previous
combined 3-5-week estimate described two algorithms and is withdrawn; estimate
the expanded work after the activation-time numerical and format inventories.

At closure reconcile MASTER-PLAN and SessionState with checked counts, evidence,
compatibility and limits. Delete completed plans under repository lifecycle,
repairing dependency links to closure commits/current sources first. Keep other
parked work inactive; create/merge PRs only on explicit user request.
