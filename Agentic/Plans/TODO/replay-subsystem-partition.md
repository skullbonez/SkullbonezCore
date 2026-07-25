# Replay Subsystem Partition

Date: 2026-07-25

Owner: Runtime/Replay + Runtime composition

State: IN PROGRESS (RS0-RS1 complete; RS2 next)

Ledger tasks: 6 (RS0-RS5)

Branch at registration: `main` (tip `c670e95f`)

Impact area: Runtime package structure, replay/prediction/planning ownership,
project files and filters, dependency enforcement, allocation-policy inventory

Priority: High. Replay is the engine's designated flagship subsystem, and the
2026-07-22 owner ruling stands: its size alone is not a finding and this plan
is not a slimming exercise. The finding this plan owns is **accretion**: the
`Runtime/Replay` package has become the default landing zone for every
product feature that consumes predicted or recorded data, so its name no
longer describes its contents and each future prediction-consuming feature
will enlarge it by default.

Implementation mode: use `Agentic/Skills/orchestrator/SKILL.md`. This plan
requires one independent rubber-duck review at whole-plan closure.

## Registration

This plan is registered in `Agentic/Plans/MASTER-PLAN.md` as a six-task
active architecture plan inside the 2026-07-25 round-4 campaign. Binding
campaign order is: 1 `ui-renderer-hard-boundary`, 2 this plan,
3 `downward-domain-bleed-remediation`.

Required plan-runner commit first line:

```text
Replay Subsystem Partition, TASK <DONE> / 6, <OVERALL_PERCENT>% OVERALL COMPLETE — <ACTION SUMMARY>
```

The live post-header-remediation ledger is 1/17 before RS0. The task
percentages after RS0-RS4 are 12%, 18%, 24%, 29%, and 35%. At RS5 closure,
inventory rule 4 removes this completed six-task plan, leaving 1/11 = 9%.
Recalculate from the authoritative master ledger if the portfolio changes.

## Problem And Measured Evidence

Census of 2026-07-25 at `main` tip `c670e95f`
(`git ls-files SkullbonezSource/Runtime/Replay`):

| Measurement | Value |
|---|---:|
| Files under `Runtime/Replay/` | 72 |
| Lines under `Runtime/Replay/` | 36,900 |
| Share of all `SkullbonezSource` lines (199,582) | 18.5% |

The package currently composes three distinguishable domains through one
`ReplayRuntime` owner ([ReplayRuntime.h](../../SkullbonezSource/Runtime/Replay/ReplayRuntime.h)):

1. **Replay core** — capture, timeline, scrub, artifact, restore, fidelity
   validation: `ReplayRecorder` (3,203-line implementation), `ReplayV2Artifact`
   (2,737), `ReplayScrubberTools` (1,847), `ReplayValidation` +
   `ReplayValidation.Probes` (3,575 combined), `ReplayTimeline`,
   `ReplayPresentation`, `ReplayOverlayRenderer`, `ReplayAuthoring*`,
   `ReplayVisualPacket*`, restore/identity/probe headers. Roughly 46 files /
   ~26,400 lines.
2. **Prediction** — future-simulation cache, worker scheduling, publication,
   topology publication, reserve policy, archive, retained trajectory storage,
   prediction drawing: the 18 `ReplayPrediction*` files plus
   `TrajectoryStore.*`. Roughly ~9,100 lines.
3. **Orbital planning product features** — operator-facing tools built *on*
   prediction, not part of it: `ReplayTripPlanner` (542 lines),
   `ReplayPorkchopPanel` (359), `ReplayGuideArcs` (232),
   `ReplayInterceptReadout` (240). Roughly 8 files / ~1,370 lines, all landed
   here by the 2026-07-23/24 solar-system campaigns because Replay owned
   prediction.

Domain 3 is the accretion proof: porkchop launch-window panels and transfer
trip planners are gameplay/tooling features. They live in Replay only because
their data source does. Without a structural boundary, the next
prediction-consuming feature (and every one after it) defaults into this
package, and "Replay" degrades into an unnamed miscellaneous-features layer —
the same god-object failure mode at package granularity that
`runtime-package-decomposition` closed for the Runtime root.

## Goal

