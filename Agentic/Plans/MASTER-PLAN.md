# MASTER PLAN — Authoritative Remaining Work

Date: 2026-07-20
Status: Authoritative inventory of every live repository plan

## Binding Owner Directive — 2026-07-19: Finish UI, Then Resolve Physics

This directive supersedes any lower text that calls Physics P1 owner-blocked,
requires exact legacy/new evolved-output equality, or requires another owner
response for the two already identified P1 transition artifacts.

Execution order is binding:

1. Finish all automatable ImGui/Tracy E15-E17 implementation, focused coverage,
   validation, review, and handoff work first. If E14-E17 acceptance is held
   only by the existing Physics P1 replay golden or unavailable hands-on owner
   evaluation, retain the validated checkpoint, record that dependency, and do
   not stop.
2. Loop back to Physics P1 before starting any new plan. Perform the complete
   bounded-divergence assessment below, transition the accepted artifacts, and
   close P1. Do not wait for another owner response.
3. Reconcile and close any ImGui/Tracy checkboxes or replay gates that were held
   only by the old P1 golden, then continue Physics P2-P7 in order.
4. Only after both live campaigns have exhausted their automatable work may the
   next MASTER plan begin. A genuinely unavailable hands-on evaluation is
   recorded for later owner review; it does not prevent the physics loopback.

### Task-Scoped Bounded Deterministic Divergence

An explicitly planned physics work-order transition may produce deterministic
behavioral divergence. Exact equality against the pre-transition evolved run is
not required, and a fresh owner response is not required to update the artifact
classes named by the owning plan. The implementing/reviewing model is authorized
to accept the transition when it records a reasoned causal assessment that all
of the following hold:

- old and new implementations consume equivalent work from identical input
  state, using the plan's same-state oracle or an equally direct proof;
- the new implementation is byte-exact across its required repeat runs and
  worker counts;
- the **complete** artifact delta is inspected rather than accepting the first
  reported mismatch alone;
- the shape and scale of divergence are plausible consequences of the changed
  code and remain bounded: preserved identities and structural relationships,
  localized additions/removals or timing movement, and no unexplained wholesale
  rewiring;
- no body/work loss, NaN, explosive energy, invariant failure, crash, warning,
  allocation-policy failure, or unrelated scene/config/schema/render change is
  hidden by the refresh; and
- the mapped gates pass against the accepted artifacts from the final binary.

Membership overlap, additions/removals, parent/depth/type changes, timing-drift
distribution, state error, energy behavior, and cross-scene consistency are
decision evidence, not frozen global percentage ratchets. The model must judge
them in the context of the specific code change and explain why the result is
bounded rather than chaotic. Widespread unexplained divergence, nondeterminism,
or an implausible causal shape is rejected and reverted/deferred; it is never
normalized by updating a baseline.

For the current P1 instance, the known replay causal delta retains all 199 old
nodes, adds only body 11, removes none, and changes zero parent, depth, or
contact/motion classifications. Of the 199 retained nodes, 123 keep the same
first frame; absolute timing drift has median 0, p95 97, and maximum 246 over a
2,401-tick horizon. This is prima-facie bounded order-transition evidence, not
mass chaotic divergence. The reviewing model must still inspect the remaining
complete replay/query delta, but it is authorized to accept and commit the
causal replay golden and mechanically derived `physics_query_varied.json`
without another owner response when the rules above hold. Use the existing
one-process/one-generation artifact for assessment and offline reconciliation;
do not launch a second replay generation merely to approve it.

P1 closed on 2026-07-19 under that authority. The complete coupled visual,
causal, and 21-query delta was inspected from the existing one-process,
one-generation report; it retained the bounded causal shape above, preserved
all scene/config/schema/render provenance, and showed no body loss, non-finite
state, explosive energy, allocation violation, or unrelated refresh. The
transitioned replay/query artifacts pass exact offline controls plus final
physics, deep-physics, and performance gates. They are the exact baseline for
P2 onward.

### Binding Owner Acceptance — 2026-07-20: Retain P7 Sleepers And Refresh Baselines

The owner explicitly accepted the final deterministic sleeper/canonical-order
behavior, judged the sleeping-heavy gain worth the measured all-awake cost, and
directed all affected baselines to be rewritten. This is a P7-instance
acceptance, not a general permission to normalize future divergence. Final
physics baseline writers must still run from the final Debug executable and
remain exact; replay must still use one engine process and one prediction
generation; all final gates must pass against the accepted artifacts.

P7 satisfied that ruling on 2026-07-20. Physics CSV, known-signature, and
SkullScope query baselines regenerated to their existing bytes. Performance
baselines moved to the final source measurements. Replay behavioral fields
remained unchanged and only final-source provenance plus the mechanically
derived visual-baseline hash moved. Closure evidence is in
`../Reports/2026-07-20/physics-body-count-scale-closure.md`.

After an accepted transition, the new committed artifacts become exact again:
P2-P5, P7, and ordinary ImGui/replay work do not receive a general divergence or
golden-refresh allowance. P6 receives the same bounded-transition authority only
if its existing automatic evidence gate activates it.

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

The SoA/SIMD scale campaign is complete. Completed historical campaigns are
excluded under commit-contract rule 4. The externally blocked validation lane
remains deliberately excluded. Scene-controller ownership closed at 7/7 and
monolith TU right-sizing closed at 8/8 on 2026-07-18; both left the live ledger
under rule 4. Their closure evidence is in
`../Reports/2026-07-17/scene-controller-ownership-closure.md` and
`../Reports/2026-07-17/monolith-tu-right-sizing-census.md`. Code-level red-flags
C0-C6 closed on 2026-07-18 and also left the live ledger under rule 4. Its
closure evidence is in
`../Reports/2026-07-18/code-level-red-flags-closure.md`.

The denominator grew 0 → 25 on 2026-07-18 when the owner registered the four
adversarial-review round-7 plans from the same-day hostile review at tip
06a17ff31: scene-controller decomposition round 2 (7 tasks), DX12 backend
ownership decomposition (8 tasks), naming and identity debt (5 tasks), and
small findings hardening (5 tasks).

The active/future denominator returned from 25 to 18 on 2026-07-18 when
scene-controller decomposition round 2 closed S0-S6 and left the live ledger
under rule 4. Closure evidence is in
`../Reports/2026-07-18/scene-controller-round-2-closure.md`.

