# MASTER PLAN — Authoritative Remaining Work

Date: 2026-07-15
Status: Authoritative inventory of every live repository plan

## Inventory Rules

1. Live implementation plans are listed here and stored under `TODO/`, except
   the active engine-cleanup campaign file.
2. Completion uses checked-phase counts, not subjective percentages. Partial
   phases are named as partial and do not increment the completed count.
3. A checkbox closes only with its acceptance evidence and required validation.
4. Completed plans/checklists are deleted; git history is the archive. A
   completed plan may remain temporarily only when this file explicitly marks
   it as evidence for an unmet aggregate closure gate; delete it as soon as
   that gate passes.
5. Every dependency link must resolve to a live file. A missing link is a plan
   defect and blocks the dependent phase.
6. A plan must name owner, problem/evidence, goal, non-goals where needed,
   phases, dependencies/decisions, acceptance, and validation.
7. Source measurements are dated and scoped; historical numbers are not reused
   as current evidence.
8. God-object cleanup is reviewed across logical types/modules, not individual
   files. A short facade, shared context, callback bag, or forwarding owner does
   not satisfy an ownership deletion proof.
9. `Agentic/Plans/WNF/` holds owner-parked "will not do now" plans. Agents
   ignore that folder entirely — do not list, resume, update, or delete its
   contents unless the owner explicitly moves a file back out of it.
10. Every DX12 modification must complete a crash-free graphics-stress run of
    at least 10 seconds before commit/PR handoff. The standard bounded proof is
    `tools\run_graphics_stress.bat 1`; record the command, measured runtime,
    and successful exit evidence alongside the normal DX12 renderer gate.
11. Replay decomposition is guarded by the permanent frame-exact 200-box
    visual-fidelity command. Every task in both live replay plans runs
    `tools\validate_replay_visual_fidelity.bat`; decomposition may not refresh
    its golden manifest, and any refresh requires explicit owner approval. One
    gate invocation starts exactly one engine process and generates prediction
    exactly once. All later work is non-engine CPU/report/artifact comparison;
    a second `SKULLBONEZ_CORE.exe` launch or generation is an immediate failure.
    Aliases do not grant another invocation: every plan task runs one mega
    command total.

## Commit Progress Contract

Every commit produced by a plan runner must begin with the owning plan, that
plan's completed task count after the commit, and active/future portfolio
completion after the commit:

```text
<PLAN_NAME>, TASK <DONE> / <TASK_COUNT>, <OVERALL_PERCENT>% OVERALL COMPLETE — <ACTION SUMMARY>
```

Example with ten 10-task plans: if plans 1 and 2 are complete and plan 3 is at
5/10, the correct overall value is 25/100 = 25%:

```text
Plan 3, TASK 5 / 10, 25% OVERALL COMPLETE — implement the current slice
```

Rules:

1. `DONE` is the owning plan's completed ledger tasks after the commit, not the
   ordinal number of the commit or the number of raw Markdown checkboxes.
2. `OVERALL_PERCENT` is
   `round(100 * active/future done / active/future total)` using the
   authoritative ledger below. Never estimate it subjectively or include
   completed historical work.
3. Companion/progress checklists do not add a second denominator. Their count
   is represented by the owning plan's ledger row. The prediction plan is the
   current deliberate 50-task exception because its execution checklist is the
   accepted task source.
4. Overall progress covers active and future implementation plans only.
   Completed historical plans, retained closure evidence, owner-parked work,
   and externally blocked lanes do not contribute to either side of the
   percentage. Git history and reports remain the evidence archive. New,
   completed, activated, parked, blocked, or rescoped plans update the ledger
   and denominator in the same commit.
5. One plan owns each plan-runner commit. Split unrelated plan work. For an
   unavoidable aggregate governance/documentation commit, use `MASTER-PLAN`
   with the bounded governance task count and list every affected plan in the
   body; the overall percentage still comes from this ledger. Commits made
   outside a plan runner use normal commit subjects and do not claim plan
   progress.
6. Every plan-implementation prompt must include the fully resolved required
   first line before implementation begins. `AGENTS.md` and the orchestrator
   skill repeat this requirement so it is present in agent prompts, not merely
   discoverable here.

### Portfolio Progress Ledger