Partition `Runtime/Replay` into three honestly named packages with an
enforced anti-accretion dependency direction:

- `Runtime/Replay` — capture, timeline, scrubbing, artifact save/restore,
  presentation of recorded data, fidelity validation. The reserve-growth
  privilege owner.
- `Runtime/Prediction` — future-simulation engine, scheduling, publication,
  topology publication, retained trajectory storage, prediction drawing,
  prediction archive.
- `Runtime/Planning` — operator planning features consuming prediction and
  replay outputs: trip planner, porkchop panel, guide arcs, intercept readout,
  and every future feature of this kind.

Behavior is frozen: this is a moves-and-includes partition with unchanged
runtime semantics, artifact bytes, golden manifests, and physics CSV.

## Target Dependency Shape

```text
Planning  -> Prediction, Replay, and standing lower-layer rules
Prediction -> Replay and standing lower-layer rules
Replay    -> standing lower-layer rules only; NEVER Prediction or Planning
```

- `Replay` must never include `Prediction/` or `Planning/`. This is the
  anti-accretion arrow: infrastructure cannot know about the features built
  on it, so new features physically cannot land "inside" Replay and still
  compile against their consumers.
- `Prediction` must never include `Planning/`.
- The standing Runtime package table in `AGENTS.md` gains rows for
  `Prediction` and `Planning`; every other package's allowed-target row is
  re-ratified by the RS0 census (expected consumers: `App`, `Render`, `UI`,
  `Editor`, `Tools`, `Capture`, `Automation`, `Diagnostics`, `Interaction`,
  `Scene`, `Startup` rows that currently name `Replay` may need `Prediction`
  or `Planning` added — each addition needs the census row that proves the
  concrete consumer).
- The Replay Boundary Rule extends verbatim to both new packages: `Physics/`,
  `Rendering/`, `Scene/`, `World/`, and `Core/` must not include
  `Runtime/Prediction/*` or `Runtime/Planning/*`.

## Non-Goals

- No behavior, artifact-format, golden, baseline, scene, config, or physics
  CSV change. Any divergence is a defect to investigate, never a refresh.
- No deduplication, slimming, rewriting, or API redesign beyond what a file
  move mechanically requires (include paths, project membership, namespace
  qualification where a file already uses relative spellings).
- No change to the replay reserve-allocation model itself — only an explicit,
  reconciled re-homing of inventory rows whose owning code moves.
- No new forwarding headers, umbrella headers, compatibility aliases, or
  re-export shims to soften the moves. A consumer includes the new location.
- No renaming of types during the partition. `ReplayTripPlanner` may keep its
  spelling while living in `Planning/`; honest renames are follow-up work the
  RS5 census may register, not silent scope growth here.
- No relocation of `Runtime/Replay`-external prediction consumers (Render,
  Tools, Editor trajectory presentation). Their include paths update; their
  ownership does not.

## Permanent Invariants

1. `Runtime/Replay` never includes `Runtime/Prediction` or `Runtime/Planning`.
2. `Runtime/Prediction` never includes `Runtime/Planning`.
3. Engine layers below Runtime never include any of the three packages.
4. Every reserve-growth privilege row names its owning package explicitly;
   a file move that carries a registered growth owner updates the inventory
   in the same commit.
5. A new operator-facing feature built on predicted or recorded data belongs
   in `Runtime/Planning` (or a future product package above it), never in
   `Replay/` or `Prediction/`. Adding planning/product vocabulary inside
   `Replay/` or `Prediction/` is a review failure.
6. One file lives in exactly one production project with exact filters.

## Preliminary File Disposition (RS0 ratifies or corrects)

| Destination | Files (preliminary) |
|---|---|
| `Runtime/Prediction` | `ReplayPrediction.{h,cpp}`, `ReplayPredictionDrawing.cpp`, `ReplayPredictionPublication.{h,cpp}`, `ReplayPredictionPublicationOperations.h`, `ReplayPredictionTopologyPublication.cpp`, `ReplayPredictionScheduling.{h,cpp}`, `ReplayPredictionReserve.{h,cpp}`, `ReplayPredictionArchive.{h,cpp}`, `ReplayPredictionArchive.Automation.cpp`, `ReplayPredictionView.h`, `ReplayPredictionPackets.h`, `TrajectoryStore.{h,cpp}` |
| `Runtime/Planning` | `ReplayTripPlanner.{h,cpp}`, `ReplayPorkchopPanel.{h,cpp}`, `ReplayGuideArcs.{h,cpp}`, `ReplayInterceptReadout.{h,cpp}` |
| `Runtime/Replay` (retained) | Everything else, including `ReplayRuntime` as the replay-core composer |