The denominator grew 18 → 26 on 2026-07-18 when the owner registered the
eight-task physics body-count scale campaign (persistent broadphase,
zero-cost sleepers, bandwidth diet, evidence-gated graph-colored solver
parallelism) from the same-day owner discussion of high-body-count engine
techniques.

The active/future denominator returned from 26 → 18 on 2026-07-18 when
DX12 backend ownership decomposition closed D0-D7 and left the live ledger
under rule 4. Closure evidence is in
`../Reports/2026-07-18/dx12-backend-ownership-decomposition-closure.md`.

The active/future denominator returned from 18 → 13 when naming and identity
debt closed N0-N4 and left the live ledger under rule 4. Closure evidence is in
`../Reports/2026-07-18/naming-and-identity-debt-closure.md`.

The active/future denominator returned from 13 → 8 when small-findings
hardening closed H0-H4 and left the live ledger under rule 4. Closure evidence
is in `../Reports/2026-07-18/small-findings-hardening-closure.md`.

The denominator grew 8 → 26 on 2026-07-18 when the owner registered the
18-task ImGui + Tracy development-editor campaign. It is sequenced directly
after the physics body-count scale campaign and retains the complete legacy UI
for separate, mutually exclusive Legacy/ImGui owner evaluation with atomic hot
swap support.

The active/future denominator returned from 26 → 18 on 2026-07-20 when the
physics body-count scale campaign closed P0-P7 and left the live ledger under
rule 4. Closure evidence is in
`../Reports/2026-07-20/physics-body-count-scale-closure.md`.

The denominator grew 18 → 53 on 2026-07-20 when the owner registered the
eight-plan architecture-review campaign from the same-day engine architecture
review (`../Reports/2026-07-20/engine-architecture-review.md`). The campaign
is the next active queue in plan order 1→8; E17 extended hands-on owner
acceptance remains a parked owner item that does not block it.

The denominator grew 53 → 54 on 2026-07-20 when the independent
dependency-direction closure review identified the physically moved Core
allocation policy's retained `Runtime::Allocation` namespace. The one-task
`allocation-namespace-restoration` follow-up is sequenced immediately after
dependency-direction closure and may not retain an alias or behavior change.

The active/future denominator returned 54 → 48 when
`dependency-direction-restoration` closed L0-L5 and left the ledger under rule
4. Physical direction proofs are zero; the two independently reviewed semantic
exceptions have concrete deletion conditions in
`allocation-namespace-restoration` A0 and `render-hal-modernization` M0/M5.
Closure evidence is in
`../Reports/2026-07-20/dependency-direction-restoration-closure.md`.

The active/future denominator returned 48 → 47 when
`allocation-namespace-restoration` closed A0 and left the ledger under rule 4.
The Core allocation namespace exception is resolved with zero compatibility
rows; only the Profiler semantic seam remains deletion-bound to
`render-hal-modernization` M0/M5. Closure evidence is in
`../Reports/2026-07-20/allocation-namespace-restoration-closure.md`.

The active/future denominator returned 47 → 44 when
`physics-facade-unification` closed F0-F2 and left the ledger under rule 4.
`PhysicsEngine` is the single cohesive owner; the standalone Scene facade,
aggregate read view, files, and project rows are deleted with zero compatibility
shapes. Closure evidence is in
`../Reports/2026-07-20/physics-facade-unification-closure.md`.

| Plan | Done | Tasks | Plan complete |
|---|---:|---:|---:|
| [imgui-tracy-editor-campaign](TODO/imgui-tracy-editor-campaign.md) | 17 | 18 | 94% |
| [physics-settings-snapshot](TODO/physics-settings-snapshot.md) | 2 | 4 | 50% |
| [run-execute-deaccretion](TODO/run-execute-deaccretion.md) | 0 | 3 | 0% |
| [render-graph-completion](TODO/render-graph-completion.md) | 0 | 6 | 0% |
| [render-hal-modernization](TODO/render-hal-modernization.md) | 0 | 6 | 0% |
| [gameplay-module-extraction](TODO/gameplay-module-extraction.md) | 0 | 4 | 0% |
| [replay-boundary-containment](TODO/replay-boundary-containment.md) | 0 | 3 | 0% |
| **Active/future total** | **19** | **44** | **43%** |

The denominator grew 9 → 14 on 2026-07-17 when the owner rejected S7 on the
current SIMD evidence and activated the five-task broadphase scale-attribution
campaign. B0 corrects the accounting contract: Broadphase is an inclusive
owner interval, while its nested GridBuild and bounds rows describe the same
time and must never be added to it. B1-B4 instrument mutually exclusive phases,
measure the 1,000→2,000 cliff, implement only the evidence-selected grid fix,
and close with repeated A/B, determinism, and independent review. S7 stayed
unchecked in that historical decision. The later 2026-07-17 owner direction
supersedes the pause with scalar-only S7 cleanup; it does not authorize SIMD
cutover or behavioral baseline regeneration.

B4 completed that campaign on 2026-07-17. Its exact-tip paired medians are
-1.61% Step / -7.34% inclusive Broadphase / -9.78% GridInsert at 1,000
bodies and -75.26% / -87.39% / -91.38% at 2,000. The plan is deleted under
inventory rule 4; closure evidence lives in
`../Reports/2026-07-17/broadphase-scale-closure.md`.

The denominator grew 9 → 19 on 2026-07-16 when the owner registered the
unit-test coverage campaign, then returned to 9 on 2026-07-17 when U0-U9
closed and the completed campaign left the active/future ledger under rule 4.
The tolerance-based behavioral/property suites now exist before the S7
decision and remain a binding scalar-cleanup oracle. The owner authorized
exactly S6 on 2026-07-17; completion returns the campaign to a hard pause at
7/9. The owner then rejected S7 on the current evidence and activated the
broadphase attribution campaign. After that campaign closed, the owner issued
fresh direction to execute S7 as scalar-only deletion and simplification.
S7-S8 closed on 2026-07-17; the completed plan left the active/future ledger
under rule 4. Closure evidence lives in
`../Reports/2026-07-17/soa-simd-closure.md`.