Scope: active and future implementation work only. Per the 2026-07-15 owner
decision, the active portfolio is the three runtime mass-reduction plans
below — the remaining items from the 2026-07-15 god-object review now that
the PhysicsWorld campaign is closed. The PhysicsWorld campaign (11/11) and
all earlier round-4/round-5 plans are historical work per commit-contract
rule 4. The externally blocked validation lane remains deliberately excluded.

| Plan | Done | Tasks | Plan complete |
|---|---:|---:|---:|
| init-startup-decomposition | 5 | 5 | 100% |
| run-member-and-include-shrink | 4 | 6 | 67% |
| wide-call-desc-struct-pass | 0 | 5 | 0% |
| **Active/future total** | **9** | **16** | **56%** |

The three round-5 plans (fp-envelope-hardening 4/4,
mutual-gravity-large-scene-fallback 3/3, math-fatal-survey-restoration 3/3)
completed at 10/10 on `15th-of-July-Night-Runner` and leave the ledger as
historical work per commit-contract rule 4.

## Current Execution Priority

The binding critical path is the 2026-07-15 runtime mass-reduction lane:
`init-startup-decomposition → run-member-and-include-shrink →
wide-call-desc-struct-pass`. Init startup decomposition is complete; the Run
member/include shrink is now the active serial step. The wide-call pass is
file-independent of the other two and may run as a parallel lane, but its T2
rebases on the Run shrink's `RunRender.cpp` edits if both are in flight. Every plan carries a
zero-baseline/zero-golden-refresh requirement; the wide-call plan's replay
task is additionally bound by inventory rule 11 (one mega-gate invocation,
one engine process, no golden refresh).

The PhysicsWorld campaign completed P0-P10 in strict order at 11/11 with zero
baseline refresh, a clear independent ownership review, and passing
full/performance/allocation gates. The 2026-07-15 round-5 lane is complete at
10/10 and all six round-4 plans are complete at 22/22, all on
`15th-of-July-Night-Runner`. Validation-gate V3 remains externally blocked
and deliberately excluded from this ledger. The previous replay critical path
completed on `nightrunner-14th-july`.

0. **Validation-gate V3 — blocked external lane.** Repository implementation is
   complete. Remaining work requires a real `merge_group` proof, required CPU
   branch protection, and trusted/ephemeral DX12 runner administration.
1. **Replay visual-fidelity mega probe — complete on
   `nightrunner-13th-july`.** The permanent frame-indexed 200-box golden,
   single-generation causal, and durable offline equality gate generates and
   presents exactly once; reconstruction is CPU-only and cannot predict or
   present.
   `Toppled` means at least 101 of 200 bricks are directly grounded and
   engine-sleeping throughout the final second; the approved base records 187.
   Every V0-V6 task ends with that command passing.
2. **Replay monolith decomposition — complete.** M0-M8 executed on
   `nightrunner-14th-july`. Every task,
   including documentation inventory, reruns the unchanged 200-box gate before
   it may be checked or committed. Remediation checkpoint 25 has moved all
   prediction and presentation owner implementations into their owner-named
   translation units, replaced root event encoding with one immutable recorder
   command, and reduced `ReplayRuntime.cpp` to 2,440 lines. M3 is now complete:
   the presentation TU consumes only the value-only `ReplayPredictionView.h`,
   not private prediction-owner internals. Destination-branch provenance and
   every per-checkpoint gate passed on `nightrunner-14th-july`. The ledger is
   9/9. M4 checkpoints 27-32 moved scrubber hit testing, visibility, and
   semantic pointer-action selection into `ReplayScrubber`, deleted six raw
   root cursor-forwarding APIs, moved typed replay gesture begin/end ownership
   into `RuntimeInteractionController`, and deleted root live-advance/velocity
   toggle forwarding in favor of explicit owner commands. `ReplayTimeline` now
   owns cold artifact decoding and atomic loaded-track installation. Root
   save/load composition is deleted or private, loaded-track activation is an
   explicit no-I/O cross-owner operation, and M4 is reclosed. M5 checkpoints
   33-34 deleted the authoring self-aliases and root-only state/query relays,
   moved cause-tree construction plus cause/velocity input execution behind
   `ReplayAuthoring`, and replaced prediction reach-back with queued value
   requests. M5 is reclosed. M6 checkpoint 35 reaudited the private prediction
   owner, immutable publication, owner-TU placement, and performance budgets,
   and deleted the now-stale `ReplayRuntime.cpp` allocation exception. M6 is
   reclosed. M7 moved probe decisions into `ReplayProbeRunner`, unified loaded
   presentation activation, introduced bounded tool/editor event outputs, and
   deleted private root forwarding relays. M8 removed the final `RunReplay*`
   file names, placed public helpers in owner namespaces, renamed the narrow
   restore transaction header, audited 42/42 touched source files, and reviewed
   all 64 root methods across three TUs as one logical type. The root retains
   only six cohesive replay owners with no reach-back or authority escape; the
   unchanged oracle and full final gates passed with no baseline refresh.