RS0 must additionally rule the genuinely shared seams rather than letting
them default: `ReplayVisualPacket.h` (consumed by prediction publication and
replay presentation), `ReplayCoordination.h`, `ReplayIdentity.h`,
`ReplayEventCommand.h`, the `Replay*Packets.h` value headers, and the
composition question of whether `ReplayRuntime` continues to construct the
prediction owners or whether `Run` composes a sibling `PredictionRuntime`.
The default ruling direction is: shared value headers stay in `Replay/`
(the lowest of the three packages) so the dependency arrows above hold
without a fourth shared package; composition stays in `ReplayRuntime` for
this plan unless the census shows a clean two-owner split with no
back-reference.

## Ledger

- [x] **RS0 — Ratify the complete source-derived partition census.**

  From the implementation tip, classify every file under `Runtime/Replay/`
  into Replay/Prediction/Planning/shared-value with per-file include evidence.
  Produce the full include-edge matrix among the 72 files and from every
  external consumer (`rg` over `SkullbonezSource` for `Runtime/Replay/`
  includes). Rule every shared seam listed above. Inventory every
  `RuntimeReserveAllocator` registration, phase gate, cap, and counter whose
  owning file moves, with its destination package. Enumerate the exact
  `AGENTS.md` package-table rows and proof commands that will change, and the
  dependency-validator rules and fixtures RS4 will add. Record all expected
  touched source-bearing files for the final comment audit.

  Acceptance:

  - Every one of the 72 files has exactly one named destination and the
    census reconciles to this plan's 72-file/36,900-line baseline or explains
    the drift.
  - Every cross-package include that would violate the target arrows has a
    named resolution (move the value header down, split the file, or re-rule
    the destination) — no edge is deferred without an owner note.
  - The reserve-inventory disposition covers every registered growth owner.
  - No source behavior changes in RS0.

  Evidence (2026-07-25):

  - `Agentic/Reports/2026-07-25/replay-subsystem-partition-rs0-census.md`
    classifies all 72 files exactly once and records every internal include.
  - The registration baseline is reconciled as 36,900 nonblank / 39,976
    physical lines; the RS0 tip is 37,022 nonblank / 41,849 physical lines.
  - The preliminary matrix has 188 internal edges. All 26 upward edges have a
    named split/removal resolution; none is deferred.
  - All 35 external include sites in 25 files and 12 Runtime packages are
    inventoried with the exact consumer-table changes for RS4.
  - All three reserve owners retain their names, Replay phase gates, caps,
    counters, and exhaustion rules; only the prediction owner moves package.
  - RS0 is documentation-only; no repository validation was required or run.

- [x] **RS1 — Extract `Runtime/Prediction`.**

  Create the package, move the RS0-ratified prediction files, update every
  include site and both production/test project files with exact filters.
  Apply the RS0 seam rulings that Prediction needs (e.g. value headers that
  stay in `Replay/`). No behavior change; no forwarding headers.

  Acceptance:

  - `Runtime/Replay` contains no `ReplayPrediction*` or `TrajectoryStore*`
    file; `rg -n 'Replay/ReplayPrediction|Replay/TrajectoryStore' SkullbonezSource`
    returns no rows.
  - No stale `Replay/ReplayPrediction*` or `Replay/TrajectoryStore*` include
    path remains. The temporary `Replay -> Prediction` edges created by the
    physical move are exactly the RS0 seam inventory and remain owned by RS3,
    which performs the composition/value-seam splits needed to eliminate
    them. Until RS3, the dependency validator allows that one temporary edge;
    RS3 removes the allowance and proves the permanent zero-edge invariant.
  - `tools\validate_project_filters.bat` passes; Profile and Debug build with
    zero warnings.
  - The mapped replay gates in the validation map pass with zero golden,
    manifest, or artifact refresh.

  Evidence (2026-07-25):

  - `Agentic/Reports/2026-07-25/replay-subsystem-partition-rs1-prediction.md`
    records the complete 18-file move, 42-file comment audit, temporary seam
    inventory, validator/project changes, and validation results.
  - No stale `Replay/ReplayPrediction*` or `Replay/TrajectoryStore*` source,
    test, project, or tool path remains; exact project/filter ownership passes
    across 767 items.
  - The 17 temporary `Replay -> Prediction` includes are the named RS0 seams
    and remain explicitly owned by RS3. The validator allowance is bounded to
    that sequencing decision and must be deleted by RS3.
  - Profile, Debug, and Automation build with zero warnings. Fast, strict
    replay allocation, V2 artifact, the sole visual-fidelity generation, and
    the cumulative full gate all pass.
  - No golden, baseline, manifest, replay artifact, scene, config, shader, or
    physics CSV file changed.