The denominator grew 8 → 9 on 2026-07-16 when the owner ruled finding R3-F1
(schedule-sensitive artifact bookkeeping nondeterminism) into scope as task
R4b per commit-contract rule 4 (rescoped plans update the ledger and
denominator in the same commit). R5 completed in parallel with that ruling;
the reconciled state was 6 done of 9 before R4b; R4b closes it at 7 of 9.

The three round-5 plans (fp-envelope-hardening 4/4,
mutual-gravity-large-scene-fallback 3/3, math-fatal-survey-restoration 3/3)
completed at 10/10 on `15th-of-July-Night-Runner` and leave the ledger as
historical work per commit-contract rule 4.

## Current Execution Priority

The 2026-07-18 adversarial-review round-7 lane is closed. Scene-controller
round 2, DX12 backend ownership, naming/identity debt, and small-findings
hardening have all left the live ledger with closure evidence.

`physics-body-count-scale-campaign` closed P0-P7 at 8/8 on 2026-07-20 and left
the live ledger. P6 remains evidence-deferred after its one-worker regression
and 4/8-worker crashes; no graph-coloring prototype is retained. P7 adds direct
520-body 0/1/4-worker byte-exact coverage, bounds every sleep-support-edge
producer behind one fail-before-grow capacity contract, and passes an 18/18
six-scene process matrix. Final `Frame/Physics` P50 versus P0 is -2.80% at 200,
-5.48% at 520, +1.52% at 1,000, +5.75% at 2,000, and -39.36% for sleepy-5,000.
The owner accepted that trade. Algorithmic and tested multithreaded determinism
are certified; cross-platform and rollback determinism are not. Full, deep
physics, performance, replay fidelity, one-minute graphics stress, and marker
smoke all pass. Closure evidence is in
`../Reports/2026-07-20/physics-body-count-scale-closure.md`.

The architecture-review campaign registered 2026-07-20 is the active queue.
Dependency-direction restoration, allocation-namespace restoration, and
physics-facade unification are closed. Remaining execution order is binding and
biggest-wins-first: 1 `physics-settings-snapshot`, 2
`run-execute-deaccretion`, 3 `render-graph-completion`, 4
`render-hal-modernization` (hard-blocked on 3), 5
`gameplay-module-extraction` (T0-T1 after 1; T2 after 3), 6
`replay-boundary-containment`. Owner decisions ratified at
registration: finish the render-graph migration (freezing rejected);
`PhysicsEngine` survives the facade unification; extracted gameplay lives in
a new top-level `SkullbonezSource/Gameplay/` module. E17 hands-on acceptance
below is a parked owner item and does not gate this campaign.