3. **Future-path vector splines — complete independent presentation lane
   (2026-07-14 owner request).** `TODO/future-path-vector-splines.md` (T1→T7)
   restyles the prediction view: near-black sky, thin anti-aliased
   vector-spline ribbons, comma-cycled color modes with UI reflection,
   selected-object-only glow, and deletion of the temporary authoring cycler.
   It touches no prediction simulation or capture code and may run in parallel
   with the decomposition lane, but its intentional presentation restyle will
   change presented prediction frames: any refresh of the frame-exact 200-box
   visual-fidelity golden manifest requires explicit owner approval per
   inventory rule 11 and must be sequenced with the decomposition lane's
   unchanged-golden requirement (restyle lands only with owner sign-off on the
   new golden, or after decomposition tasks that depend on the current golden
   are closed). T1 is complete: prediction now fades to a near-black dome with
   a faint horizon and no cloud/sun/shaft energy, then restores the authored
   sky on exit. T2 replaced glow bands with literal-pixel analytic-AA vector
   coverage and an emphasis-only halo/HDR branch. T3 deleted the random look
   explorer, period action, logs, mutable style state, and final color mutation;
   ordinary paths now use immutable zero-emphasis styles. T4-T5 add five
   deterministic allocation-free color modes across all path lanes, a comma
   cycle action, and matching HUD/option-row state. The 1,485-frame multi-level
   visual run passed with stable submissions, zero dropped segments, and no
   steady-state reserve growth. T6 routes the halo/HDR emphasis only through
   the selected future/past root and leaves every sibling lane on the flat
   default; the post-critique 1,485-frame focused run passed with 1,578 stable
   segments, zero drops, and no steady-state reserve growth. T7 final-source
   full, renderer, stress, and scrub-propagation gates pass. The owner-approved
   golden refresh used one authoritative engine process and one prediction
   generation; the refreshed 2,401-tick comparison, causal/durable checks, and
   every offline false-pass control pass.
4. **Adversarial-review round 3 — locally complete.** All ten tasks and the
   final independent review are closed; evidence lives in
   `../Reports/2026-07-13/adversarial-review-round-3-closure.md`.
5. **Adversarial-review remediation round 1 — locally complete.** All five
   active 2026-07-12 remediation plans are closed. The comment-rot sweep
   remains owner-parked in `WNF/` (no comment changes yet), so it is not live
   work or part of the portfolio ledger.
6. **Adversarial-review round 2 — locally complete.** EngineLog fatal-path
   thread safety, SpatialGrid input validation, AmortizedTask lifetime guards,
   and worker-pool exception-plumbing removal are complete and validated.

## Engine Cleanup Campaign

| Plan | State | Verified basis | Next work |
|---|---|---|---|
| [aggregate closure](../Reports/2026-07-12/engine-cleanup-aggregate-closure.md) | Complete | Independent full-module and narrow repeat reviews are clear | Campaign closed; eight retained evidence plans deleted |

## Active Architecture, Safety, And Test Plans

| Plan | State | Verified phase count | Next blocking action |
|---|---|---:|---|
| [validation-gate-integrity](TODO/validation-gate-integrity.md) | Blocked | 5/6 | V3 needs merge-group proof, required branch protection, and trusted/ephemeral DX12 runner administration |
| [replay-visual-fidelity-mega-probe](TODO/replay-visual-fidelity-mega-probe.md) | Complete | 7/7 | One engine, one prediction, 2,401 exact ticks, 187 grounded sleepers, durable CPU-only reconstruction, and adversarial closure approved |
| [replay-monolith-decomposition](TODO/replay-monolith-decomposition.md) | Complete on `nightrunner-14th-july` | 9/9 | Retain as closure evidence while the active/future portfolio continues with the spline plan |
| [future-path-vector-splines](TODO/future-path-vector-splines.md) | Complete on `nightrunner-14th-july` | 7/7 | Owner-approved golden reconciled; one-process 2,401-tick oracle and all final gates passed |