- [ ] **RS2 — Extract `Runtime/Planning`.**

  Move the four planning features into `Runtime/Planning`, update consumers
  (UI readout surfaces, Render/overlay submission sites, automation probes),
  and update projects/filters. No behavior change.

  Acceptance:

  - `Runtime/Replay` and `Runtime/Prediction` contain no trip-planner,
    porkchop, guide-arc, or intercept-readout source.
  - Neither `Replay/` nor `Prediction/` includes `Planning/`.
  - Projects/filters and zero-warning builds as in RS1; mapped gates pass
    with zero refresh.

- [ ] **RS3 — Reconcile composition, shared seams, and the reserve inventory.**

  Apply the RS0 composition ruling for `ReplayRuntime` versus a sibling
  prediction composer; re-home any remaining shared value header per the
  rulings; delete every temporarily widened include. Update the authoritative
  replay reserve inventory so each registered growth owner, phase gate, cap,
  and counter names its post-partition package, and extend the standing
  Replay Boundary Rule text to cover `Prediction/` and `Planning/`.

  Acceptance:

  - The include-edge matrix re-run shows only arrows permitted by the target
    shape; zero `Replay -> Prediction`, `Replay -> Planning`, or
    `Prediction -> Planning` rows.
  - The reserve inventory reconciles: every row has an owner package, and the
    strict two-generation Replay allocation gate reports the same owner set,
    caps, and zero policy violations as the pre-partition baseline.
  - No `void*`, callback pack, friend edge, or backpointer was introduced to
    make the composition split compile.

- [ ] **RS4 — Install the anti-accretion enforcement.**

  Update `AGENTS.md`: add `Prediction` and `Planning` rows to the Runtime
  package table, update every consumer row the RS0 census ratified, add the
  complement-pattern `rg` proofs for both new packages, and extend the
  Replay Boundary Rule proof to
  `Runtime/(Replay|Prediction|Planning)/`. Add the invariant-5 placement rule
  to the standing review rules. Register the same directional rules in the
  dependency validator delivered by `ui-renderer-hard-boundary` UR5, with
  positive and negative fixtures for each new edge class, so a future
  forbidden include fails `validate_fast`/CPU/full mechanically rather than
  by review vigilance.

  Acceptance:

  - All new/changed `AGENTS.md` proof commands return no rows from the tip.
  - Validator self-tests plus new fixtures pass; a deliberately planted
    `Replay -> Prediction` include and a `Prediction -> Planning` include
    each fail the validator in the negative fixtures.
  - No frozen-count, line-budget, or spelling-ratchet rule was added — the
    enforcement is directional edges plus the placement review rule only.

- [ ] **RS5 — Close behavior, ownership, and documentation.**

  Re-run the complete census and every standing direction proof. Audit every
  touched source-bearing file against the comment-style guide (moved files
  need their learning headers' `Related:` paths and package vocabulary
  corrected — a stale path is an audit failure). Run one independent
  rubber-duck review over the partition: its mandate is to find hidden
  accretion escapes (a planning feature left in Replay, a shared header that
  smuggles an upward edge, a reserve row without an owner). Any credible
  finding reopens the owning task.

  Acceptance:

  - All permanent invariants and static proofs pass from final source.
  - One independent review with no unresolved finding.
  - All mapped gates pass from final source with zero refresh of any golden,
    baseline, artifact, scene, or config.
  - Closure evidence is written under `Agentic/Reports/<date>/`, this plan is
    deleted under inventory rule 4, `MASTER-PLAN.md` and
    `Agentic/SessionState.md` record the handoff.