`imgui-tracy-editor-campaign` (E0-E17) is 17/18. E14-E15 are reconciled against
the accepted P1 one-process golden and their retained replay gates now pass
exactly without another engine process. E16 is complete and E17's automatable
evaluation, review, and gates are complete; its checkbox is retained only for
extended hands-on owner acceptance. The 2026-07-18 owner amendment
keeps Legacy as the development default and makes ImGui an explicit
`--dev-ui imgui` mode;
parallel/Both activation is forbidden because it creates competing focus and
input owners. E0 completed the full legacy
surface/command/frame-field/hotkey/owner inventory, captured seven current
screenshots, and ratified the development/configuration and coexistence
contract. E1 pinned Dear ImGui docking and Tracy as exact licensed gitlinks,
wired only their required sources into development configurations, proved a
fresh bootstrap path, and confirmed Release excludes them. E2 established one
compile-time development capability, separate hard-capped ImGui/Tracy
allocation owners, and a calling-thread scope that leaves the gameplay guard
active; focused and cumulative gates plus Release artifact inspection passed.
E3 now owns Tracy through an explicit manual/on-demand application lifetime,
marks only successfully presented frames, names the real composite main lane
and all 63 workers, and publishes fixed editor connection state. A pinned
external capture received 101 frame marks, no-viewer and disconnect exits were
clean, Release exclusion held, and the cumulative full gate passed with no
oracle refresh. E4 now mirrors established owner paths into focused Tracy
zones, publishes 32 bounded capacity plots, and requires explicit standard or
heavy process modes so ordinary/perf runs do not allocate Tracy's vendor
queues. A final standard capture correlated all six required owner families;
perf, full, bounded graphics stress, and platform-marker gates passed with no
oracle refresh. E5 adds the cohesive development-only ImGui context owner,
typed scalar frame and command values, deterministic DPI-aware font fallback,
versioned layout, and exact Legacy/ImGui selector with rejected simultaneous
activation. Repeated lifecycle and
resize probes plus UI, fast, perf, full, allocation, and Release-exclusion
gates passed with no oracle refresh. E6 gives that owner a concrete DX12
renderer, separate fixed 16-row development descriptor heap, synchronous font
upload retirement, two-frame resource lifetime, and explicit draw placement
before Present. Visible resize/fault probes and the renderer, one-minute
graphics stress, fast, allocation, full, and Release gates passed with zero
DX12 messages, descriptor growth, warnings, oracle changes, or golden refresh.
E7 routes native messages through the pinned Win32 backend at the `Window`
boundary, classifies mouse/keyboard/text/platform intent, keeps viewport input
authoritative, neutralizes captured engine classes, prevents held-input ghost
presses on return, and reconciles shared HWND capture/cursor state. The
Legacy/ImGui plus rejected-simultaneous matrix, final 15-message probe,
UI/UI-stress/fast/full gates,
allocation scan, and Release exclusion all passed with no oracle change.
E8 adds one domain-grouped frame view, fixed typed surface queues, deterministic
duplicate coalescing and conflict reporting, canonical owner projection, and
independent development visibility preferences. UI, tests, allocation, full,
and Release gates passed without oracle or authored-data changes. E9 now owns
deterministic layout version 2, stable dock IDs, the
editor-left/viewport-center/utility-right/replay-bottom/status-bottom
topology, the menu/toolbar shell, Tracy affordance, corrupt-layout recovery,
and byte-stable operator reset. Minimum, 16:9, and ultrawide captures plus
UI/stress/full/allocation/Release gates passed without oracle or authored-data
changes.
E10 completes the left editor workflow with typed scene/mode commands, a
fixed stable-ID hierarchy, registered asset/create controls, real session
visibility and lock behavior, and accurate history clean state. A native
select/duplicate/delete/undo/redo/load/reset matrix and the UI, DX12, bounded
stress, perf, full, allocation, and Release-exclusion gates passed without an
authored-data, baseline, or golden change.
E11 makes the center an editor-grade live DX12 viewport. One persistent
full-client sample texture and stable descriptor capture the completed world
before UI, an explicit physical-pixel letterbox maps hovered input to the
source extent, and existing world selection, gizmo, and placement overlays
remain authoritative. Native selection, asset-drop, and maximized-resize
probes ended with 18,029 captures, exactly two resource recreations, a 2/16
descriptor high-water mark, and empty stderr. UI, DX12, bounded stress, perf,
full, allocation, and Release-exclusion gates passed without an authored-data,
baseline, or golden change.
E12 adds an explicit none/single/mixed/stale contextual Inspector and one
canonical World/Simulation authoring surface. The Inspector groups Transform,
Identity, Render, Physics, Audio, and object-specific facts without taking
transform authority from E11. Five World sections route 19 typed properties
through preview-without-mutation and one release-time commit, with deterministic
Legacy/ImGui duplicate coalescing. Native authoring evidence and the UI,
physics, full, perf, allocation, and Release-exclusion gates passed without an
authored-data, baseline, or golden change.
E13 consolidates the right rail into one canonical six-section Rendering
surface, bounded Audio Authoring, and domain Diagnostics. Shared metadata owns
25 ordinary, 64 cinematic, and 8 feature rows; fixed editor views expose 13
audio globals, 16 four-band contact recipes, a 64-sample library, and bounded
physics/render/memory/worker/audio/UI facts. Native command probes, focused
reducer tests, the 10/10 comment audit, and cumulative UI/physics/full/perf,
allocation, and Release gates passed without an authored-data, baseline, or
golden change. Generic profiling remains in Tracy and E13 touched no render
backend or shader source.
E14's validated checkpoint projects one bounded compact cause summary from the
existing immutable replay publication and exposes every existing row through a
separate virtualized dockable detail window. Native compact/detail captures,
1 focused case/16 assertions, a 7/7 comment audit, UI/full/allocation/Release
proof, and the replay interaction path passed without authored-data or golden
changes. Its mandatory single replay-fidelity invocation passed launcher and
control proofs, then stopped only at the topology `199 -> 200`. P1's complete
bounded assessment accepted the coupled one-process golden, which now passes
exactly without another engine process; E14 is complete.
E15's validated checkpoint adds the complete replay-owned bottom transport,
recording control, prediction/scrub/cause workflows, recoverable typed command
path, minimum-width priority collapse, and mutually exclusive Legacy/ImGui
selection with atomic `Ctrl+0` hot swaps. Legacy remains the selected default;
an omitted selector preserves scene-authored Legacy visibility so established
captures and tests remain stable. Native default/control/popover/minimum-width
and two-direction swap captures, 5 focused cases/91 assertions, a 27/27 comment
audit, and final UI/full/allocation/Release proof passed without authored-data,
baseline, or golden changes. Its single replay-fidelity invocation passed the
launcher and 16 control cases/72 assertions before the same authorized topology
transition. P1's accepted one-process golden now passes exactly with all offline
controls, so E15 is complete.
E16 closes persistence and stress hardening with one versioned benign
preference record, deterministic stale-layout recovery, a coherent DPI-safe
editor palette, fixed stable-ID automation commands/assertions, and explicit
inactive-Legacy fences for scene stress and replay pointer input. The final
180-frame matrix passed 36 actions/23 assertions across minimum/default/
ultrawide sizes, exact 200-body scene transition, historical replay scrub,
panel churn, and eight additional atomic hot swaps. Five native captures, two
real Tracy attachments, a bounded 263-load long session, the 16/16 comment
audit, and fast/UI/UI-stress/perf/allocation/full/Release gates passed with
Legacy still the default, zero simultaneous activation, zero DX12 errors, and
no authored scene/config, baseline, or golden change. E17's retained checkpoint
records the complete Legacy disposition, six original-resolution captures, a
production same-frame `Ctrl+0` exclusivity assertion, three sequential overhead
lanes, and a 95/95 source comment audit. Independent review reopened and closed
Tracy rpmalloc backing, embedded LZ4 allocation, and late Legacy replay-render
authority defects, then found no remaining blocker. Full, UI, UI-stress, DX12,
bounded graphics-stress, unchanged-retry performance, allocation-policy,
platform-marker, and Release gates passed. The mandated single replay-fidelity
invocation reached only the already authorized topology `199 -> 200` transition;
P1 later reconciled that same one-process report. Extended hands-on feedback
remains the sole E17 hold and does not block Physics P2-P7.
The campaign adds a
development-only ImGui docked editor and Tracy
instrumentation while retaining the old UI intact for separate Legacy and
ImGui process launches. The ImGui default dock layout is editor-heavy on the left, game viewport centered,
Inspector/World/Rendering/Diagnostics plus compact Causality on the right, and
replay transport permanently docked across the bottom.

The three 2026-07-17 round-6 plans are closed: scene-controller ownership
T0-T6, monolith TU right-sizing N0-N7, and code-level red-flags C0-C6. The
final independent review and broad/DX12 stress gates passed without baseline
or golden refresh.

`physics-soa-simd-1000-bodies`
closed S0-S8: the byte-exact SoA scalar path remains; every SIMD/toggle/counter
artifact is deleted; SpatialGrid has one bounded 8,192-cell table; coverage is
mandatory; and the full gate passed without behavioral physics baseline
regeneration. Closure evidence is in
`../Reports/2026-07-17/soa-simd-closure.md`.

`physics-broadphase-scale-attribution` previously closed
B0-B4 with an unchanged 4,096-row primary table, a fixed 16,384-slot lookup,
and 4,096 cold overflow rows. Exact-tip paired medians are -1.61% Step / -7.34%
inclusive Broadphase / -9.78% GridInsert at 1,000 bodies and -75.26% / -87.39%
/ -91.38% at 2,000. The final scalar-OFF matrix is 1.0546 ms Step at 1,000
bodies and 2.0517 ms at 2,000. Complete deterministic cell coverage, the
44,401-line oracle, fixed-capacity allocation rules, and independent review are
closed. That campaign did not authorize S7, a toggle-default change, or any
baseline/golden refresh; the later owner direction uses its measurements to
simplify the fix while retaining complete 8,192-cell coverage.