## Planned Architecture Work (2026-07-11 gap review)

Added from the 2026-07-11 architecture gap review; written before the same
day's overnight completions landed, then reconciled against them on merge.
Reconciliation notes live inside each plan.

| Plan | State | Verified phase count | Start condition / next action |
|---|---|---:|---|
| [sim-render-interpolation](../Reports/2026-07-12/sim-render-interpolation-closure.md) | Complete | 5/5 | Allocation-free live interpolation, deterministic capture pinning, coherent cameras/listener, review, and final gates complete |
| [editor-undo-redo](../Reports/2026-07-12/editor-undo-redo-closure.md) | Complete | 5/5 | Fixed command history, stable-id recreation, exact state-fingerprint proof, lifecycle clearing, review, and final gates complete |
| [data-format-versioning](../Reports/2026-07-12/data-format-versioning-closure.md) | Complete | 5/5 | Asset/config v0 upgrades, hull v1 window, no-downgrade writers, migration tool, review, and final gates complete |

## Adversarial Review Remediation (2026-07-12)

Source: 2026-07-12 independent adversarial source review of the DX12 backend,
physics core, math layer, and frame loop (findings referenced with file:line
evidence inside each plan). Grouped by owner ruling into must-do and
nice-to-have lanes.

Must do:

| Plan | State | Verified phase count | Start condition / next action |
|---|---|---:|---|
| [dx12-descriptor-and-handle-lifetime](../Reports/2026-07-12/dx12-descriptor-and-handle-lifetime-closure.md) | Complete | 5/5 | Fence-safe SRV/UAV and framebuffer RTV/DSV reclamation, generation handles, and 131-turnover stress proof complete |
| [determinism-contract-hardening](../Reports/2026-07-12/determinism-contract-hardening-closure.md) | Complete | 4/4 | Explicit `/fp:precise` pins, complete chunk-accumulation audit, documented MSVC v143 envelope, and byte-exact isolated physics gates complete |
| [upload-arena-overflow-policy](../Reports/2026-07-12/upload-arena-overflow-policy-closure.md) | Complete | 4/4 | Per-category accounting, bounded replay ribbons, phase-aware caller drops, and flush-free stress/perf/DX12 proof complete |

Nice to have (start only after the must-do lane closes):

| Plan | State | Verified phase count | Start condition / next action |
|---|---|---:|---|
| [frame-view-calling-convention](../Reports/2026-07-12/frame-view-calling-convention-closure.md) | Complete | 4/4 | Four non-copyable capability slices replace high-arity frame calls without recreating a universal context bag |
| [render-interface-and-workerpool-slimming](../Reports/2026-07-12/render-interface-and-workerpool-slimming-closure.md) | Complete | 5/5 | Typed fixed-ring dispatch, measured interface retention, independent review, and full/perf/DX12 stress gates complete |

## Adversarial Review Remediation Round 2 (2026-07-12)

Source: second 2026-07-12 adversarial pass over post-remediation source
(worker primitives, logging/fatal path, broadphase input validation). All four
findings are consolidated into one plan because they share a theme — internal
contracts enforced by convention instead of code — and none justifies a
separate validation cycle.

| Plan | State | Verified phase count | Start condition / next action |
|---|---|---:|---|
| [runtime-contract-enforcement](../Reports/2026-07-12/runtime-contract-enforcement-closure.md) | Complete | 5/5 | Mutex-owned logger, bounded fatal probes, broadphase input guards, task lifetime enforcement, and final gates complete |

Recorded clean in the same pass (no plan needed): `Fence` signal/wait
ordering, upload-reservation saturating arithmetic, replay `make_unique`
allowlist compliance, descriptor free-list double-alloc/double-free checks,
and `strtod`-based config parsing.

Owner-parked 2026-07-12 (inventory rule 9 applies — not live work, not in the
ledger): `WNF/dx12-frame-path-comment-rot-sweep.md`. The owner ruled no
comment changes yet; the Present GPU-timer dead-store finding it carries stays
recorded there for when the plan is restored.

Deliberately not planned (owner may revisit): AoS `PhysicsBodyRecord` layout
reshaping and terrain warm-start/clamp heuristic replacement — both working,
honestly documented, and baseline-entangled; undertake only with a concrete
perf or stacking-stability motivation. Repeated glossary-header deduplication
is available as a documentation-only plan if the owner wants it (currently
excluded by the same no-comment-changes ruling).