## Dependencies And Decisions

- Sequenced after `ui-renderer-hard-boundary` closes so RS4 registers rules
  in the UR5 validator instead of inventing a second enforcement mechanism.
  If that plan's UR5 is descoped, RS4 falls back to `AGENTS.md` `rg` proofs
  plus a dedicated small checker and records that decision.
- `downward-domain-bleed-remediation` DB1 moves retained-trajectory semantics
  out of `Rendering` and lands them in `Runtime/Prediction`; it therefore
  runs after RS1 creates the package. That plan owns the Rendering side; this
  plan owns the package homes.
- Owner ruling honored: this plan performs zero deduplication or slimming.
  The 2026-07-22 "internal-quality, not slimming" ruling stays binding.
- Inventory rule 11 binds every task: each RS task runs the single-invocation
  replay visual-fidelity mega gate exactly once; a second engine launch or
  prediction generation within one gate run is an immediate failure.

## Static Closure Proofs

```powershell
rg -n '^#include[[:space:]]+.*(Prediction|Planning)/' SkullbonezSource/Runtime/Replay
rg -n '^#include[[:space:]]+.*Planning/' SkullbonezSource/Runtime/Prediction
rg -n '^#include[[:space:]]+.*Runtime/(Replay|Prediction|Planning)/' SkullbonezSource/Physics SkullbonezSource/Rendering SkullbonezSource/Scene SkullbonezSource/World SkullbonezSource/Core
rg -n --ignore-case 'porkchop|tripplanner|guidearc|interceptreadout' SkullbonezSource/Runtime/Replay SkullbonezSource/Runtime/Prediction
```

All four commands must return no rows at closure. RS0 may extend this list
with the ratified per-consumer package proofs.

## Validation Map

| Phase | Iteration evidence | Pre-commit/closure gates |
|---|---|---|
| RS0 | Census tables and plan reconciliation | Documentation-only; no repository validation |
| RS1 | Zero-warning builds, include/project scans | `tools\validate_fast.bat`, `tools\validate_project_filters.bat`, `tools\validate_replay_visual_fidelity.bat` (single invocation), strict Replay allocation gate, `tools\validate_replay_v2_artifact.bat` |
| RS2 | Zero-warning builds, include/project scans | `tools\validate_fast.bat`, `tools\validate_project_filters.bat`, `tools\validate_replay_visual_fidelity.bat` (single invocation) |
| RS3 | Edge-matrix re-run, reserve-inventory diff | `tools\validate_fast.bat`, strict Replay allocation gate, `python tools\check_allocation_policy.py --repo .`, `tools\validate_replay_visual_fidelity.bat` (single invocation) |
| RS4 | Validator fixtures, proof-command output | `tools\validate_fast.bat`, `tools\validate_all_cpu_tests.bat`, direct validator run |
| RS5 | Final census, comment audit, review record | `tools\validate_full.bat`, `tools\validate_replay_visual_fidelity.bat` (single invocation), `tools\validate_replay_scrub.bat` if scrub-facing files moved |

Mapped gates are cumulative with the standing file-to-validation table. Any
task that touches render submission or DX12-visible presentation adds
`tools\validate_dx12_renderer.bat` plus the bounded graphics-stress proof.

## Closure Evidence Requirements

The closure report must contain:

- before/after file inventory per package with line counts;
- the complete ruled seam table and composition decision;
- before/after include-edge matrices and all proof-command outputs;
- the reconciled reserve-inventory table (owner, package, phase, cap,
  counter) with the strict allocation-gate result;
- validator fixture proof for every new directional rule;
- project/filter proof and zero-warning build evidence;
- confirmation of zero golden, baseline, artifact, scene, config, or physics
  CSV refresh;
- touched-source comment-audit inventory with checked/deferred counts;
- the independent review verdict and any remediation.