The owner-paused 2026-07-16 physics SoA/SIMD campaign has completed S0-S6. Its fixed-seed
200/520/1,000/2,000-body matrix measures the scalar-AoS reference; the 1,000-
body Physics Step averages 0.9978 ms on the Threadripper 3970X and the ratified
final-cutover budget is no more than 0.80 ms. Capacity reaches 2,000 without
exhaustion; the stretch-row grid cost and pre-existing sleep-counter width
mismatch are recorded rather than silently changed. Performance, byte-exact
physics, full, and the single reference replay mega gate passed with no baseline
refresh. S1 added 20 fixed-capacity, 32-byte-aligned component arrays inside
`PhysicsBodyStore`. S2 migrated every stage, replay, presentation, diagnostics,
editor, and automation consumer to narrow hot-field spans, made the cold record
honest, and deleted the temporary bit-copy seam. All 204 tests, allocation,
full, the final 44,401-line byte-exact physics oracle, and the single one-process
replay mega gate pass with no baseline/golden refresh. S3 found and removed
20-span by-value helper copies plus unrelated full-row store traffic without
changing arithmetic or values. The final SoA-scalar 1,000-body Physics average
is 0.9795 ms, 1.8% faster than S0; performance and the 44,401-line byte-exact
physics gate pass. S4 added the default-OFF v3 config/migration, dedicated
per-file AVX2/FMA kernel, eight-lane masked integration pilot, and streaming A/B
oracle. The pilot marker is 4.5% faster; the OFF path remains 44,401 lines
byte-exact and full/performance gates pass. S5 added dark universal-gravity,
mutual-pair, and broadphase-bounds kernels with masked-tail tests, passing
chaotic-scale and focused mutual-gravity A/B oracles, and unchanged OFF-path
proof. S6 added dedicated narrowphase-prune and solver-row preparation kernels,
masked-tail tests, a full toggle-ON doctest pass, and the combined scale matrix.
At 1,000 bodies the enabled set measures 1.0666 ms against the binding 0.80 ms
budget; the scalar solver core retains only 0.0092 ms (0.86% of step), so its
follow-up trigger is not met. The owner rejected SIMD cutover on the current
evidence on 2026-07-17 and then issued fresh direction for scalar-only S7
cleanup. S7 completed the deletion at 8/9; the 0.80 ms SIMD cutover budget
neither authorized nor blocked it. S8 then closed the campaign at 9/9.

Portfolio ordering (2026-07-16 owner reorder, then 2026-07-17 scalar-cleanup
direction): `unit-test-coverage-campaign` completed U0-U9 before the S7
decision. Its tolerance-based momentum, orthonormality, bounds, grid, and
stage-contract cases are the independent oracle for deleting rejected code
without a behavioral baseline refresh. The broadphase scale-attribution
campaign is complete. S7 retained scalar SoA, deleted the measured-neutral SIMD
experiment and attribution-only production machinery, simplified the grid, and
passed the owner-requested full gate. A future SIMD campaign would need a new
plan and fresh owner approval; S7 preserves no dark path.

U5 provenance governance (2026-07-17 owner ratification): the two replay
golden provenance hashes changed only because S4's v3 SIMD-toggle config bump
changed the mechanically verified `engine.cfg` hash and its direct causal
binding. The owner retroactively ratified that content-free reconciliation and
adopted the standing `AGENTS.md` rule: config format/version bumps
automatically authorize provenance-hash-only reconciliation, recorded per
instance; payload, tolerance, or non-provenance changes still require explicit
approval.

The 2026-07-16 replay mass-reduction campaign completed R0 → R8 strictly in
order. R0–R5 closed the census and implementation work: the
ratified fingerprint, Debug probe, and Automation verifier code now link only
in their intended configurations; the final Release map has zero diagnostic
objects/symbols while product archive/restore remains present. R3 retained
distinct RVPD/V2 codecs, recorded the schedule-sensitive artifact-bookkeeping
nondeterminism as a separately ruled follow-up, and proved all gate-covered
content byte-identical across three artifacts. R4 unified bit-identical quota
and render-pose mechanics, retained policy-distinct trajectory loops, and kept
3D ribbons, screen-space UI, and packet telemetry separate. R5 deleted only
two proven zero-caller accessors and retained every live/test/migration path
under individual rulings. R4b then closed R3-F1 by canonicalizing every ruled
RVPD/RVIS topology, trajectory-store, inactive worker-bank, reserve-growth,
and derived semantic field at serialization while leaving live counters raw.
Both original R3 artifacts and two final-encoder artifacts now produce the
same 36,564,003-byte file and whole-file SHA; the gate-covered projection SHA
remains unchanged. The initial zero semantic sentinel was correctly rejected;
the owner explicitly approved one additional same-tip invocation, making
three R4b invocations total, and the final invocation passed. This exception
is closed and inherited by no other task. R6 then ruled the remaining four
oversized/near-threshold replay translation units cohesive: a moves-only split
would require a new internal API, duplicate byte/order contracts, or create a
cosmetic include fragment. R7 then closed the final census and independent
whole-campaign review after repairing the review's content-sensitive semantic-
hash and stale-governance findings. Product compilation is down one TU and
2,354 implementation lines; map-attributed bytes honestly changed +364
Release/+1,988 Profile. The original R3 mismatch pair and final encoder are
byte-exact at the same 36,564,003-byte SHA. R8 then passed tests, full,
performance, allocation-policy, and the one-process final mega gate from final
source. The plan is deleted under inventory rule 4; no active local serial step
remains. Closure evidence is in
`Agentic/Reports/2026-07-16/replay-mass-reduction-closure.md`.

The 2026-07-15 runtime mass-reduction critical path completed at 16/16:
`init-startup-decomposition → run-member-and-include-shrink →
wide-call-desc-struct-pass`. Every plan
closed with its mapped validation and no behavioral baseline, golden, or
screenshot refresh. The wide-call replay task used one engine process and one
prediction generation; after that process exposed stale pre-config-v2
provenance, explicit owner approval authorized only the config hash and its
dependent manifest hash before the CPU-only comparison tail passed.

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