## Adversarial Review Remediation Round 3 (2026-07-13)

Source: 2026-07-12 owner-commissioned adversarial architecture review of the
full source tree at the `nightrunner-11th-july` tip, re-verified against the
`nightrunner-12th-july` tip on 2026-07-13. Owner ruled findings in or out on
2026-07-12/13; in-scope work is consolidated into one ten-task plan ordered
header hygiene → mechanical namespace/ownership passes → C++20/`std::span` →
solver SIMD → DX12 bindless and frame headroom.

| Plan | State | Verified phase count | Start condition / next action |
|---|---|---:|---|
| [adversarial-review-round-3](../Reports/2026-07-13/adversarial-review-round-3-closure.md) | Complete | 10/10 | Three-frame SM6.6 bindless raster path, measured perf budget, independent review, and final gates complete |

The owner restored replay subsystem right-sizing to live work on 2026-07-13 as
the two ordered replay plans above, guarded by the frame-exact 200-box visual
fidelity gate. Remaining owner-ruled exclusions from this round are unit-test
depth expansion, sleep parallel-array consolidation, `Init.cpp` decomposition,
and any `RenderBackendDX12` re-partitioning beyond the bindless/frame-headroom
task. The unpinned-`/fp` finding was already closed by
`determinism-contract-hardening`.

## Adversarial Review Remediation Round 4 (2026-07-15)