Standing hazards binding every plan in this campaign: zero behavioral baseline,
golden, or screenshot refresh; move-only semantics with identical call positions,
strings, and exit codes; the Init split is a free-function file split (the
class-TU-split prohibition does not apply there, but cross-module internal
reach is banned); the Run shrink requires a single end-of-plan independent
ownership review; replay-touching wide-call work runs the one-invocation
200-box mega gate per inventory rule 11. The owner-approved 2026-07-16
wide-call exception reconciled only stale whole-config provenance and the
dependent manifest hash; all behavioral golden values remained unchanged.

| Plan | State | Verified phase count | Start condition / next action |
|---|---|---:|---|
| `init-startup-decomposition` | Complete | 5/5 | Init is a 453-line process orchestrator; four focused Startup owners, exact CLI proofs, independent ownership review, final full gate, and both manual exit-code probes are closed in `../Reports/2026-07-15/init-startup-decomposition-map.md` |
| `run-member-and-include-shrink` | Complete | 6/6 | Run.h has 23 direct includes and none of the four named heavy transitive headers; two cohesive owners, recorded UI/audio stays, clear independent review, and full/DX12/stress closure evidence are recorded in `../Reports/2026-07-15/run-member-shrink-map.md` |
| `wide-call-desc-struct-pass` | Complete | 5/5 | Ten record-construction names are 0–2 arguments; all 31 surviving ≥12-argument names have individual reasons, independent review is clear after one comment fix, and final full/replay/DX12/physics evidence is recorded in `../Reports/2026-07-15-runtime-wide-invocation-inventory.md` |

## Replay Mass Reduction Campaign (2026-07-16)

Source: the proportion finding from the 2026-07-15 hostile review, activated
by the owner and completed on 2026-07-16. `Runtime/Replay/` was 33,783 lines at tip
`c0dd7016` with three diagnosed diseases: the probe/validation harness
(ReplayValidation.cpp 3,729 lines plus fingerprint/probe-archive tooling)
compiles into production Release because only three files sit behind the
automation define; artifact-codec and presentation-emission mechanics are
duplicated across sibling owners; and ReplayPrediction.cpp (4,424 lines) is
the engine's largest remaining TU.

Standing rules binding every task: MASTER rule 11 verbatim (one mega-gate
invocation per task, one engine process, one prediction generation, zero
golden refresh, revert-on-diff — never fix forward a replay behavior
difference); no ownership re-decomposition (the monolith campaign's six
owner boundaries stand); no artifact-format or probe-output-schema changes
(the Python gate tooling parses those schemas); no line quotas — deletion
only through R5's recorded owner rulings; the automation boundary is
link-level project-configuration exclusion, never `#ifdef` scatter through
product TUs; replay reserve-allocator registrations move unchanged in owner,
phase, cap, and counter; owner-TU partitions only for R6 splits. Owner
decisions required at R0: the automation-only bucket confirmation and the
product-config probe entry-point behavior (absent vs no-op stubs). The
2026-07-16 provenance-hash protocol is precedent for any provenance-only
reconciliation.

| Plan | State | Verified phase count | Start condition / next action |
|---|---|---:|---|
| [physics-broadphase-scale-attribution](../Reports/2026-07-17/broadphase-scale-closure.md) | Complete | 5/5 | Saturated scan removed with bounded complete overflow coverage; exact-tip pairs, final matrix, comments, and independent review closed |
| [physics-soa-simd-1000-bodies](../Reports/2026-07-17/soa-simd-closure.md) | Complete | 9/9 | SoA retained; SIMD and attribution-only machinery deleted; one bounded grid, mandatory coverage, independent review, and final gates closed |
| [replay-mass-reduction](../Reports/2026-07-16/replay-mass-reduction-closure.md) | Complete | 9/9 + R8 | Product compilation is -1 TU/-2,354 lines; exact artifact SHA, clear review, and final tests/full/perf/allocation/mega gates are closed |

## Unit Test Coverage Campaign (2026-07-16, complete)