Source: 2026-07-15 owner-commissioned hostile review of the full tree at the
`nightrunner-14th-july` merge (PR #120). The owner ruled six findings into
scope on 2026-07-15 and selected the approach for each (recorded per plan
under Dependencies And Decisions): drain-per-frame message pump; data-driven
shadow caster streams; Vector3 inline-only (padded-SIMD and SoA deferred);
math Try-APIs plus debug assert; deterministic parallel exact-sum mutual
gravity (approximation rejected); and signature decomposition only from the
god-object finding (PhysicsWorld/Init TU splits and Run member shrink
deliberately deferred).

Binding execution order — small isolated wins first, shared-file work last:

1. `win32-message-pump-drain` (isolated frame-loop fix)
2. `data-driven-shadow-caster-streams` (isolated renderer fix)
3. `vector3-inline-hot-math` (header-wide, must precede the math API change)
4. `math-fatal-removal` (same function bodies as 3; runs after it)
5. `deterministic-parallel-mutual-gravity` (perf evidence lands on inlined ops)
6. `runtime-signature-decomposition` (rebases on 1's `RunFrame.cpp` changes)

| Plan | State | Verified phase count | Start condition / next action |
|---|---|---:|---|
| `win32-message-pump-drain` | Complete | 3/3 | Bounded drain-then-frame loop, interactive input/quit smoke, and unchanged full gate passed |
| `data-driven-shadow-caster-streams` | Complete | 3/3 | Owner-prepared opaque stream ids replace renderer content sniffing; tests, unchanged DX12 baselines, and stress passed |
| `vector3-inline-hot-math` | Complete | 3/3 | Header-inline operations, trivial copy/assign, 12-byte ABI, unchanged byte-exact physics baseline, and no perf regression proved |
| `math-fatal-removal` | Complete | 4/4 | Try normalization/division APIs, classified callers, deterministic fallbacks, zero Maths fatals, and unchanged physics baseline proved |
| `deterministic-parallel-mutual-gravity` | Complete | 4/4 | Fixed chunks build unique pair slots; original-order replay is byte-exact across 0/1/4 workers and scales pair construction by about 48% |
| `runtime-signature-decomposition` | Complete | 5/5 | Eleven owner-plumbing calls now use stack-only frame views at 4–6 arguments; the 208-row inventory, independent review, and unchanged full gate passed |

Every plan in this round carries a zero-baseline-refresh requirement: pump,
streams, inlining, fatal removal, and gravity must all pass their mapped gates
against unchanged committed baselines. Gravity satisfies the bitwise-identity
obligation with one unique slot per pair and serial replay in the original
triangular order; chunk-local body sums were rejected because they regroup
floating-point additions.

## Adversarial Review Remediation Round 5 (2026-07-15)

Source: 2026-07-15 owner-commissioned adversarial review of the round-4
claims and diffs on `15th-of-July-Night-Runner`. Two findings reopen round-4
closures (per the review-finding rule, a credible finding reopens the owning
checklist item); one hardens the FP envelope that the Vector3 inlining flip
exposed. Owner rulings 2026-07-15: FP work is layers 1-3 only (diagnose, pin
contraction, re-scope contract docs) with manifold hysteresis explicitly
deferred to the future SIMD lane; the gravity fix restores the old envelope
via an exact serial fallback rather than growing scratch or approximating.
Findings ruled documentation-honesty only (the "as material data" framing of
the pine relocation, the dropped `std::floor` transcription note, boilerplate
keep-reasons in the 208-row inventory, and the report-path convention miss)
are recorded here without plans; re-litigation requires new evidence.

| Plan | State | Verified phase count | Start condition / next action |
|---|---|---:|---|
| `mutual-gravity-large-scene-fallback` | Complete | 3/3 | Above 512 bodies, the original exact serial triangular loop bypasses pair scratch and workers; tests, allocation checks, and unchanged physics baseline passed |
| [fp-envelope-hardening](../Reports/2026-07-15/fp-envelope-hardening-diagnosis.md) | Complete | 4/4 | The Profile-only ff6e780e inline flip is diagnosed as an inlining/packed-SSE optimizer-boundary change; forced `fp_contract(off)` now covers all four projects, the certified envelope is documented honestly, and full/perf gates passed without a baseline refresh |
| [math-fatal-survey-restoration](../Reports/2026-07-15/math-fatal-call-site-survey.md) | Complete | 3/3 | The regenerated survey reconciles 23 Vector3 named calls, 52 Vector3 divisions, and 24 Quaternion spelling matches; no migration gap remains, so closure is documentation-only |

## PhysicsWorld Stage-Owner Decomposition Campaign (2026-07-15)

Source: the 2026-07-15 god-object review named `PhysicsWorld.cpp` (3,954
lines, ~45 flat `m_*` members spanning seven concerns, one continuous step
path) the largest remaining mass after the round-4 signature work. The owner
activated the campaign on 2026-07-15. It completed P0-P10 at 11/11 with the
closure evidence recorded in the linked report below.

Binding campaign rules (from the plan, restated here because they gate every
commit): concrete stage owners under `Physics/Stages/` with value-context
inputs and no reach-back — never a TU split of the `PhysicsWorld` class; one
owner extraction per task per commit; byte-exact `validate_physics` after
every task with revert-on-diff (never fix forward a float difference); zero
baseline refresh for the entire campaign; allocation-allowlist rows move with
their vectors in the same commit; P7 (sleep controller) adds one
`validate_physics_deep`; P10 is a mandatory independent ownership review over
the whole logical module with full closure gates compared against P0
certification numbers.

| Plan | State | Verified phase count | Start condition / next action |
|---|---|---:|---|
| [physicsworld-stage-owner-decomposition](../Reports/2026-07-15/physicsworld-stage-owner-decomposition-closure.md) | Complete | 11/11 | Seven concrete owners, zero credible final ownership findings, full/perf/allocation gates passed, and no baseline refresh |

## Runtime Mass Reduction Campaign (2026-07-15)

Source: the remaining unaddressed items from the 2026-07-15 god-object
review, activated by the owner on 2026-07-15 after the PhysicsWorld campaign
validated clean. Owner rulings recorded per plan: the round-3 parking of
Init.cpp decomposition is lifted; the Run shrink allows at most two new
cohesive owners and forbids a services bag (a member fitting neither owner
stays on `Run` with a reason); the wide-call conversion threshold is ≥12
arguments, with 7-11-arg rows keeping their existing inventory dispositions.

Standing hazards binding every plan in this campaign: zero baseline, golden,
or screenshot refresh; move-only semantics with identical call positions,
strings, and exit codes; the Init split is a free-function file split (the
class-TU-split prohibition does not apply there, but cross-module internal
reach is banned); the Run shrink requires a single end-of-plan independent
ownership review; replay-touching wide-call work runs the one-invocation
200-box mega gate per inventory rule 11.

| Plan | State | Verified phase count | Start condition / next action |
|---|---|---:|---|
| `init-startup-decomposition` | Complete | 5/5 | Init is a 453-line process orchestrator; four focused Startup owners, exact CLI proofs, independent ownership review, final full gate, and both manual exit-code probes are closed in `../Reports/2026-07-15/init-startup-decomposition-map.md` |
| [run-member-and-include-shrink](TODO/run-member-and-include-shrink.md) | Active | 5/6 | T6: run full, explicit DX12 renderer, and one-minute graphics-stress closure gates with unchanged baselines |
| [wide-call-desc-struct-pass](TODO/wide-call-desc-struct-pass.md) | Active | 0/5 | T1: re-resolve the ≥12-arg inventory rows post-PhysicsWorld, then desc-struct conversions with designated initializers |

## Features

| Plan | State | Verified phase count | Start condition |
|---|---|---:|---|
| [shadow-edge-quality](../Reports/2026-07-12/shadow-edge-quality-closure.md) | Complete | 5/5 | Fixed Poisson filtering, detail-first terrain sampling, texel snapping, measured presets, and no-cascades decision complete |

Fracture replay was moved to `WNF/` by the owner on 2026-07-11 (inventory
rule 9 applies — it is not live work and is not tracked here).

## Binding Decisions And Open Decisions

Binding:

- DX12 explicit helpers own live barriers; RenderGraph does not become a barrier
  compiler.
- Any DX12 modification requires a crash-free graphics-stress run lasting at
  least 10 seconds; `tools\run_graphics_stress.bat 1` is the standard bounded
  proof.
- No exceptions in engine code; recoverable failures must propagate rather than
  disappear.
- `Run` remains only process/frame composition after five named ownership
  extractions.
- Scene-lifetime physics ownership is promoted through
  `SceneController`/`PhysicsScene`; `Run` wires it and `GameModelCollection`
  stops owning `PhysicsEngine`.
- Inspect and Editor share one stable selection identity; workspace-specific
  gesture/presentation state remains separate.
- Completed files are deleted rather than archived in the tip tree, except for
  the temporary aggregate-closure evidence allowed by inventory rule 4.
- Root-signature work executes in one ownership sequence: render-backend A2
  establishes the pipeline owner, shader P1-P3 modernizes and consolidates its
  contract, and shadow S1 extends that surviving contract.
- Scene schema v2 and its deterministic v1 upgrade are the versioning-policy
  precedent. Other formats adopt the same semantics without renaming the scene
  field, resetting its version history, or adding a competing scene migration.
- The Current Execution Priority critical path is binding. Plan-local work may
  run early only where that section explicitly names a preparation or parallel
  lane; it must not cross a listed dependency barrier.
- Replay visual fidelity is frozen before replay ownership moves: V0-V6 builds
  and closes `tools\validate_replay_visual_fidelity.bat`, then every M0-M8
  decomposition task reruns it against the unchanged approved 200-box manifest.
  Refactors cannot authorize a baseline refresh.
- The active decomposition and future spline plans execute on
  `nightrunner-14th-july` by explicit 2026-07-14 owner decision. Decomposition
  retains the unchanged passing baseline provenance requirement; spline
  presentation changes still require explicit approval before any golden
  manifest refresh.
- 2026-07-11 owner ruling (definitive): no `SimulationController` — the
  implemented `SimulationSystem` pacing / `SceneController` ownership / `Run`
  frame-order split stands. No unified `EntityId` registry —
  `PhysicsSceneObjectId` is the engine's single cross-system object identity;
  per-subsystem handles remain the hot-path currency.

Open and blocking:

- CI: register a GPU-capable Windows/DX12 runner before making runtime CI a
  required check; CPU Windows CI does not wait for that runner.

## Engine Cleanup Campaign Closure Gate

Before deleting `runtime-shell-decomposition.md` or closing the engine-cleanup
campaign:

- [x] One final independent ownership review covers the complete logical `Run`
  surface, every extracted owner, and the current high-fan-in/mega-module
  inventory. It records zero credible god-object, shared-state-hub, callback-bag,
  forwarding-facade, or renamed-compatibility findings.
- [x] The review's method/field ownership inventory, inspected hotspot list,
  concrete evidence, and zero-finding verdict are committed under
  `Agentic/Reports/<date>/`. Any credible finding reopens its owning plan and
  blocks campaign closure.

## Plan Closure Checklist

Before deleting any plan:

- [x] Every phase checkbox is complete with evidence.
- [x] All hard decisions are resolved in the plan or a binding owner record.
- [x] Required focused and broad gates passed from final source/data state.
- [x] New/changed test targets are registered in the CPU umbrella.
- [x] Source comment audit requirements are satisfied.
- [x] Current measurements and deletion proofs are rerun.
- [x] Session state and this inventory are updated in the same commit.
- [x] The plan and completed execution checklist are deleted; commit history is
  the archive.