Source: the 2026-07-16 owner discussion on unit coverage. The engine has
strong system oracles over an ~4% unit layer; twice in one week the gap was
demonstrated (the ff6e780e knife-edge flip passed the byte-exact baseline;
R3-F1's artifact nondeterminism was invisible until the first byte-compare).
Owner direction: NO global percentage target — tiered per-subsystem floors
ratified from a measured baseline (defaults: Tier 1 Maths/core 85%, Tier 2
physics stores/stages/codecs/startup/config 70%, Tier 3 runtime logic owners
50%, Tier 4 render/UI/platform excluded and left to the system gates), with
the global number reported as an output only.

Standing rules: behavioral assertions only, never golden-mirror tests; fixed
seeds; `validate_tests` stays under ~60 s; coverage floors measure product
lines with versioned exclusions; floors are a quality gate, not the banned
migration-debt ratchet (U0 records that ruling); a new gate lane
(`validate_coverage` via OpenCppCoverage) ships report-only at U0 and is
armed at U9; U5's adversarial artifact decode is the campaign's single
mega-gate invocation. The campaign completed before the paused SoA/SIMD
cutover; closure evidence is in
`../Reports/2026-07-17/unit-test-coverage-closure.md`.

| Plan | State | Verified phase count | Start condition / next action |
|---|---|---:|---|
| None | No active test plan | — | Coverage campaign closed; physics remains owner-paused |

## Adversarial Review Remediation Round 6 (2026-07-17)

Source: 2026-07-17 owner-commissioned hostile full-tree review at the `main`
tip `0d77d51a4`. The owner ruled three findings into scope on 2026-07-17 and
excluded any frame-buffering change (`FRAME_COUNT` stays 2): the relocated
`SceneController` god object with its 22-parameter load surface and
`.inl`-in-class splice; surviving >2,000-line product monolith TUs
(`ReplayPrediction.cpp` 4,428, `InteractionAutomationController.cpp` 4,131,
`RenderBackendDX12.cpp` 3,636 + 1,431-line header, `ReplayRecorder.cpp`
3,492, `UI.cpp` 2,501, `PhysicsApi.cpp` 2,226); and the code-level red-flag
set (Text.cpp mutable file-scope statics, hot-path singleton resolution, the
`PhysicsScene`/`PhysicsEngine` friend edge, the 32-tick catch-up clamp, and
the per-binary LTO determinism exposure).

Standing rules binding all three plans: zero behavioral baseline, golden, or
screenshot refresh (the only narrow exception is the owner-approved catch-up
clamp path recorded in the red-flags plan); replay-touching tasks run the
one-invocation 200-box mega gate per inventory rule 11 and honor the
mass-reduction R6 cohesion rulings, which are re-litigated only with new
evidence; DX12-touching tasks carry the mandatory bounded graphics-stress
proof per inventory rule 10; every extraction is a concrete owner with typed
boundaries — mechanical TU splits, forwarding facades, and context bags are
closure failures; each plan ends with one independent review.

| Plan | State | Verified phase count | Start condition / next action |
|---|---|---:|---|
| [scene-controller ownership closure](../Reports/2026-07-17/scene-controller-ownership-closure.md) | Complete | 7/7 | Concrete scene owners and final independent ownership review closed |
| [monolith TU right-sizing closure](../Reports/2026-07-17/monolith-tu-right-sizing-census.md) | Complete | 8/8 | All named oversized TUs received owner rulings and mapped closure evidence |
| [code-level red-flags closure](../Reports/2026-07-18/code-level-red-flags-closure.md) | Complete | 7/7 | Repeat independent review, full gate, and bounded graphics stress closed |

## Adversarial Review Remediation Round 7 (2026-07-18)

Source: 2026-07-18 owner-commissioned hostile full-tree review at the
`nightrunner-17th-july` tip 06a17ff31 (post-round-6). The owner ruled four
areas into scope on 2026-07-18: `SceneController` remains a ratified
multi-domain aggregate after round 6 (~120 public methods; navigation policy,
water input, audio notify, and ragdoll queries still on the lifecycle owner;
~20 owners still flow through every load); `RenderBackendDX12` is one class
implementing seven interfaces across 9,437 lines with descriptor, readback,
graph-transient, diagnostics, and shader-development state still resident;
vestigial-identity naming (`TestScene` as the production scene type,
`GameModelRenderer` outliving `GameModel`, `RuntimeTuning`,
`Run*`/`Runtime*` prefix soup); and the minor red-flag set (the
`LockOrderValidator` singleton, `PSOKey12` pointer-as-identity, the unruled
153-site cast inventory, unfenced `nlohmann::json` reachability, two unruled
UI tab TUs).

Owner exclusions recorded 2026-07-18: the seven render consumer interfaces
are retained unchanged for future consumers — no merging, splitting, or
signature change in any round-7 plan. `FRAME_COUNT` stays 2 (round-6 ruling
carries forward).

Standing rules binding the round-7 and physics portfolio: zero behavioral baseline, golden,
screenshot, or coverage-floor refresh; replay-touching tasks run the
one-invocation 200-box mega gate per inventory rule 11; DX12-touching tasks
carry the mandatory bounded graphics-stress proof per inventory rule 10;
every extraction is a concrete owner with typed boundaries — mechanical TU
splits, forwarding facades, and context bags are closure failures; renames
must leave `SkullbonezData/` and `TestOutput/baselines/` byte-identical;
each plan ends with one independent review.

Small-findings hardening completed H0-H4 and left the active inventory under
rule 4. Closure evidence:
`../Reports/2026-07-18/small-findings-hardening-closure.md`.

## Physics Body-Count Scale Campaign (2026-07-18 — Complete 2026-07-20)

Source: the 2026-07-18 owner discussion of high-body-count engine techniques
(Bullet incremental broadphase and pair persistence, Jolt free sleepers and
thread-count-invariant determinism, Box2D v3 deterministic graph-colored
multithreaded solving), grounded in the engine's own evidence: the solver
core is 0.86% of the step (S6), the S6 SIMD experiment proved the frame is
memory-bound, `PhysicsBroadphaseStage` reinserts every body every step
(`PhysicsBroadphaseStage.cpp:357-371`), and sleeping bodies pay full
insertion/pair/prune cost (`PruneSleepPairs` filters after generation,
`PhysicsBroadphaseStage.cpp:460`).

Campaign order: P0 instrumentation + sleeping-heavy 5,000-body scene and
measurement baseline → P1 canonical pair-order determinism transition
(same-state dual-driver work-equivalence first; affected physics-baseline
transition, and replay-golden regeneration only with explicit approval)
→ P2 persistent incremental grid → P3 zero-cost sleepers → P4 hot-state
compaction/pass fusion → P5 measurement checkpoint and automatic Tier-2
evidence gate → P6 conditional deterministic graph-colored solver parallelism
(second approved transition) → P7 final matrix, comment audit, independent
review, closure.

Standing rules binding this campaign: run-to-run and worker-count
(0/1/4) byte-identical determinism at every task boundary; P1 and P6 are
the only baseline/golden-visible tasks, each governed by the plan's
Determinism Transition Protocol (same-state work membership and task-specific
ordering invariants pass before any artifact regenerates; independently
evolved runs need not match across an authorized order transition; numerical
P6 impulses need not equal serial order; physics CSVs regenerate only from the
final Debug binary; the 200-box replay golden regenerates only with explicit
per-instance owner approval, one engine process, one prediction generation per
MASTER rule 11); fixed capacity with Lane F exhaustion and zero steady-state
allocation throughout; no solver-model change, no SIMD (S7 ruling stands), no
GPU physics; the Measurement Ledger matrix
(per-marker medians over `physics_scale_200/520/1000/2000` plus the new
sleepy scene) is recorded in every implementation task's commit body with
regressions explained; B0 inclusive-marker accounting applies to all new
markers. P6 proceeds without another owner response only when the P4-tip
Profile evidence meets its plan-defined trigger. Otherwise—or if its oracle,
determinism, artifact authority, gates, or measured benefit fail—it is restored
to passing P5 behavior by reversing only P6 changes, recorded as deferred, and
execution continues to P7.
The conditional P6 slot remains part of the historical eight-task count in its
evidence-deferred outcome. P7 closed the campaign at 8/8 on 2026-07-20; the
live plan was deleted under inventory rule 4. Closure evidence:
`../Reports/2026-07-20/physics-body-count-scale-closure.md`.

## ImGui + Tracy Development Editor Campaign (2026-07-18)

Source: the owner selected Dear ImGui and Tracy for development tooling and
directed a real engine-editor layout rather than extending the crowded legacy
tab strip. The complete 18-task campaign is
[`imgui-tracy-editor-campaign`](TODO/imgui-tracy-editor-campaign.md).

Campaign order: coexistence inventory → pinned dependencies and narrow
development allocation boundary → Tracy lifecycle/instrumentation → ImGui
Win32/DX12/input foundations → shared typed command seam → deterministic dock
shell → editor-left workflow → central viewport → right-side
Inspector/World/Rendering/Diagnostics → compact Causality → bottom replay
transport → persistence/stress → separate-mode/hot-swap owner evaluation and independent
closure review.

Binding layout and migration rules: the left rail is editor-first from the top
down; the game viewport owns the center; replay controls are permanently docked
across the bottom; Causality is compact by default so Inspector and useful
world/render/diagnostic tools fit on the right. Tracy remains its external
viewer and supersedes the profiler only in the new ImGui surface. The legacy
UI remains compiled, selectable, and functionally intact for exclusive Legacy
or ImGui activation, including atomic hot swaps. Simultaneous activation is
forbidden; no legacy deletion is authorized.

| Plan | State | Verified phase count | Start condition / next action |
|---|---|---:|---|
| [imgui-tracy-editor-campaign](TODO/imgui-tracy-editor-campaign.md) | Active — E0-E16 complete; E17 automatable checkpoint complete and hands-on acceptance retained | 17/18 | Retain Legacy as the default; run separate Legacy/ImGui extended owner playtests and await acceptance |

## Architecture Review Campaign (2026-07-20)

Source: the owner-requested critical architecture review of 2026-07-20
(`../Reports/2026-07-20/engine-architecture-review.md`), registered the same
day as the next active queue. The review found the classic god objects
closed but named five structural debts: unenforced dependency direction
(Physics/Rendering/Core include Runtime), the surviving
PhysicsEngine/PhysicsScene forwarding facade, `EngineConfig` threaded into
the physics hot path, three half-finished dual architectures (physics
facade, direct-vs-graph render paths, Legacy-vs-ImGui UI), and gameplay
content (tornado) fused into Physics and Rendering.

Owner decisions ratified at registration: finish the render-graph migration
(RenderGraph owns barriers and pass scheduling; freezing as diagnostics was
rejected); `PhysicsEngine` survives the facade unification and absorbs
`PhysicsScene`; extracted gameplay content lives in a new top-level
`SkullbonezSource/Gameplay/` module; the campaign starts immediately with
E17 hands-on acceptance parked as a non-blocking owner item.

Standing rules binding every plan in this campaign: zero behavioral
baseline, golden, screenshot, replay, or physics CSV refresh — byte-exact
and image-identical oracles prove each refactor, and divergence is reverted,
never normalized; every DX12 slice runs the bounded graphics-stress proof
per inventory rule 10; replay-facing slices run the one-invocation mega gate
per inventory rule 11; one independent rubber-duck review per plan at
closure, not per slice; no new compatibility spellings, forwarding wrappers,
or hot-path inheritance/callback artifacts (existing review rules apply
verbatim). The Legacy/ImGui UI resolution is deliberately **not** part of
this campaign: it waits on the parked E17 owner verdict.

Execution order is binding (biggest wins first): plans 1-4 are the cheap
compounding tranche; plan 5 gates plan 6 and 7-T2. Replay boundary containment
inherits the now-closed solver-snapshot move.

| Plan | State | Verified phase count | Start condition / next action |
|---|---|---:|---|
| [physics-settings-snapshot](TODO/physics-settings-snapshot.md) | Active | 2/4 | Start C2 field-faithfulness and clamp tests; no blocker |
| [run-execute-deaccretion](TODO/run-execute-deaccretion.md) | Registered | 0/3 | Independent; may run parallel to plans 2-3 |
| [render-graph-completion](TODO/render-graph-completion.md) | Registered | 0/6 | Start G0 anytime; owner ruling to finish the migration is recorded |
| [render-hal-modernization](TODO/render-hal-modernization.md) | Registered | 0/6 | Hard-blocked on render-graph-completion closure |
| [gameplay-module-extraction](TODO/gameplay-module-extraction.md) | Registered | 0/4 | T0-T1 unblocked; T2 after render-graph-completion |
| [replay-boundary-containment](TODO/replay-boundary-containment.md) | Registered | 0/3 | RB0 after plan 1 L2 lands the snapshot move (soft) |

## Features

| Plan | State | Verified phase count | Start condition |
|---|---|---:|---|
| [shadow-edge-quality](../Reports/2026-07-12/shadow-edge-quality-closure.md) | Complete | 5/5 | Fixed Poisson filtering, detail-first terrain sampling, texel snapping, measured presets, and no-cascades decision complete |

Fracture replay was moved to `WNF/` by the owner on 2026-07-11 (inventory
rule 9 applies — it is not live work and is not tracked here).

## Binding Decisions And Open Decisions

Binding:

- Superseded 2026-07-20 by the owner's render-graph decision: RenderGraph
  becomes the owner of pass scheduling and barrier emission through
  [`render-graph-completion`](TODO/render-graph-completion.md). Until a
  resource class is migrated by that plan, DX12 explicit helpers remain the
  live barrier authority for it; the old freeze ruling no longer blocks the
  migration.
- Any DX12 modification requires a crash-free graphics-stress run lasting at
  least 10 seconds; `tools\run_graphics_stress.bat 1` is the standard bounded
  proof.
- No exceptions in engine code; recoverable failures must propagate rather than
  disappear.
- `Run` remains only process/frame composition after five named ownership
  extractions.
- Scene-lifetime physics ownership is promoted through the scene controller and
  surviving `PhysicsEngine`; `Run` wires it and `GameModelCollection` stops
  owning physics. (Historical ruling, satisfied. The former `PhysicsScene` was
  absorbed under
  [`physics-facade-unification`](../Reports/2026-07-20/physics-facade-unification-closure.md);
  the ownership promotion this ruling achieved is unchanged.)
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
- The completed decomposition and future spline plans executed on
  `nightrunner-14th-july` by explicit 2026-07-14 owner decision. Their
  unchanged baseline provenance and approved presentation evidence remain
  recorded in their closure reports.
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
