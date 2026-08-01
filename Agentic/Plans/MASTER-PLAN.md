# MASTER PLAN — Authoritative Remaining Work

Date: 2026-08-01
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

The denominator grew 0 → 24 on 2026-07-28 when the owner registered the
seven-plan Principal Engineer Feedback Campaign from the current-tree review at
main tip `0768593d`: physics body hot-layout evidence (4 tasks), Replay restore
and wide-signature governance (4), PhysicsFixedList copy contract (3), compact
SbResult success path (4), explicit vector dot-product API (3), deterministic
terrain fixture isolation (3), and generated dependency proofs (3). The
bounded one-task response that registered the campaign also removes the exact
Replay pure forwarder, publishes the quaternion convention in its public
header, makes orientation conversion const, gives identity constants one inline
definition, and enables `/WX` for first-party IDE builds; completed immediate
work does not inflate the live denominator.

Physics body hot-layout evidence closed at 4/4 on 2026-07-28 and left the live
ledger under rule 4. The remaining Principal Engineer Feedback Campaign is
therefore 0/20. Closure evidence is in
`../Reports/2026-07-28/physics-body-hot-layout-closure.md`.

Replay restore and wide-signature governance closed at 4/4 on 2026-07-28 and
left the live ledger under rule 4. The remaining Principal Engineer Feedback
Campaign is therefore 0/16. Closure evidence is in
`../Reports/2026-07-28/replay-restore-wide-signature-governance-closure.md`.

Physics fixed-list copy contract closed at 3/3 on 2026-07-28 and left the live
ledger under rule 4. The remaining Principal Engineer Feedback Campaign is
therefore 0/13. Closure evidence is in
`../Reports/2026-07-28/physics-fixed-list-copy-contract-closure.md`.

SbResult compact success path SR0 completed on 2026-07-28. The current type is
528 bytes, its protected 511-byte failure payload remains intact, the complete
177-definition value-flow/lifetime census found no queue or CPU-thread handoff,
and the unrefreshed performance gate passed. The live ledger is therefore
1/13 (8%). Evidence is in
`../Reports/2026-07-28/sbresult-compact-success-path-sr0-census.md`.

SbResult compact success path SR1 completed on 2026-07-28. One App-composed
256-slot immutable diagnostic store is selected; a failed 16-byte result leases
a packed slot/generation token until its last copy releases, while success
touches no store. Explicit store injection replaces the old producer/public
field API without a wrapper. The original decision recorded a 176-producer plus
30 retained/aggregate-site bound under verified no-recursion/re-entry, no
worker publication, and no result-container/queue assumptions; SR2's
multiline-aware correction below supersedes that count. Escaping diagnostics
use bounded copy-out rather than borrowed pointers. The live ledger is
therefore 2/13 (15%). Evidence is in
`../Reports/2026-07-28/sbresult-compact-success-path-sr1-decision.md`.

SbResult compact success path SR2 completed on 2026-07-28. The 16-byte leased
carrier, 159,760-byte fixed App store, exact bounded diagnostics, stale identity
checks, last-lease reclamation, explicit producer migration, and compact
`ApplicationExitState` retention now compile and pass focused tests. SR2's
multiline-aware correction changes the SR1 decision-tip census from the
reported 176+30 to 220+30. The final capacity census is 221 producer
expressions plus 29 result-member sites, conservatively 250/256 live entries
without a hidden store, result container, deferred queue, or worker handoff.
The live ledger is therefore 3/13 (23%).
Evidence is in
`../Reports/2026-07-28/sbresult-compact-success-path-sr2-implementation.md`.

SbResult compact success path SR3 completed on 2026-07-28. The final source
pins the result at 16 bytes and the 256-slot store at 159,760 bytes, preserves
the exact 511-byte payload, and passes lease, identity, allocation, DX12 epoch,
Automation, performance, full, comment, and independent-review proofs. The
final tracked-source census corrects retained members to 31; 221 publication
expressions plus 31 members conservatively bound the source at 252/256. The
completed four-task plan left the ledger under rule 4, so the remaining
Principal Engineer Feedback Campaign is 0/9 (0%). Closure evidence is in
`../Reports/2026-07-28/sbresult-compact-success-path-closure.md`.

Vector dot-product API VD0 completed on 2026-07-28. Its
Profile-preprocessed type-aware tracked-tree census found 171 true
vector-vector dot calls across 34 files: 163 production and 8 tests, including
96 byte-exact-sensitive Physics calls. Scalar, matrix, quaternion,
component-wise, and unrelated multiplications are excluded by resolved type.
VD2's configuration-complete correction below supersedes the completeness of
that historical count. The live campaign was then 1/9 (11%).

Vector dot-product API VD1 completed on 2026-07-28. One shared inline
`Math::Vector::Dot` preserves the exact established x/y/z arithmetic spelling;
170 Profile-visible call sites now name it and deletion of the OrbitalMechanics
adapter accounts for the census's remaining overload call. The vector-vector
overload and every compatibility spelling are gone. Profile compilation, 62
focused cases / 9,294 assertions, project/filter metadata, dependency
direction, and all three ownership inventories pass. The VD1 touched-source
comment audit is 34/34. The protected warm-start working-tree hunks remain
byte-identical and are excluded by the recorded partial-stage boundary. The
live campaign was then 2/9 (22%).

Vector dot-product API VD2 completed on 2026-07-28. Full Debug compilation
exposed two `_DEBUG`-only calls omitted by the VD0 Profile census, so the
configuration-complete total is 173 uses across 36 files: 172 named call-site
replacements plus the deleted adapter-body use. One shared definition and 179
calls remain; permanent mixed-sign coverage pins x-then-y-then-z evaluation.
Tests, byte-exact and deep Physics/SkullScope, performance, one-generation Replay
visual fidelity, DX12, full validation, 36/36 comment audit, and independent
review pass without baseline refresh. The completed three-task plan left the
ledger under rule 4, so the remaining Principal Engineer Feedback Campaign is
0/6 (0%). Closure evidence is in
`../Reports/2026-07-28/vector-dot-product-api-closure.md`.

Deterministic terrain fixture isolation TF1 completed on 2026-07-28. All four
shared terrain statics, the hidden terrain default, and the unused nullable
branch are deleted. All 18 affected determinism cases now use per-test
config/terrain/heap-engine invariant owners; comparison engines own independent
fixtures. The two related test cases use per-case owners, and the startup
lifecycle probe now destroys its retained-view engine before terrain/config.
Focused tests, the 437-case suite, lifecycle smoke, dependency direction, and
all ownership inventories pass. Exact formatting passes for the four touched
source files; the repository gate is blocked only by the owner's separate
warm-start files. No baseline or layout changed. The live ledger is therefore
2/6 (33%). Evidence is in
`../Reports/2026-07-28/determinism-terrain-fixture-isolation-tf1.md`.

Deterministic terrain fixture isolation TF2 completed on 2026-07-28. A
seven-run flat/deep fixture reconstruction witness covers all four predecessor
transitions and produces exact repeated hashes. Both recorded randomized
determinism seeds pass 24/24 cases, the final suite passes 438/438 cases and
2,419,221 assertions, byte-exact and deep Physics pass, and the final full gate
passes after independent-review remediation. The completed three-task plan left
the ledger under rule 4, so the remaining Principal Engineer Feedback Campaign
is 0/3 (0%). Closure evidence is in
`../Reports/2026-07-28/determinism-terrain-fixture-isolation-closure.md`.

Dependency proof generation DP0 completed on 2026-07-28. The deterministic
comparison reconciles 27 include rules, one content rule, one project rule, the
21-row Runtime table, 28 hand-written commands, 46 negative include fixtures,
two content fixtures, and one compound project fixture. It identifies the over-broad
Input proof, every Runtime regex's future-package hole, the frame-view
self-include hole, the UI prose/proof omission of Rendering, the compound
project-fixture false pass, selected-example fixture coverage, textual include
and resolver limits, path-casing platform behavior, and the closed-world App-row
wording. DP1 will extend the existing checker with one deterministic Markdown
projection and marked-block freshness check that owns the 21-row matrix and all
27 mechanical commands; only one explicitly qualitative Rendering vocabulary
search remains outside. It preserves prefix-versus-exact-file meaning, migrates
the six frame-view file exceptions out of prefix lists, and intentionally
rejects a pseudo-descendant path while retaining exact live edges. Marker
handling is fail-closed, writes preserve outside-block bytes, and Markdown
escaping is deterministic. It will not add a second checker, package branch,
edge count, or budget. The live ledger is therefore 1/3 (33%). Evidence is in
`../Reports/2026-07-28/dependency-proof-generation-dp0-comparison.md`.

Dependency proof generation DP1 completed on 2026-07-28. The existing checker
now renders, freshness-checks, and byte-preservingly rewrites one fail-closed
marked `AGENTS.md` block. That projection replaces the independently maintained
21-row Runtime table and all 27 mechanical `rg` proofs, while the sole broad
Rendering vocabulary search remains explicitly qualitative. Prefixes and exact
files are distinct columns; all six frame-view allowances are exact files and a
`RuntimeFrameViews.h/Child.h` fixture is rejected. App is visibly closed-world,
Camera's App allowance is shown at its true prefix scope, and UI prose now
rejects both Runtime and Rendering. Determinism, Markdown escaping, missing /
duplicate / reversed markers, stale/current blocks, and outside-byte
preservation are covered by focused self-tests. The dependency gate reports 27
include rules, 47 negative edge fixtures, two content fixtures, one project
fixture, and zero repository findings. The live ledger is therefore 2/3 (67%).
DP2 still owns planted rule drift, independent missing-required/Core/Tests
project cases with end-to-end XML/path discovery, bounded residual-parser
fixtures, final instructions/comment reconciliation, independent review, and
the mapped fast/full gates. Evidence is in
`../Reports/2026-07-28/dependency-proof-generation-dp1-checkpoint.md`.

Dependency proof generation DP2 completed on 2026-07-28. Planted rule drift
invalidates the prior generated block; missing, duplicate, and reversed markers
fail closed; every Runtime allow row rejects a future package; four isolated
parser cases pin the macro, continuation, quoted, angle, and local-first limits;
and exact project cases exercise required-only, missing-required,
required-plus-Core, required-plus-Tests, Git path discovery, both MSBuild item
kinds, and all four governed suffixes. Follow-up independent review is clear,
the 1/1 touched-tool comment audit is complete, and dependency, fast, and full
gates pass with 438/438 tests, 2,419,221 assertions, zero DX12 errors, and
byte-exact Physics. The completed plan left the ledger under rule 4, so the
Principal Engineer Feedback Campaign has no live plan and the active/future
ledger is empty (0%). Closure evidence is in
`../Reports/2026-07-28/dependency-proof-generation-closure.md`.

The denominator grew 0 → 28 on 2026-07-29 when the owner registered the
seven-plan Fresh-Read Engine Review Campaign from a source-only review of main
tip `90e4d52f`: broadphase canonical-order guard (2 tasks), function complexity
review trigger (3), contact solve phase ownership (5), collision hull shape
instancing (4), broadphase capacity right-sizing (4), runtime include-closure
reduction (4), and quaternion convention normalization (6). Execution order is
1→7 as listed; the ordering rationale, dependencies, and per-plan gates live in
the campaign section below. Two findings from the same review were dropped by
owner decision and are not tracked: the zero-in-source-TODO debt-visibility
question, and any repair of the documented quaternion convention short of the
full normalization registered as plan 7.

The denominator grew 28 → 35 on 2026-07-29 when the owner registered
`box-vibration-and-warm-start-integrity` (7 tasks, BV0-BV6) from the 2026-07-27/28
SkullScope investigation at tip `0768593d`. Its evidence is independent of the
fresh-read review and it carries a bounded-divergence allowance that the other
eight plans deliberately do not. The owner resolved its open sequencing question
the same day by placing it after `quaternion-convention-normalization`, so it is
listed as plan 8 of the Fresh-Read queue rather than as a separate campaign; its
owner directions and superseded ruling remain in its own section below.

Broadphase canonical-order guard closed BG0-BG1 on 2026-07-29 and left the
live ledger under rule 4. Both pair collectors derive their radix layout from
`MAX_SCENE_OBJECTS`; triangular identities and reset arithmetic are wide before
their guarded signed-int narrowing; exact cell coordinates remain full-width
while the visualization projection saturates explicitly. Unfiltered and
production-filtered ceiling-order proofs pass through body index 8,191, Physics
is byte-exact, performance is clear, and independent review closed both of its
initial blockers. The active/future ledger is now 0/33 (0%). Evidence is in
`../Reports/2026-07-29/broadphase-canonical-order-guard-closure.md`.

Function complexity review trigger closed CX0-CX2 on 2026-07-29 and left the
live ledger under rule 4. The owner ratified 400 inclusive body lines and brace
depth 6 as independent qualitative-review triggers. The current tree reports
6,285 definitions and 40/40 exact current-body rulings; self-tests fail closed
for body drift, stale/deleted rows, invalid repair-plan paths, malformed
recognized bodies, and conflicting self-test modes. Governance, mapped fast
validation, 3/3 comment audit, and independent review are clear. The
active/future ledger is now 0/30 (0%). Evidence is in
`../Reports/2026-07-29/function-complexity-review-trigger-closure.md`.

Contact solve phase ownership closed CS0-CS4 on 2026-07-29 and left the live
ledger under rule 4. One guarded transaction owns all thirteen construction,
solve, and post-solve phases; `Solve` is a 108-body-line, depth-3 sequencer.
`ParseAction` is a 21-body-line ordered dispatch across 34 exact per-action
parsers. Physics remains byte-exact, performance and full validation pass, the
9/9 comment audit is complete, and independent review answers all five
ownership questions with zero findings. The active/future ledger is now 0/25
(0%). Evidence is in
`../Reports/2026-07-29/contact-solve-phase-ownership-closure.md`.

Collision hull shape instancing closed HS0-HS3 on 2026-07-29 and left the live
ledger under rule 4. Canonical resolved-path plus exact authored-scale identity
reduces the three acceptance scenes from 911,352 to 387,504 committed hull
bytes, saving 523,848 bytes (57.4803%). Retained scene-lifetime rows preserve
stable indices through deletion, replacement, Replay clone, and editor undo;
unproved variants remain unique. The 18-run, 42,120-frame narrowphase matrix
shows no material measured timing change. Deep Physics, performance, the
single authoritative Replay visual run, and full validation pass; full reports
441/441 tests and 2,420,993 assertions. The 18/18 comment audit and independent
review close with zero blockers. The active/future ledger is now 0/21 (0%).
Evidence is in
`../Reports/2026-07-29/collision-hull-shape-instancing-closure.md`.

Broadphase capacity right-sizing BC0 completed on 2026-07-29. Debug, Profile,
and Release all measure `SpatialGrid` at exactly 8,535,792 bytes: 7,913,120
bytes targeted for registered storage plus a 622,672-byte retained inline
topology/state core. Current
committed bytes remain 8,535,792 for one grid and 17,071,584 for the live plus
Replay prediction grids at 300, 4,000, and 8,192 bodies alike. The exact
SceneLoad reservation chain and `SetCellSize`/`BeginFrame` lifetime interaction
are recorded; `overlayEntries` is correctly classified as a fixed 4,096-row
transient-work ceiling rather than a body-count formula. The active/future
ledger is now 1/21 (5%). Evidence is in
`../Reports/2026-07-29/broadphase-capacity-right-sizing-bc0-census.md`.

Broadphase capacity right-sizing BC1 now moves the two dominant arrays to
registered SceneLoad storage. Persistent entries ultimately reserve
`8 * bodies + 32`; pair dedup reserves exact triangular identity words. The
duplicated 4,096-row blanket is removed while a measured 32-row oversized-shape
spill and the compile-time ceiling remain.
Backing/high-water survive cold clears, no Replay privilege exists, and exact
phase/capacity fatal probes pass. Physics remains byte-exact across 44,401 rows
and the performance gate passes. The active/future ledger is now 2/21 (10%).

Broadphase capacity right-sizing BC2 now moves the remaining seven arrays to
registered storage. Body rows use exact admission, candidate staging uses four
rows per admitted body, and the swept overlay retains its accurately labelled
fixed 4,096-row transient ceiling. Additional live admission preserves existing
membership topology through grow-only suffix construction. Exact capacity,
retention, exhaustion, owner-census, allocation-policy, byte-exact Physics, and
unchanged idle performance checks pass. The active/future ledger is now 3/21
(14%).

Broadphase capacity right-sizing closed BC0-BC3 on 2026-07-29 and left the live
ledger under rule 4. `SpatialGrid` measures 623,256 inline bytes in Debug,
Profile, and Release. At 300 admitted bodies, exact inline plus registered
backing falls from 8,535,792 to 839,264 bytes per grid: 7,696,528 bytes saved,
a 90.167708% reduction, and 10.170568 times smaller. All nine owners report
SceneLoad-only capacity with zero Replay growth privilege; owning memory totals
include the backing exactly once. All CPU, deep Physics, performance,
allocation policy, formatting, ownership inventories, and full validation
pass. The 9/9 comment audit and independent review close with zero blockers.
The active/future ledger is now 0/17 (0%). Evidence is in
`../Reports/2026-07-29/broadphase-capacity-right-sizing-closure.md`.

Runtime include-closure reduction IC0 now records the complete 254-TU closure
inventory and reproduces the registered distribution and six heavy-header
fan-in rows exactly. IC1's first Profile compile corrected the initial
storage-owner reading: the 33 classified files are 27 concrete command/query
contract users and six value-only consumers. IC2 will break
`PhysicsEngine.h -> PhysicsWorld.h` without moving ownership or adding a hot
inner-loop pointer chase. Clean full rebuilds pass in 43.066 seconds for Debug
x64 and 44.614 seconds for Profile x64. Evidence is in
`../Reports/2026-07-29/runtime-include-closure-reduction-ic0-census.md`.

Runtime include-closure reduction IC1 now moves the six value-only consumers to
`PhysicsApi.h`. Prediction's isolated-simulation destructor is out of line at
the concrete owner so scheduling/publication units no longer need
`PhysicsEngine.h` to destroy an incomplete `unique_ptr`. The corrected focused
Profile solution and fast gate pass; fast reports 447/447 tests and 2,421,986
assertions. The 8/8 comment audit is clear. The active/future ledger is now
2/17 (12%).

Runtime include-closure reduction IC2 now breaks
`PhysicsEngine.h -> PhysicsWorld.h` through one fixed-size owner allocation and
an out-of-line destructor. The public diagnostics view is a concrete
Physics-owned value contract, not a forwarding or forward-declaration umbrella.
`UI.h`'s corrected direct census is ten tab headers: it retains the profiler
declaration dependency and removes the other nine. The focused Profile build,
allocation/dependency scans, 788/788 project/filter inventory, final fast gate,
and 7/7 IC2 comment audit pass. The active/future ledger is now 3/17 (18%).

Runtime include-closure reduction closed IC0-IC3 on 2026-07-29 and left the
live ledger under rule 4. TUs above 200 headers fall 17 to 16, the maximum
falls 255 to 248, and `PhysicsWorld.h`/`SpatialGrid.h` reach zero non-Physics
TUs. Debug and Profile rebuild samples pass at 44.860 and 45.358 seconds,
respectively, with no speedup claimed. Full, Physics, DX12, graphics-stress,
and replay-visual gates pass without baseline refresh; replay reserve accounting
now counts world debug/broadphase storage exactly once. The 30/30 comment audit
and final independent review are clear. The active/future ledger is now 0/13
(0%). Evidence is in
`../Reports/2026-07-29/runtime-include-closure-reduction-closure.md`.

Quaternion convention normalization closed QN0-QN5 on 2026-07-29 and left the
live ledger under rule 4. Textbook Hamilton multiplication, active orientation
matrices, canonical Euler composition, scene v3, replay v5, and prediction
archive v3 are live; legacy readers migrate once and losslessness is proved.
Owner visual acceptance preceded every baseline change. The complete refresh
preserves all CSV identities and finite-state/peak-energy bounds, Replay keeps
2,401 ticks and 200 causal nodes, and the accepted DX12 images retain their
dimensions. All mapped gates, the 41/41 comment audit, and independent review
are clear. The active/future ledger is now 0/7 (0%). Evidence is in
`../Reports/2026-07-29/quaternion-convention-normalization-closure.md`.

Persistent contact convergence early-out closed CE0-CE3 on 2026-07-30 and
left the live ledger under rule 4. The owner approved retaining the current
stopping criterion because the bounded per-iteration trace and controlled
object-only chain prove honest row-level non-convergence. No behavior,
threshold, iteration count, allocation policy, baseline, or golden changed.
The active/future ledger is empty (0%). Evidence is in
`../Reports/2026-07-30/persistent-contact-convergence-early-out-closure.md`.

Validation-gate integrity closed V0-V5 at 6/6 on 2026-07-30 and left the live
ledger under rule 4. The owner retired merge queues and any repository-
ownership change. Hosted CPU validation remains required on pull requests;
graphics-card validation is local-only. Exact-commit hosted run 30505659321
passed the final workflow shape. Evidence is in
`../Reports/2026-07-30/validation-gate-integrity-closure.md`.

The denominator grew 0 → 16 on 2026-07-30 when the owner registered the
four-plan Claim Integrity Campaign from a source-and-tests-only engine review at
tip `91a8403d`, conducted without reading `Agentic/` or commit history:
build-configuration parity (6 tasks, BP0-BP5), maths surface reachability
(4 tasks, MR0-MR3), inverse-trig domain guards (4 tasks, TD0-TD3), and
retirement diagnostic honesty (2 tasks, DH0-DH1). Execution order is 1→4 as
listed; the ordering rationale, dependency barriers, and per-plan gates live in
the campaign section below.

Build configuration parity closed BP0-BP5 on 2026-07-30 and left the live
ledger under rule 4. Maths Surface Reachability MR2 then registered the
four-phase Unreachable Symbol Remediation follow-up, growing the remaining
ledger from 0/10 to 0/14. Maths Surface Reachability then closed MR0-MR3 and
left the ledger under rule 4, returning the active/future total to 0/10.
Inverse-Trig Domain Guards closed TD0-TD3 and left the ledger under rule 4,
reducing the active/future total to 0/6.
Retirement Diagnostic Honesty closed DH0-DH1 and left the ledger under rule 4,
reducing the active/future total to 0/4. Unreachable Symbol Remediation then
closed UR0-UR3 and left the ledger under rule 4. The Claim Integrity Campaign
is complete and its active/future ledger is empty (0%).
All 1,640 compile rows have inherited list metadata, the 61 shared sources have
exact owner rulings for their intentional test/engine differences, production
JSON semantics reach the test binary, and every mapped gate is clear. Closure
evidence is in
`../Reports/2026-07-30/build-configuration-parity-closure.md`.

The denominator grew 0 → 16 on 2026-07-31 when the owner registered the
four-plan Gate Blind Spot Campaign from a source-and-tests-only engine review at
tip `1967a863`, conducted without reading `Agentic/`, plan files, reports, or
commit history: solver diagnostic hot-path cost (4 tasks, HP0-HP3), runtime
contract hygiene (3 tasks, CH0-CH2), engine glossary consolidation (4 tasks,
GC0-GC3), and angular impulse frame correctness (5 tasks, AI0-AI4). Execution
order is 1→4 as listed. Plan 4 is deliberately last because it is the only plan
in the campaign that stops for an owner decision; the ordering rationale,
dependency barriers, and the owner gate live in the campaign section below.

Solver Diagnostic Hot-Path Cost HP0 completed on 2026-07-31. The census found
all 16 pipeline stages produced in every configuration, corrected the
provisional record size from 44 to 56 bytes, proved that solver Replay
snapshots/hash/artifacts and prediction consume complete ordered records, and
measured the current `perf_1000` count and Profile timing. Evidence is in
`../Reports/2026-07-31/solver-diagnostic-hot-path-cost-hp0-census.md`.

Solver Diagnostic Hot-Path Cost HP1 completed on 2026-07-31. One Physics-owned
recorder now preserves exact 4,096-event saturation in count-only and
full-record modes; Replay capture, pipeline presentation, Debug SkullScope, and
default direct/prediction engines retain ordered payloads. The original
allocation-owner identity and backing remain unchanged. Focused mode coverage,
455 unit cases / 2,423,400 assertions, fast validation, ownership inventories,
and reachability pass. The active/future ledger is therefore 2/16 (13%).
Evidence is in
`../Reports/2026-07-31/solver-diagnostic-hot-path-cost-hp1-recorder.md`.

Solver Diagnostic Hot-Path Cost HP2 completed on 2026-07-31. Every producer
selects full/count execution before its row loop or through a compile-time
specialization. Count-only execution constructs no pipeline payload, omits the
diagnostic body-position loads and `sqrtf`, and submits bounded stage counts
without row-local capacity checks. Synchronized Automation/Debug/Profile
reachability, 456 unit cases / 2,424,707 assertions, fast validation, a 17/17
comment audit, and independent re-review pass. The active/future ledger is
therefore 3/16 (19%). Evidence is in
`../Reports/2026-07-31/solver-diagnostic-hot-path-cost-hp2-payload.md`.

Solver Diagnostic Hot-Path Cost HP3 completed on 2026-07-31. Physics and every
approved Replay visual/causal value remain exact without a golden refresh; the
enabled 348,925,625-byte diagnostic trace matches HP0 byte-for-byte. Allocation
policy and the two bounded 4,096-row full-record reserves remain unchanged. The
same two-pass Profile workload improves mean `Frame/Physics` by 6.71%,
persistent contacts by 33.65%, and `SolveRows` by 40.90%. Tests, Physics,
Replay visual, allocation, performance, and full validation pass. The
active/future ledger is therefore 4/16 (25%). Evidence is in
`../Reports/2026-07-31/solver-diagnostic-hot-path-cost-closure.md`.

Runtime Contract Hygiene CH0 completed on 2026-07-31. Direct
`Run::Execute` phase boundaries no longer return a failure status that a caller
can drop: their results are `void` or contain control/presentation values only,
and frame failures use the first-failure `ApplicationExitState` latch.
`RequestPhaseFailure` rejects success through Lane F, while focused coverage
proves a latched phase failure cannot resolve message exit code zero as success.
Profile compilation, the complete repository gate, a 7/7 comment audit, and
independent ownership review pass. The active/future ledger is therefore 5/16
(31%). Evidence is in
`../Reports/2026-07-31/runtime-contract-hygiene-ch0-exit-contract.md`.

Runtime Contract Hygiene CH1 completed on 2026-07-31. `Quaternion.h` now
documents only live methods and states the normalized world-axis/radians
contract for `RotateAboutAxis`; its implementation adds a Debug-only unit-axis
assert without changing Release production arithmetic. The 22-site caller
audit found every production caller valid and corrected two test helper
boundaries that admitted four non-unit fixture axes. Formatting, Debug
compilation, 28 focused cases / 2,302 assertions, a 4/4 comment audit, and
independent review pass. The active/future ledger is therefore 6/16 (38%).
Evidence is in
`../Reports/2026-07-31/runtime-contract-hygiene-ch1-quaternion.md`.

Runtime Contract Hygiene CH2 completed on 2026-07-31. `PhysicsFixedList`
requires nothrow-move-constructible elements, retains the trivial byte-transfer
path, and relocates non-trivial rows without an unwind branch. Every live
instantiation compiles under the contract; focused non-trivial lifetime
coverage, 457 unit cases / 2,424,712 assertions, byte-exact Physics, allocation
policy, the 652.3-second full gate, a 2/2 comment audit, and independent review
pass. Engine source now contains zero throw expressions. The active/future
ledger is therefore 7/16 (44%). Evidence is in
`../Reports/2026-07-31/runtime-contract-hygiene-closure.md`.

Engine Glossary Consolidation GC0 completed on 2026-07-31. The corrected
tracked source-bearing scope is 575 files: the provisional 576 included
`SkullbonezSource/AGENTS.md`. The complete census records 2,172 definitions for
1,285 unique terms, classifies 321 multi-file terms as shared and 964
single-file terms as local, and adjudicates canonical wording for all 264
shared terms whose copies have drifted. The explicit checklist contains 575
unique unchecked rows. This phase is documentation-only. The active/future
ledger is therefore 8/16 (50%). Evidence is in
`../Reports/2026-07-31/engine-glossary-consolidation-gc0-inventory.md`.

Engine Glossary Consolidation GC1 completed on 2026-07-31. The shared glossary
reproduces all 321 GC0 owner adjudications exactly. The repeatable inventory
reproduces the 575-file / 2,172-definition / 1,285-term census, reports 321
multi-file terms and 264 wording drifts, and matches 321 exact current
file/line/wording migration rulings with zero diagnostics. Comment and reviewer
rules now enforce the shared/local split and non-tautological summaries;
`validate_fast` runs the self-test and strict scan. The complete 457-case /
2,424,712-assertion gate, direct inventory checks, 2/2 tool comment audit, and
independent re-review pass. The active/future ledger is therefore 9/16 (56%).
Evidence is in
`../Reports/2026-07-31/engine-glossary-consolidation-gc1-standard.md`.

Engine Glossary Consolidation GC2 completed on 2026-07-31. All 1,208 source
definition sites for the 321 shared terms are removed, all 964 local term/file
pairs remain exact, and all 447 affected files cite the canonical glossary.
The complete whitespace-normalized audit corrects the provisional filler count
from 77 to 107 and removes every instance. The strict inventory now reports 964
unique local definitions with zero multi-file terms, drift, rulings, or
diagnostics. All 575 non-comment source suffixes are byte-identical to GC1; the
checklist is 458 checked and 117 explicitly deferred to GC3. Automation refresh,
the 457-case / 2,424,712-assertion fast gate, direct inventories, and independent
review pass. The active/future ledger is therefore 10/16 (63%). Evidence is in
`../Reports/2026-07-31/engine-glossary-consolidation-gc2-pass.md`.

Engine Glossary Consolidation GC3 completed on 2026-07-31 and closes the plan
at 4/4. All 117 conservative basename-led candidates are adjudicated: 83 retain
their informative clause without the filename subject, and 34 templated UI
headers now state concrete bounds, input, retained-state, preview/commit, style,
cache, or drawing ownership. Zero basename-led summaries remain across all 575
tracked source files. The checklist is 575/575 checked with zero deferred; all
117 non-comment suffixes are unchanged; strict glossary/path inventories, the
457-case / 2,424,712-assertion fast gate, post-gate direct proofs, and
independent closure review pass. The active/future ledger is therefore 11/16
(69%). Evidence is in
`../Reports/2026-07-31/engine-glossary-consolidation-closure.md`.

Angular Impulse Frame Correctness AI0 completed on 2026-07-31. The three
torque-to-angular-velocity paths and every direct caller are censused, the
application point is ruled a world-space center-relative offset, and a focused
expected-failure test records the rotated anisotropic mismatch against the
contact solver. The pre-change oracle predicts zero committed artifact bytes:
all mapped authored impulses are isotropic spheres, generated anisotropic boxes
receive their pending impulse at identity, and the launcher interaction targets
a sphere. The active/future ledger is therefore 12/16 (75%). Evidence is in
`../Reports/2026-07-31/angular-impulse-frame-correctness-ai0-census.md`.

Angular Impulse Frame Correctness AI1 completed on 2026-07-31. Mutual-gravity
workers now emit dense canonical prefixes into disjoint slices; the main thread
compacts them in chunk order and reduces only the active linear list. The
40-body fixture creates cross-chunk receiver gaps and remains bit-exact at zero,
one, and four workers; the unchanged 520-body fallback also passes. Across 660
samples of the same 200-body Profile scene, Reduce mean improved from 0.093166
ms to 0.079943 ms and median from 0.0924 ms to 0.0780 ms. The active/future
ledger is therefore 13/16 (81%). Evidence is in
`../Reports/2026-07-31/angular-impulse-frame-correctness-ai1-gravity-reduce.md`.

The owner halted AI2 on 2026-07-31 to restore the last approved physics oracle;
that restoration remains authoritative. Owner direction on 2026-08-01 resumed
AI2 and removed the proposed extra `at_rest` all-asleep-frame prerequisite
because the existing deep lane already hashes the complete CSV byte-for-byte.
The generated 7,649,427-byte / 54,001-line artifact retains committed SHA-256
`0a46651405e181428aabb5cc5081bd0d90ac6ca73e3a0c2786353f00cf55a984`.

Angular Impulse Frame Correctness AI2 completed on 2026-08-01. One shared
inertia-frame helper now serves pending gameplay, world-force, and contact
impulses while preserving each path's diagonal operation and the historical
world-force order. The rotated anisotropic-box test is green and the rotated
isotropic-sphere path is component-exact. Public and retained-state vocabulary
now exposes the application value as a world-space center-relative offset.
Unit, core Physics, and deep Physics
gates pass with no baseline refresh. The active/future ledger is therefore
14/16 (88%). Evidence is in
`../Reports/2026-07-31/angular-impulse-frame-correctness-ai2-impulse.md`.

Angular Impulse Frame Correctness AI3 completed on 2026-08-01 without changing
source or artifacts. The sweep found the anisotropic angular-drag clamp mixes
world components with body-axis inertia, the authored `forcePosition` schema
contains one absolute-position outlier, and test-only `VectorReflect` leaves an
axis-versus-plane convention ambiguous. It also confirmed all current
`PhysicsApi.h` descriptor consumers agree on their frames, although the public
fields do not state that matrix. AI3 registered the five-phase Vector Frame
Contract Closure follow-up, growing the denominator 16 -> 21; the active/future
ledger is therefore 15/21 (71%). Evidence is in
`../Reports/2026-07-31/angular-impulse-frame-correctness-ai3-convention-sweep.md`.

Angular Impulse Frame Correctness AI4 completed with explicit owner acceptance
on 2026-08-01. Unit, core Physics, deep Physics, performance, DX12 screenshot,
Replay visual/causal, strict ownership-inventory, and full-composition evidence
support AI0's zero-byte prediction without changing a committed baseline. The
accepted packet discloses the known CRLF/LF Replay-manifest provenance
limitation and the shared-helper test-isolation caveat; independent review found
no blocker. The post-acceptance rerun passed 453 cases / 2,422,921 assertions,
core and deep Physics exactness, an idle-host performance comparison, all
accepted offline Replay controls against the existing single generation, and
the 494.8-second full gate. The active/future ledger is therefore 16/21 (76%).
Evidence is in
`../Reports/2026-07-31/angular-impulse-frame-correctness-closure.md`.

Vector Frame Contract Closure VF0 completed on 2026-08-01. Every public
Physics vector/quaternion/shape field now states its frame, rotated query and
point-joint tests reject plausible wrong-frame inputs, and Ragdoll's
anisotropic inverse-inertia path shares the AI2 conversion without changing
its isotropic branch. The pre-change VF1-VF3 artifact prediction is zero bytes:
mapped scenes have zero effective angular-drag density, the authored offset
outlier belongs only to unmapped `ragdoll_playground`, and `VectorReflect` has
no production caller. The active/future ledger is therefore 17/21 (81%).
Evidence is in
`../Reports/2026-07-31/vector-frame-contract-closure-vf0-frame-matrix.md`.

Vector Frame Contract Closure VF1 completed on 2026-08-01. General angular
drag now clamps torque and velocity in body-principal axes for rotated
anisotropic records, returns changed torque to world space, and preserves the
original world value when no clamp activates. The defect oracle fails the old
mixed-frame result and passes the correction; an actively saturated isotropic
sphere remains component-exact to the previous arithmetic. Core and deep
Physics artifacts remain exact without a golden refresh, and the complete fast
gate passes 457 cases / 2,422,977 assertions. The active/future ledger is
therefore 18/21 (86%). Evidence is in
`../Reports/2026-07-31/vector-frame-contract-closure-vf1-angular-drag.md`.

Vector Frame Contract Closure VF2 completed on 2026-08-01. The authored ball
schema now carries the explicit `impulseWorldOffsetFromCenter` name through all
56 values in 23 schema-v4 scenes, parsed storage, and runtime setup. The
version-gated legacy parser accepts the historical key for versions 1-3, the
cold tool migrates it deterministically, and v4 rejects it rather than hiding
a compatibility alias. `ragdoll_playground`'s
`wake_ball` absolute-position outlier converts to the correct zero
center-relative offset, and a real `PhysicsEngine` handoff test pins the queued
world impulse and center offset. The complete unit suite, core/deep Physics,
and strict ownership/reachability checks pass with no golden refresh. The
active/future ledger is therefore 19/21 (90%). Evidence is in
`../Reports/2026-07-31/vector-frame-contract-closure-vf2-authored-impulse-offset.md`.

Vector Frame Contract Closure VF3 completed on 2026-08-01. `VectorReflect`
now implements the conventional surface-plane formula
`incident - 2 * dot(normal, incident) * normal`: tangent components remain
unchanged and the normal component reverses. One focused case pins both oblique
and normal incidence. The zero-production-caller census remains true, and core
plus deep Physics artifacts are exact without a golden refresh. The
active/future ledger is therefore 20/21 (95%). Evidence is in
`../Reports/2026-07-31/vector-frame-contract-closure-vf3-vector-reflect.md`.

Vector Frame Contract Closure VF4 completed on 2026-08-01. Final focused tests
pass 9 cases / 653 assertions; the complete suite passes 460 cases / 2,423,070
assertions. Core/deep Physics remain exact, performance passes without refresh,
the accepted existing-generation Replay controls pass, and the 560.3-second
full gate reports matching DX12 captures and byte-exact Physics. The aggregate
comment audit is 18/18 and independent closure review is clear. The Gate Blind
Spot Campaign completed 21/21 and left the active/future ledger under rule 4;
the ledger is empty (0%). Evidence is in
`../Reports/2026-07-31/vector-frame-contract-closure.md`.

The denominator grew 0 -> 7 on 2026-08-01 when the owner registered Contact
Energy And Warm-Start Integrity. The plan retains the working terrain support
seed, installs complete-solve and scene-level energy invariants before changing
behavior, isolates object-contact restitution from any conditional identity
repair, and requires a 64-level tower plus the existing 200-box topple. The live
ledger is 0/7 (0%); no baseline-refresh authority is granted. The agent must run
all seven implementation phases to completion and prepare exact candidate
artifacts before asking the owner the sole terminal question: whether those
reviewed candidates may replace the tracked baselines.

The denominator grew 7 -> 14 on 2026-08-01 when the owner registered Look Lab
Random Style Authoring and placed it first in execution order. F10 rerolls one
deterministic coherent presentation-only look; F11 saves the fully resolved
schema-current style, human-readable receipt, and matching screenshot inside one
ignored root `LookLab/<datetime>_<seed>/` directory. F5/F6 remain the profiler
diagnostics, camera/lenses are excluded, and the existing scene, simulation,
shader source, resource-quality policy, and validation baselines remain
untouched. The live ledger is 0/14 (0%) across two plans.

The SoA/SIMD scale campaign is complete. Completed historical campaigns are
excluded under commit-contract rule 4. Scene-controller ownership closed at
7/7 and monolith TU right-sizing closed at 8/8 on 2026-07-18; both left the
live ledger under rule 4. Their closure evidence is in
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

The active/future denominator returned 44 → 40 when
`physics-settings-snapshot` closed C0-C3 and left the ledger under rule 4.
The final 27-field Physics-owned value has one cold Core stamp, no fixed-step
Core-config edge, exact default/custom/Boolean provenance tests, unchanged
single-site clamps, and clear independent review. Closure evidence is in
`../Reports/2026-07-20/physics-settings-snapshot-closure.md`.

The active/future denominator returned 40 → 37 when
`run-execute-deaccretion` closed X0-X2 and left the ledger under rule 4. Run
now sequences one scene proceed policy and one concrete operator-editor
composer call; the complete logical ownership review is clear. Closure evidence
is in `../Reports/2026-07-20/run-execute-deaccretion-closure.md`.

The active/future denominator returned 37 → 31 when
`render-graph-completion` closed G0-G5 and left the ledger under rule 4. One
live graph now owns world/UI callback order and compiled resource transitions;
normal frames retain only the declaration-only Present edge, while capture
restart uses an explicit zero-declaration completion. Closure evidence is in
`../Reports/2026-07-20/render-graph-completion-closure.md`.

The active/future denominator returned 31 → 25 when
`render-hal-modernization` closed M0-M5 and left the ledger under rule 4.
Declared raster values now own PSO identity, the DXR facet is typed, and the
Profiler semantic exception is resolved through a fixed Rendering-owned timing
owner and Core-owned value history. Closure evidence is in
`../Reports/2026-07-21/render-hal-modernization-closure.md`.

The active/future denominator returned 18 → 0 on 2026-07-21 when the owner
accepted ImGui/Tracy E17 as ready for extended hands-on use. The campaign
closed E0-E17 at 18/18 and left the ledger under rule 4. Legacy remains the
development default; no retirement or default switch was authorized. Closure
evidence is in
`../Reports/2026-07-21/imgui-tracy-editor-campaign-closure.md`.

The denominator grew 0 → 19 on 2026-07-22 when the owner registered the
three-plan architecture follow-up campaign from the 2026-07-22 architecture
review conversation: render interface retirement (6 tasks), owner fan-out
reduction (6 tasks), and replay subsystem consolidation (7 tasks). Execution
order is 1→3 as listed; the ordering rationale and per-plan gates live in the
campaign section below.

The active/future denominator returned 19 → 13 when
`render-interface-retirement` closed RH0-RH5 and left the ledger under rule 4.
All ten render interfaces and five test implementations are gone; concrete DX12
owners remain non-polymorphic, transient consumer authority is narrower than
the RH0 baseline, and the final CPU/full/three-repeat renderer/stress evidence
is recorded in
`../Reports/2026-07-22/render-interface-retirement-closure.md`.

The denominator grew 0 → 24 on 2026-07-22 when the owner registered the
five-plan architecture follow-up round-2 campaign from the same-day
architecture review conversation (chat review at main tip 0c5263e1; the plan
documents carry the dated evidence): physics standalone-world unification
(5 tasks), Run::Execute frame-phase decomposition (4 tasks), RuntimeRenderer
decomposition (6 tasks), replay deduplication audit (4 tasks), and wide
signature reduction (5 tasks). Execution order is 1→5 as listed; the ordering
rationale and per-plan gates live in the campaign section below.

The active/future denominator returned 24 → 19 when
`physics-standalone-world-unification` closed PU0-PU4 and left the ledger under
rule 4. `PhysicsEngine` is the only simulation owner, the engine lifecycle smoke
pins full point-joint/query/contact/island state, and the independent review and
final broad gate are clear. Evidence is recorded in
`../Reports/2026-07-22/physics-standalone-world-unification-closure.md`.

The active/future denominator returned 19 → 15 when
`run-execute-frame-phase-decomposition` closed RX0-RX3 and left the ledger
under rule 4. `Run::Execute` is a 74-line phase schedule, the sensitive frame
order and restart edges are pinned, and the independent ownership review and
final broad gate are clear. Evidence is recorded in
`../Reports/2026-07-22/run-execute-frame-phase-decomposition-closure.md`.

The active/future denominator returned 15 to 9 when
`runtime-renderer-decomposition` closed RR0-RR5 and left the ledger under
rule 4. `RuntimeRenderer` now owns pass instances and frame-graph order behind
a cohesive backend-resource lifecycle owner; UI text, Replay consequence
grading, and scene/render frame publication have explicit boundaries. The
46-to-26 member, 10-to-3 constructor, and 2-to-0 wide-method reductions are
independently verified, and the final full, repeated DX12, and stress gates are
clear. Evidence is recorded in
`../Reports/2026-07-22/runtime-renderer-decomposition-closure.md`.

`replay-deduplication-audit` completed RD0 on 2026-07-22. The exhaustive
64-file census identifies seven owner-decision candidates: five direct copied
Prediction/Presentation mechanics and two overlapping packet values with
separate-lifetime evidence. Prior codec/hash and Validation placement rulings
remain binding where the audit found no new evidence. RD1 awaits owner rulings;
evidence is in
`../Reports/2026-07-22/replay-deduplication-rd0-census.md`.

The owner completed RD1 on 2026-07-22: C1-C5 are `dedup-now`, C6-C7 are
`cohesion-retain`, and no candidate is deferred. Implementation is authorized
on the condition that every Replay doctest, focused v2 artifact gate, strict
allocation gate, authoritative visual-fidelity oracle, and final broad gate
pass before closure.

`replay-deduplication-audit` closed RD0-RD3 on 2026-07-22 and left the live
ledger under rule 4. C1-C5 now have one owning implementation or canonical
composed value, while C6-C7 retain their ruled separate lifetimes. The final
independent review is clear; every owner-conditioned Replay lane, physics, and
the broad gate pass without a golden, baseline, schema, config, or reserve
inventory change. Closure evidence is recorded in
`../Reports/2026-07-22/replay-deduplication-closure.md`.

The active/future denominator returned 5 → 0 when
`wide-signature-reduction` closed W0-W4 and left the ledger under rule 4. The
repeatable threshold-7 inventory falls 301 → 285, all 16 ruled defects are
replaced by bounded domain values or typed policy, every survivor retains an
owner ruling, and the independent no-bag review and final broad gate are clear.
Evidence is recorded in
`../Reports/2026-07-23/wide-signature-reduction-closure.md`.

The active/future denominator returned 5 → 0 when
`wide-signature-decomposition-round-2` closed D0-D4 and left the ledger under
rule 4. All eight reopened threshold-16 rows were removed through real action,
lifecycle, and value-lifetime boundaries. The final threshold-16 inventory is
empty, mutable subsystem owners remain explicit, and independent no-bag,
allocation, dependency, Replay, and broad validation are clear. Evidence is
recorded in
`../Reports/2026-07-23/wide-signature-decomposition-round-2-closure.md`.

The denominator grew 0 → 6 on 2026-07-23 when the owner requested the same
decomposition treatment for every current 13-, 14-, and 15-parameter function,
while explicitly accepting 12 and below. `wide-signature-decomposition-round-3`
owns nine rows across UI, Gameplay, Physics, Rendering, and Replay. T0 found a
bounded owner-safe treatment for every row; none is blocked or deferred.
T1 replaces both Scene-tab rows with one-turn layout/gesture values and
`EmitFxQuad` with reference-based corners plus scalar style/terrain values. The
threshold-13 inventory is down from nine to six. T2 replaces the private
physics wake constructor's controller-owned row fan-out with a private
capability while external physics owners remain explicit. The inventory is
down to five. T3-T5 subsequently closed the threshold-13 inventory and the
round-3 report records the original closure evidence.

The denominator grew 0 to 6 again on 2026-07-23 when the owner rejected the
campaign's mechanical parameter objects. The reopened audit found input,
request, descriptor, and context values that existed mainly to conceal
arguments and were immediately unpacked or forwarded.
`wide-signature-parameter-bag-remediation` removes those shapes through
real operation decomposition while preserving the accepted 12-parameter
ceiling. B0 records the complete campaign-introduced census and explicit
retain/remove rulings. B1 removes the broad UI input record, gives Scene-tab
controls to the Scene-tab owner, removes both Scene frame packets, and emits
tornado vertices without quad call packs. B2 removes the sleep-row constructor
pack, all DX12 texture/mesh creation descriptors, `RenderModelPassInput`, and
the shadow selection packet. Main/reflection visibility and object-shadow
  submission are structural choices. B3 splits cause-tree surface/activation,
  velocity gizmo/target picking, loaded-artifact reset/arming, and the two scrub
  gestures. `ReplayWorkspaceFrameInput` remains only at the top-level workspace
  transaction and is no longer forwarded. B4 removes the Replay prediction
  frame request, the additionally discovered prediction-job descriptor, the
  render-preparation packet, and the restore-diagnostic input packet. Prediction
  and render work now advance through explicit ordered owner phases, while
  restore producers construct the real emitted diagnostic. B5 reconstructs the
  campaign history, removes six additional missed descriptors plus
  `RuntimeRenderInputs` and `RuntimeRenderServices`, and closes with an empty
  threshold-13 inventory, exact 55/55 comment audit, hostile no-bag approval,
  and all mapped gates clear. The denominator returns 6 to 0; evidence is in
  `../Reports/2026-07-23/wide-signature-parameter-bag-remediation-closure.md`.

The denominator grew 6 → 22 on 2026-07-23 when the owner registered the
three-plan architecture follow-up round-3 campaign from the same-day
from-source engine architecture review of `nightrunner-22nd-JUL-26` (review
conducted without plan files or git history; the plan documents carry the
dated evidence): source blemish remediation (6 tasks), UI/Runtime separation
(5 tasks), and Runtime package decomposition (5 tasks). Execution order is
1→3 as listed and begins only after
`wide-signature-parameter-bag-remediation` closes; the ordering rationale,
owner exclusions (Replay frozen, parameter seams excluded, engine/game
content boundary untouched), and per-plan gates live in the campaign section
below.

The active/future denominator returned 13 → 7 when
`owner-fanout-reduction` closed OF0-OF5 and left the ledger under rule 4.
Scene-load inputs are 18→10, consumer outputs are 20→11, view-parameter slots
are 68→36, a reactive owner integrates in at most three files, and the final
independent ownership review and broad/DX12-stress gates are clear. Evidence is
recorded in
`../Reports/2026-07-22/owner-fanout-reduction-of5-closure-census.md`.

The active/future denominator returned 7 → 0 when
`replay-subsystem-consolidation` closed RC0-RC6 and left the ledger under rule
4. Replay is assigned across six named domains, production include edges fell
48 → 33 with explicit survivors, the unchanged three-owner reserve inventory
passes the strict two-generation gate, and the final independent review and
broad gate are clear. The visual-fidelity config-provenance mismatch was later
resolved through an authorized two-field provenance reconciliation, and the
full 2,401-tick oracle passed without behavioral golden changes. Evidence is in
`../Reports/2026-07-22/replay-subsystem-consolidation-closure.md` and
`../Reports/2026-07-22/replay-visual-fidelity-provenance-reconciliation.md`.

The active/future denominator returned 22 → 18 when
`replay-policy-debt-closure` closed RP0-RP3 and left the ledger under rule 4.
Thread-local allocation attribution and a strict two-generation gate now prove
zero gameplay/reserve-policy violations; Replay, Physics, Rendering, and
Runtime share `PhysicsSceneObjectId`; the duplicate identities and dormant
facades are gone; and cumulative allocation, physics, performance, full, replay,
comment, and independent-review evidence is recorded in
`../Reports/2026-07-21/replay-policy-debt-closure.md`.

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

The denominator grew 0 → 7 on 2026-07-25 when the owner registered
`ui-renderer-hard-boundary`. UR0-UR6 preserve the completed UI/Runtime
value-command separation while deleting the remaining UI-to-Rendering backend
edges, moving GPU submission and resource lifetime into Runtime/Render,
removing operator-presentation policy from Rendering, establishing a
standalone UI build target, and adding a mandatory directional dependency
gate. UR6 closed at 7/7 after the owner approved the two attributed
performance baselines and the complete performance gate passed; inventory rule
4 removed those seven tasks from the live ledger. Permanent evidence is in
[`ui-renderer-hard-boundary-closure`](../Reports/2026-07-25/ui-renderer-hard-boundary-closure.md).

The denominator grew 7 → 19 on 2026-07-25 when the owner registered the
architecture follow-up round-4 campaign from the same-day engine architecture
review (chat review at main tip `c670e95f`; the plan documents carry the
dated evidence): replay subsystem partition (6 tasks) and downward domain
bleed remediation (6 tasks). The same registration amended
`ui-renderer-hard-boundary` with draw-stream fingerprint gating, baked
font-metric single sourcing with exact parity, measured capacity high-water,
preview-fallback contract placement, and consolidation of the standing
shell-snippet dependency proofs into the UR5 validator; its task count is
unchanged at 7. UR0 then ratified the exact 25-row/14-file census, assigned
every edge a destination owner and deletion condition, mapped the reverse
Runtime/Render composition path, and recorded the expected final comment-audit
scope. UR1 then established the bounded read-only draw stream, immutable
baked-font metrics, preview fallback values, measured capacity evidence, and
renderer-free CPU coverage. The denominator grew 19 → 24 on 2026-07-25 when
the owner registered `invariant-ownership-governance-and-transaction-repair`
(5 tasks) as the campaign's fourth entry: the review found that four sound
bans (context bags, owner reach-back, callback packs, the 12-parameter
ceiling) intersect to extrude wide transactions into N sibling structs plus
wide apply free functions plus ordering comments, leaving the correctness
invariant owned by nobody. GV0 amends the governance to test for invariant
ownership rather than shape, and GV1-GV4 repair the scene-load transaction
and every other censused offender. UR2 then converted Legacy UI construction,
cached replay, Runtime badges, and Replay overlays to one record-only value
flow with committed semantic fingerprints and Runtime-owned fixed scratch
streams. UR3 then moved draw translation, preview GPU lifetime, frame-local
handle resolution, timing, and draw attribution into a concrete Runtime/Render
owner while deleting the UI render context and raw preview handles. UR4 then
moved profiler layout into UI recording, projected renderer diagnostics at the
Runtime/UI boundary, and removed the final UI-to-Rendering source edge. UR5
then established the standalone UI production library, exact single-project
UI ownership, and the mandatory data-driven dependency graph gate shared by
the follow-up plans.

The denominator grew 24 → 27 on 2026-07-25 when the owner registered
`header-claim-staleness-remediation` (3 tasks) from the same-day header
comment staleness audit
(`../Reports/2026-07-25/header-comment-staleness-audit.md`). The audit was
triggered by a near-miss: the architecture review reported a false
"half-finished render graph" finding because `RenderGraph.h` still described
the pre-`render-graph-completion` design, nearly registering a six-task
campaign to build what shipped on 2026-07-20. The header was corrected in
`d0e2c14f`. The audit then found a phantom `RunInput` owner named in 14
comments across 9 files despite having zero declarations anywhere, three more
falsified ownership claims, and 12 broken `Related:` pointers. It also
established that the comment regime is strong at deletion (zero retired
vocabulary survives) and blind to responsibility movement. This plan is
sequenced early — before the replay partition — because RS0 and GV2 censuses
read the exact files carrying false claims. HC0-HC2 then corrected 18
false-claim sites, repaired 21 dead pointers, and installed claim-verification
governance plus a mechanical `Related:` resolver. The completed plan is removed
from the live inventory under ledger rule 4. Replay RS0 ratified all 72 files,
188 internal include edges, 35 external include sites, 26 named upward-edge
resolutions, and the three-owner reserve inventory. RS1 then moved all 18
prediction files into `Runtime/Prediction`, updated every live
path/project/filter, and passed the mapped plus cumulative Runtime gates
without refresh. RS2 then moved all eight planning files into
`Runtime/Planning` with exact production/test project ownership and the same
zero-refresh gate result. RS3 then reconciled sibling composition and the
three-owner reserve inventory without introducing an upward escape. RS4
installed the standing table, placement rule, and full dependency-fixture
matrix. With GV0, the active/future ledger was 6/17. The denominator then grew
17 -> 25 on 2026-07-26 when the owner registered
`concrete-parameter-bag-elimination` (8 tasks) from the complete current-tree
bag audit. The plan registered 22 repair-required shapes: the original
`SceneSaveRequest`, 19 established additional shapes, and two Replay-restore
contexts present after the Replay partition work. It requires concrete
owner-produced values, focused direct operations, or concrete phase-checked
transactions and explicitly bans inheritance, interfaces, virtual dispatch,
callbacks, type erasure, and renamed service/context bags. The active/future
ledger was 6/25. Replay RS5 then closed final hidden cross-package contracts,
moved retained Prediction presentation authority out of App, passed the
complete census, comment audit, independent review, and mapped gates, and
completed Replay Subsystem Partition at 6/6. The ledger reached 7/25 before
the completed six-task plan left the active/future inventory under rule 4.
Downward Domain Bleed DB0 then ratified the three registered bleed classes,
added nine unused Physics-to-Assets include rows to DB2, fixed the exact
retained-geometry and terrain value contracts, and recorded the B2/B3
byte-exact strategy. DB1 then moved the retained-record meaning, capacity, and
continuation policy into Runtime/Prediction while leaving Rendering with a
generic value contract; the approved shader tree and every visual baseline
remained unchanged. The live ledger was 3/19. DB2 then replaced per-body World
terrain pointers with one Physics-owned scene view, moved support
classification into Physics, removed all Physics-to-World/Scene/Assets
includes, and passed the byte-exact 44,401-row physics oracle plus unit, perf,
and dependency gates. DB3 then moved all five per-body fluid/support facts from
`PhysicsBodyRecord` into a fixed-capacity `BuoyancySystem` store aligned with
body/collider rows. The final source preserves the 44,401-row oracle
byte-for-byte and passes unit, perf, allocation, and dependency gates. DB4 then
extended the data-driven dependency validator with the complete Physics upward
edge ban and exact retired Rendering trajectory-name deletion, including
planted negative fixtures. DB5 then repeated the complete census and 89-file
comment audit, remediated both hostile-review findings, and passed the final
full gate with the physics oracle byte-exact. The ledger reached 7/19 before
the completed six-task plan left the active/future inventory under rule 4,
leaving 1/13 (8%).
GV1 then ruled the complete current-tip census: 201 wide operation
definitions, 22 suffix-family definitions, every ordering/arbitration hit,
all 22 companion-plan targets, and all four pre-ruled non-offenders. Scene
load remains GV2's repair; generated-scene control rebuild ordering is the
single additional GV3 row. Three 13-parameter render/UI operations are
assigned to PB0. The live ledger is now 2/13 (15%).
GV2 then installed the concrete `SceneLoadTransaction`: a private detached
request/output record plus an exhaustive phase cursor now owns Load ->
RuntimeReactions -> Presentation and mid-batch value arbitration. All former
call sites compile through the transaction, the four free helpers and public
output bag are deleted, and the full gate passes without baseline motion. The
live ledger is now 3/13 (23%).
GV3 then repaired the census's sole additional invariant-shaped row. The
non-copyable `SceneGeneratedControlTransaction` now owns the generated-scene
walk DrainAndReset -> Repopulate -> PublishFollowUps -> Complete, while seven
runtime owners remain synchronous borrows. Three authority-free participant
bags and five free operations are deleted; the exhaustive cursor test, full
gate, and bounded graphics stress pass without baseline motion. The live ledger
is now 4/13 (31%).
GV4 then completed the governance plan at 5/5 and removed it from the live
inventory. PB0 subsequently ratified all 22 registered parameter-bag rows at
implementation tip `e61e82a6`, added eight repair rows, carried forward three
13-parameter render/UI operations, and explicitly ruled every other reviewed
hit. Permanent PB0 evidence is
`../Reports/2026-07-26/concrete-parameter-bag-elimination-pb0-census.md`.
The active/future ledger is now 1/8 (13%).

The denominator grew 0 → 56 on 2026-07-26 when the owner registered the
thirteen-plan Architecture Follow-Up Campaign Round 5 from the same-day
from-source engine architecture review of `nightrunner-26th-JUL-26` at tip
`35f6de4e` (review conducted without plan files or git history; each plan
document carries the dated file:line evidence). The campaign owns one plan per
review finding plus the owner's two same-day requests: scene-sized store
capacity and closer memory tracking. Execution order is 1→13 as listed in the
campaign section below. `governance-shape-to-judgment-conversion` runs first
because its amended rules are the acceptance test every sibling plan's closure
review uses.

The denominator grew 56 → 59 on 2026-07-27 when the owner registered
`new-aggregate-ruling-gate` (3 tasks) as the campaign's fourteenth plan, after a
same-day review of what the campaign does and does not close. The G2/G3 gate fires
only on `single-member` and `empty`, both decidable from a declaration alone; the
four-to-seven-member context bag that caused every historical problem is
unguarded, proved by running the shipped tool against a planted seven-reference
aggregate (`GATING=[]`). Plans 4/5/6 delete the roughly 40 that exist and leave
number 41 unopposed. The new plan makes the ruling file the permanent baseline so a
gated aggregate cannot enter the tree without a written reason. It may run
immediately, ahead of plan 4, because the gate should protect during the campaign
rather than after it. NA2 depends on plan 4's CA4 and may sit unchecked until then.
Two limits are recorded rather than overclaimed: the destructured-at-entry test
stays ungated because it is not lexically decidable, and a name-scoped gate remains
evadable by renaming, which is why the `AGENTS.md` review question stays the
backstop.

The denominator then fell 59 → 53 on 2026-07-27 when
`governance-shape-to-judgment-conversion` closed G4 at 6/6 and left the live
inventory under rule 4. Permanent closure evidence is
`../Reports/2026-07-27/governance-shape-to-judgment-conversion-closure.md`.

The denominator then fell 53 → 50 on 2026-07-27 when
`extraction-scar-remediation` closed ES0-ES2 and left the live inventory under
rule 4. All 88 repair findings are gone, the sole WorkerPool retain is
unchanged, and byte-exact Physics plus independent ownership review are clear.
Permanent closure evidence is
`../Reports/2026-07-27/extraction-scar-remediation-closure.md`.

The denominator then fell 50 to 42 on 2026-07-27 when
`scene-sized-store-capacity` closed SC0-SC7 and left the live inventory under
rule 4. Final source has 98 scene-committed Physics rows, the complete retained
payload falls 89.06% for the 200-body scene, and the isolated prediction-engine
request falls 82.21%. Permanent closure evidence is
`../Reports/2026-07-27/scene-sized-store-capacity-closure.md`.

The denominator then fell 42 to 38 on 2026-07-27 when
`store-capacity-memory-reporting` closed MR0-MR3 and left the live inventory
under rule 4. The final three-scene process reports 95 sorted rows per section,
the Memory tab and unload log allocate zero in guarded phases, and independent
review cleared the canonical publisher-token authority after two remediation
passes. Permanent closure evidence is
`../Reports/2026-07-27/store-capacity-memory-reporting-closure.md`.

The denominator then fell 38 to 33 on 2026-07-27 when
`ceremonial-aggregate-elimination` closed CA0-CA4 and left the live inventory
under rule 4. All 35 authority-free couriers are gone without renamed
equivalents, the full-tree signature maximum remains 12, and the final
inventories contain zero signalled, unruled, stale, or `remove` rows.
Permanent closure evidence is
`../Reports/2026-07-27/ceremonial-aggregate-elimination-closure.md`.

The denominator then fell 33 to 29 on 2026-07-27 when
`render-backend-service-bag-removal` closed RB0-RB3 and left the live inventory
under rule 4. The eleven-pointer bag is deleted, required capture is explicit,
optional capability presence is startup-owned, and all closure gates pass
without baseline refresh. Permanent closure evidence is
`../Reports/2026-07-27/render-backend-service-bag-removal-closure.md`.

The denominator then fell 29 to 25 on 2026-07-27 when
`scene-runtime-verb-partition-consolidation` closed SR0-SR3 and left the live
inventory under rule 4. Seven verb units are deleted; residual files and types
carry owner/domain names; the 60-file comment audit and independent ownership
review are clear; and Physics, DX12, and full gates pass without baseline
refresh. Permanent closure evidence is
`../Reports/2026-07-27/scene-runtime-verb-partition-closure.md`.

The denominator then fell 25 to 21 on 2026-07-27 when
`operator-command-invariant-ownership` closed OC0-OC3 and left the live
inventory under rule 4. One non-copyable value-only transaction owns the exact
eight-edge phase walk, same-frame arbitration, and unified acceptance ledger;
the seven result records, seventeen free apply entry points, and all 71
`RunInternal` rows are gone. Independent review and the 37-file comment audit
are clear; full, Automation, DX12, one-minute graphics stress, and byte-exact
Physics gates pass without baseline refresh. Permanent closure evidence is
`../Reports/2026-07-27/operator-command-invariant-ownership-closure.md`.

### Binding Owner Rulings — 2026-07-27: Round 5 Open Decisions Closed

The owner ruled all three blocking Round 5 decisions on 2026-07-27 so the campaign
can run to completion without stopping. Each is recorded in its owning plan with
the reason, and each rejected alternative is named so a later reviewer does not
relitigate it.

1. **Plan 5 FV0 — frame loop endpoint: concrete operands.** Delete all four
   `RuntimeFrame*View` structs; every phase takes only the owners it uses. The
   frame-turn phase-cursor transaction is rejected: a cursor earns its keep when
   many call sites re-encode the order, and `Run::Execute` is a single call site
   that already enforces order by being a linear schedule. A long but honest
   argument list is the accepted outcome; reintroducing an aggregate to shorten one
   is a closure failure.
2. **Plan 11 AF1 — foreign free: lane-F fatal in Debug/Profile, counted in
   Release.** Always-fatal is rejected because a long-silent third-party
   interaction would become a shutdown crash; counted-only is rejected because it
   leaves the heap risk with no development signal. A non-zero Debug/Profile count
   is now a gate failure rather than an explainable observation.
3. **Plan 13 T3 — terrain contact seed: ratify the approximation.** Name the
   constant, state the vertical-gravity assumption, add tests pinning the two
   behaviors it exists for. Replacement is rejected on cost: it would require a
   full physics and replay baseline transition for no visible gameplay gain. A
   principled replacement is explicitly not a deferred follow-up row.

Consequence for the campaign: **no Round 5 plan requires divergence authority and
no plan may refresh a physics, SkullScope, replay, visual, or DX12 baseline.** The
whole campaign is byte-exact. Two smaller decisions were given explicit safe
defaults in the same pass so no phase stalls on them: plan 13 T2 leaves
`LocatePolygon` in place behind a bound guard and only reports whether its debug
caller should move, and plan 1's Capability Slice Ownership Rule landed
layer-agnostic rather than Runtime-only, which changes no current finding.

## Current Execution Priority

**Look Lab Random Style Authoring (2026-08-01) is first in the live order at
0/7.** F10 rerolls presentation only and F11 writes one ignored root
`LookLab/<datetime>_<seed>/` bundle containing the standalone style, text receipt,
and matching screenshot. F5/F6 remain unchanged, lenses/camera are out of scope,
and no default visual baseline may move.

**Contact Energy And Warm-Start Integrity (2026-08-01) is second at 0/7.** Its
agent runs ES0-ES6 without an owner pause, leaves tracked baselines
untouched, and presents a completed implementation plus exact staged transition
for the owner's terminal baseline decision. An expected old-oracle mismatch is
decision evidence, not an engineering blocker.

The Gate Blind Spot Campaign (2026-07-31) is complete at 21/21 and has left the
active/future ledger under rule 4.

Its five plans closed in order with permanent reports and no unauthorized
baseline refresh. Their completion predates the current contact-energy plan and
carries no remaining live work.

The Fresh-Read Engine Review Campaign (2026-07-29) is complete. Plan 9
`persistent-contact-convergence-early-out` closed CE2-CE3 on the owner's
approved retain decision: the dense wall's 12-iteration cap reflects honest
row-level non-convergence, so the current stopping criterion remains unchanged.
All nine plans are complete and excluded under rule 4; the active/future ledger
is empty (0%).

Validation-gate integrity is also complete at 6/6 and excluded under rule 4.
The owner retired merge queues and any GitHub ownership change, exact-commit
hosted CPU run 30505659321 passed, and graphics-card validation remains
local-only.

The Claim Integrity Campaign (2026-07-30) is complete. Build Configuration
Parity, Maths Surface Reachability, Inverse-Trig Domain Guards, Retirement
Diagnostic Honesty, and Unreachable Symbol Remediation all closed and left the
live ledger under rule 4. The final remediation deleted 181 unreachable
functions, retained 79 exact ruled seams, and expanded compiled reachability to
the mandatory Automation/Debug/Profile object graph. No plan in this campaign
used bounded divergence; final Physics remained byte-exact.

Plans 1-6 were strictly byte-exact. Plan 7 moved its baselines only after the
QN4 hands-on visual acceptance checkbox. Plan 8 used its explicit
bounded-divergence allowance once and closed with the final four-artifact
transition recorded in its closure report. Plan 9 inherits no divergence
authority. The required pre-plan-1 recovery is complete: the contact-identity
regression test is pushed at
`origin/codex/contact-identity-regression-29th-jul-26` commit `27906417`, the
stale source/baseline hunks were not imported, and the original stash was
dropped.

The Principal Engineer Feedback Campaign and its protected final local action
are complete. All seven plans closed and left the live inventory under rule 4;
generated dependency proofs were the last 3/3 plan. The warm-start key-capacity
guard then passed byte-exact contact stability, Physics-frame and inclusive
solver performance gates, comment audit, and independent review without a
baseline refresh. Permanent evidence is
`../Reports/2026-07-28/persistent-contact-key-capacity-closure.md`. The live
plan ledger remains empty at 0/0 and no local campaign action remains.

The Nightrunner 26 July campaign is complete at 3/3. Replay scrub performance,
owner code style, and the space-scene velocity-drag preview are closed, and the
completed TODO was deleted under inventory rule 4. Permanent evidence is
`../Reports/2026-07-26/nightrunner-26-july-closure.md`.

Architecture Follow-Up Campaign Round 5 is complete at 7/7 (100%). Plan 11
closed AF2 and left the live inventory under rule 4. Plan 5 closed FV0-FV3:
all four frame views are deleted, delegated operations are concrete and capped
at 12 operands, independent review is clear, and every mapped gate passes.
Plan 12 SR0 measured 176 named returning definitions plus one lambda and selected sentinel-only
success construction because the protected 511-byte failure maximum forbids a
smaller buffer. SR1 implemented the ruling without changing failure storage or
call sites. SR2 corrected the initial census-review blocker, then closed with
zero review blockers and passing tests, performance, and full validation.
`governance-shape-to-judgment-conversion`, `extraction-scar-remediation`, and
`scene-sized-store-capacity` closed on 2026-07-27 and left the live inventory
under rule 4. Plan 3 `store-capacity-memory-reporting` and plan 4
`ceremonial-aggregate-elimination` closed and left the live inventory on
2026-07-27. Plan 6 `render-backend-service-bag-removal` closed RB0-RB3 and left
the live inventory under rule 4. Plan 5 `runtime-frame-view-retirement` closed
FV0, then blocked at FV1 because its concrete-only endpoint, parameter ceiling,
short schedule, and carrier ban are jointly unsatisfiable for the fixed-step
coordinator. Plan 8 `scene-runtime-verb-partition-consolidation` closed SR0-SR3
with clear review and unchanged baselines, then left the live inventory under
rule 4. Plan 9 `operator-command-invariant-ownership` closed OC0 with the exact
phase order, arbitration winners, operation destinations, and acceptance-ledger
consumers. OC1 installed the non-copyable value-only transaction and proved all
82 illegal calls from reachable phases fatal. OC2 moved every operation behind
the phase owner, unified the ledger, and removed all 71 `RunInternal` rows. OC3
closed with clear independent review, a 37/37 comment audit, and all mapped
gates; plan 9 then left the live inventory under rule 4. Plan 10 CG0 assigned
all five gate-named tests to existing subsystem files and recorded the direct
coverage baseline. CG1 deleted the gate-named file and preserved all 418 tests,
2,410,159 assertions, and ten exact coverage results. CG2 installed the
permanent subsystem-naming rule, passed every CPU lane, and closed with clear
independent review; plan 10 then left the live inventory under rule 4. Plan 11
AF0 selected a table-based MSVC exception guard for the one candidate-header
read, proved the inaccessible-predecessor-page case, and passed the complete
performance gate without regression. AF1 installed the process-lifetime
tripwire, the ratified Debug/Profile fatal and Release counted/reporting CRT
fallback, checked allocation-size arithmetic, and joined the existing memory
diagnostics without changing the zero-count UI fingerprint. AF2 is binding
only after independent review found and the candidate closed two magic-only
provenance bypasses with a guarded whole-header copy and a process-specific
ownership cookie. The owner retained that safe provenance and replaced literal
instruction identity with a clean `validate_perf.bat` no-regression contract,
unblocking AF2. AF2 then closed with zero foreign frees across performance and
graphics stress, clear independent review, clean allocation-policy scans, all
421 tests, and the default full gate. Plan 11 left the live inventory under
rule 4. The owner also permitted direct `Run` coordinator member reach
while delegated operations remain concrete and at or below 12 parameters,
unblocking plan 5. Plan 12 remains sequenced after plan 5. Plan 14 NA0 then ran
while plans 5 and 11 were blocked,
plan 12 waited for plan 5, and plan 13 remained last. Plan 14 NA0 re-measured 1,167
discovered types and the bounded 84-row suffix/no-invariant gated set. All 84
already have CA0 rulings and the transitional verdict count is zero. Explicit
strict mode, source-drift validation, and planted transition fixtures are in
place. NA1 wired strict mode into `validate_fast`, stated the permanent rule and
name-scope residual, and observed the real gate reject a planted seven-member
`FooFrameContext`. NA2 retired the transition, closed every independently found
scanner bypass, replaced generic ruling prose with 86 concrete ownership
reasons, and passed `validate_fast` plus all six CPU lanes with clear independent
review. Plan 14 then left the live inventory under rule 4. Plan 13 T0 reproduced
the 44,401-row physics oracle and SkullScope query packet exactly, and named a
four-case / 47-assertion focused terrain-support oracle. The missing direct
shoreline-scale test is the expected T3 gap, not a baseline mismatch. T1 is
complete with world-X/world-Z cell names, `quad` terminology, and explicit
zero comparison; all focused, physics, deep physics, and SkullScope oracles
remain exact. T2 locally proves every `LocatePolygon` post read, adds an exact
upper-edge Debug fatal probe, and retains the method because its sole
debug-visualizer caller needs triangle vertices the cache view does not expose.
The focused oracle and byte-exact Physics gate pass. T3 names the full and
shoreline seed scales, records the complete
vertical-gravity/unit-normal/equal-point assumption set, and pins first-frame
rest, a real classified two-point terrain edge, stage sleep inhibition, and a
one-iteration three-box stack at all three strengths. T4 closed the 4/4
whole-file comment audit and all five independent-review findings. Format,
421/421 tests and 2,410,268 assertions, byte-exact Physics and deep Physics,
exact SkullScope, performance, and the default full gate pass without baseline
refresh. Plan 13 then left the live inventory under rule 4. Plans 5 and 11
remain blocked and plan 12 waits for plan 5; there is no next non-blocked item.

G0 recorded five rule gaps with the admitting `AGENTS.md` text for each; evidence
is `../Reports/2026-07-26/governance-shape-to-judgment-g0-census.md`. The measured
figures corrected the originating review twice: 89 extraction scars across 15
files rather than the hand-counted 33 across 4, and **zero of 94 aggregate
candidates state a per-type `Invariant:` block**. The two largest scar sites are
the two frame-view consumers, which destructure the views straight back into
`m_`-named locals — mechanical evidence for `runtime-frame-view-retirement`.
G1/G1b amended `AGENTS.md` plus both independent-review skills, the orchestrator
skill, and two reference guides; the review skills previously contained no
aggregate criteria at all, which is why the other four gaps went unenforced. G2/G3
landed two repeatable inventories and wired them into `validate_fast` step 4/8 on
an unruled-fails/ruled-passes contract with no frozen count anywhere. G4 then
hardened both inventories against the review's structural and lexical evasion
cases, reconciled every signalled row to a named implementation plan, passed
independent review with zero blockers, and passed both mandatory validation
umbrellas. `extraction-scar-remediation` then removed all 88 repair findings
across 14 edited files while preserving the fifteenth file's language-required
WorkerPool binding. Permanent evidence is
`../Reports/2026-07-27/extraction-scar-remediation-closure.md`.

The 2026-07-25 round-4 campaign has no live plans. Concrete Parameter-Bag
Elimination closed PB0-PB7 and left the live ledger under rule 4. Invariant
Ownership Governance And Transaction Repair closed GV0-GV4 and left the live
ledger under rule 4;
permanent evidence is
`../Reports/2026-07-26/invariant-ownership-governance-and-transaction-repair-closure.md`.
PB1 then installed the three owner-produced save publications, removed the
duplicate writer view and scene-load policy bag, split the editor hotkey
authority, and passed 398 doctests plus the 173.3-second broad gate. Permanent
evidence is
`../Reports/2026-07-26/concrete-parameter-bag-elimination-pb1-scene.md`.
PB2 then deleted the Runtime/editor/mouse-pick projection bags, retained
`RuntimePointerEvent` as the one semantic pointer value, and passed the
256.6-second broad gate without a DX12 baseline refresh. Permanent evidence is
`../Reports/2026-07-26/concrete-parameter-bag-elimination-pb2-pointer-routing.md`.
PB3 then deleted the render-frame, UI-text, Replay-overlay, and graph-callback
service bags, repaired all three wide operations, and passed DX12, stress,
performance, Replay visual-fidelity, and broad gates without a baseline
refresh. Permanent evidence is
`../Reports/2026-07-26/concrete-parameter-bag-elimination-pb3-render-ui.md`.
PB4 then removed all eight Replay capture, focus, and restore bags. Concrete
capture/focus values and the owner-free, phase-checked restore transaction pass
the artifact, allocation-policy, scrub/visual-fidelity, focused-test, and broad
gates without a baseline refresh. Permanent evidence is
`../Reports/2026-07-26/concrete-parameter-bag-elimination-pb4-replay.md`.
PB5 then deleted the five collision, narrowphase, broadphase-filter, and
persistent-solver bags. Concrete stage APIs preserve bounded storage and
serial/parallel pair order, and pass focused tests plus Physics, performance,
and broad gates with the physics oracle byte-exact. Permanent evidence is
`../Reports/2026-07-26/concrete-parameter-bag-elimination-pb5-physics-collision-solver.md`.
PB6 then deleted the seven sleep, wake, external-force, terrain, force, and
integration bags plus the obsolete shared context header. Direct stage
operations preserve worker partitioning and wake/terrain order, and pass
focused tests plus Physics, performance, and broad gates with the physics
oracle byte-exact. Permanent evidence is
`../Reports/2026-07-26/concrete-parameter-bag-elimination-pb6-physics-sleep-force-terrain.md`.
PB7 then reconciled all 30 repair rows and three ceiling defects, remediated
the independent review's UI ownership and focused-test findings, completed the
91/91 comment audit, and passed every cumulative mapped gate without baseline
refresh. Permanent evidence is
`../Reports/2026-07-26/concrete-parameter-bag-elimination-closure.md`.
PB0-PB7 are complete. The completed plan leaves the live inventory under rule
4, returning the active/future denominator from 8 to 0.
Header-claim staleness remediation is complete at 3/3 and removed
from the live ledger; its permanent evidence is
`../Reports/2026-07-25/header-claim-staleness-remediation-closure.md`. Replay
RS0 evidence is
`../Reports/2026-07-25/replay-subsystem-partition-rs0-census.md`; RS1 evidence
is `../Reports/2026-07-25/replay-subsystem-partition-rs1-prediction.md`; RS2
evidence is
`../Reports/2026-07-25/replay-subsystem-partition-rs2-planning.md`. RS3 now
has permanent evidence in
`../Reports/2026-07-26/replay-subsystem-partition-rs3-seams.md`; RS4 evidence
is `../Reports/2026-07-26/replay-subsystem-partition-rs4-enforcement.md`.
Replay is complete at 6/6 and removed from the live inventory; permanent
closure evidence is
`../Reports/2026-07-26/replay-subsystem-partition-closure.md`. Downward Domain
Bleed Remediation is complete at 6/6 and removed from the live inventory;
permanent closure evidence is
`../Reports/2026-07-26/downward-domain-bleed-remediation-closure.md`. GV1 now
has permanent evidence in
`../Reports/2026-07-26/invariant-ownership-governance-gv1-census.md`; GV2 has
permanent evidence in
`../Reports/2026-07-26/invariant-ownership-governance-gv2-scene-load-transaction.md`;
GV3 has permanent evidence in
`../Reports/2026-07-26/invariant-ownership-governance-gv3-generated-scene-transaction.md`;
GV4 owns the front of the dependency chain.
The UI plan preserves the existing UI/Runtime value-command boundary and
makes Runtime/Render the sole UI-to-renderer composition point, and its UR5
validator is the shared enforcement vehicle the two follow-up plans extend
(RS4, DB4). Use the repository orchestrator skill for implementation.

`solar-system-slingshot-usability` closed 4/4 on 2026-07-24 after the XY/Z-up
correction, newest-state held-drag prediction, extended horizon, 32-body Mars
demonstration, independent review, and mapped gates. Closure evidence is in
[`solar-system-slingshot-usability-closure`](../Reports/2026-07-24/solar-system-slingshot-usability-closure.md).

The prior solar-system trajectory planner closed SS0-SS6 on 2026-07-24 after
its independent review findings were remediated and all final gates passed.
Closure evidence is in
[`solar-system-trajectory-planner-closure`](../Reports/2026-07-24/solar-system-trajectory-planner-closure.md).

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

The architecture-review campaign and both 2026-07-22 architecture follow-up
campaigns are complete. The round-2 sequence closed Physics standalone-world
unification, Run frame-phase decomposition, RuntimeRenderer decomposition,
Replay deduplication, and wide-signature reduction in binding order. Closure
reports are linked from the round-2 campaign section below.
Dependency-direction restoration, allocation-namespace restoration,
physics-facade unification, physics-settings snapshot, Run::Execute
de-accretion, render-graph completion, Render HAL modernization, and gameplay
module extraction are closed. Gameplay now owns tornado state, forces, and
visuals above bounded Physics/Rendering value seams; final full, physics,
performance, replay, DX12, and stress gates pass, and independent ownership
review is clear. Closure evidence is in
`../Reports/2026-07-21/gameplay-module-extraction-closure.md`.
Replay boundary containment is closed with an enforceable zero-inbound rule,
an exact three-owner privilege inventory, a complete surface census, and clear
independent review. Closure evidence is in
`../Reports/2026-07-21/replay-boundary-containment-closure.md`. Its audit-filed
policy follow-up is also closed: strict allocation is zero/zero, identity is
converged on `PhysicsSceneObjectId`, artifact bytes/schema are unchanged, and
the final performance, physics, full, and visual-fidelity gates pass. Closure
evidence is in `../Reports/2026-07-21/replay-policy-debt-closure.md`. Owner decisions
ratified at registration: finish the render-graph migration (freezing rejected);
`PhysicsEngine` survives the facade unification; extracted gameplay lives in
a new top-level `SkullbonezSource/Gameplay/` module. The separately parked E17
owner item is now accepted and closed.

`imgui-tracy-editor-campaign` (E0-E17) is complete at 18/18. E14-E15 are
reconciled against the accepted P1 one-process golden and their retained replay
gates now pass exactly without another engine process. E16 is complete and
E17's automatable evaluation, review, gates, assisted playtest, and owner
acceptance are complete.
The 2026-07-18 owner amendment
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
P1 later reconciled that same one-process report. A 2026-07-21 current-tip
assisted ImGui/Tracy/Legacy playtest found no blocking usability issue, and the
owner then accepted the editor as ready for extended hands-on use. The campaign
closes at 18/18.
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
`15th-of-July-Night-Runner`. Validation-gate integrity is complete at 6/6,
excluded under rule 4, and recorded in
`Agentic/Reports/2026-07-30/validation-gate-integrity-closure.md`. The previous
replay critical path completed on `nightrunner-14th-july`.

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
| [replay-visual-fidelity-mega-probe](../Reports/2026-07-14/replay-visual-fidelity-mega-probe-closure.md) | Complete | 7/7 | One engine, one prediction, 2,401 exact ticks, 187 grounded sleepers, durable CPU-only reconstruction, and adversarial closure approved |
| replay-monolith-decomposition | Complete on `nightrunner-14th-july` | 9/9 | Retain as closure evidence while the active/future portfolio continues with the spline plan |
| future-path-vector-splines | Complete on `nightrunner-14th-july` | 7/7 | Owner-approved golden reconciled; one-process 2,401-tick oracle and all final gates passed |

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

Historical 2026-07-12 owner ruling: full-record AoS `PhysicsBodyRecord`
reshaping and terrain warm-start/clamp heuristic replacement were deliberately
not planned because they were working, documented, and baseline-entangled.
The 2026-07-28 principal-feedback campaign supersedes that ruling only for the
hot-store evidence question. The completed measurement retained SoA; closure is
recorded in
`../Reports/2026-07-28/physics-body-hot-layout-closure.md`.
Terrain heuristic replacement remains parked. Repeated glossary-header
deduplication is
available as a documentation-only plan if the owner wants it (currently
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
tab strip. The complete 18-task campaign and owner acceptance are recorded in
the [closure report](../Reports/2026-07-21/imgui-tracy-editor-campaign-closure.md).

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
| [ImGui + Tracy editor closure](../Reports/2026-07-21/imgui-tracy-editor-campaign-closure.md) | Complete — owner accepted extended hands-on use; Legacy retained as default | 18/18 | Closed; await new owner direction |

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
`SkullbonezSource/Gameplay/` module; the campaign started with E17 hands-on
acceptance parked as a non-blocking owner item, which is now accepted.

Standing rules binding every plan in this campaign: zero behavioral
baseline, golden, screenshot, replay, or physics CSV refresh — byte-exact
and image-identical oracles prove each refactor, and divergence is reverted,
never normalized; every DX12 slice runs the bounded graphics-stress proof
per inventory rule 10; replay-facing slices run the one-invocation mega gate
per inventory rule 11; one independent rubber-duck review per plan at
closure, not per slice; no new compatibility spellings, forwarding wrappers,
or hot-path inheritance/callback artifacts (existing review rules apply
verbatim). The Legacy/ImGui UI resolution was deliberately **not** part of
this campaign; the owner accepted E17 separately on 2026-07-21.

Execution order was binding (biggest wins first). All eight architecture-review
campaign plans are closed with exact evidence and independent review. No live
execution row remains; E17 extended owner acceptance is also complete.

## Architecture Follow-Up Campaign (2026-07-22)

Source: the owner-requested critical architecture review of 2026-07-22
(chat review of the post-campaign tip; no report file — the plan documents
carry the dated evidence). The review found no living god object and named
three remaining structural debts: coupling made legible rather than reduced
(18-owner scene-load borrow graph, four hand-rolled frame-view structs), a
ten-interface render HAL with exactly one implementation each and a
seven-interface `RenderBackendDX12` aggregation monolith, and replay as the
largest single domain (34,735 lines, the repository's two biggest TUs) with
interleaved interior domains.

Owner decisions ratified at registration: DX12 is the terminal runtime
backend and the abstract render interface layer is retired (supersedes the
retained-HAL exception recorded at `render-hal-modernization` M0/M5); the
scene-lifecycle ledger pattern replaces reactive owners' presence in the
load borrow graph; replay is consolidated behind `ReplayRuntime` plus typed
value packets with six named interior domains, behavior frozen.

Standing rules binding every plan in this campaign: zero behavioral
baseline, golden, screenshot, replay, or physics CSV refresh; every DX12
slice runs the bounded graphics-stress proof per inventory rule 10;
replay-facing slices run the one-invocation mega gate per inventory rule
11; one independent rubber-duck review per plan at closure; no new
compatibility spellings, forwarding wrappers, context bags, callback packs,
or hot-path inheritance artifacts. Execution order is binding.

| Plan | State | Verified phase count | Start condition / next action |
|---|---|---:|---|
| [owner-fanout-reduction](../Reports/2026-07-22/owner-fanout-reduction-of5-closure-census.md) | Complete | 6/6 | Ten-input scene-load boundary, ≤3-file reactive-owner path, clear independent review, and final broad/stress gates complete |
| [replay-subsystem-consolidation](../Reports/2026-07-22/replay-subsystem-consolidation-closure.md) | Complete | 7/7 | Six domains, 48 → 33 production include edges, accepted cohesion exceptions, strict allocation proof, clear review, final broad gate, and reconciled 2,401-tick replay oracle complete |

Render interface retirement closed RH0-RH5 with zero interface classes,
narrower transient authority, clear independent review, and complete final
gates. Closure evidence:
[`render-interface-retirement-closure`](../Reports/2026-07-22/render-interface-retirement-closure.md).

## Architecture Follow-Up Campaign Round 2 (2026-07-22)

Source: the owner-requested critical architecture review of 2026-07-22 at
main tip 0c5263e1 (chat review; the plan documents carry the dated
evidence). The review found the classic god objects closed and named the
remaining debts this campaign owns: a second physics simulation
(`PhysicsStandaloneWorld`, 2,226-line `PhysicsApi.cpp`, one production
consumer) diverging from the production solver; `Run::Execute` as a
~570-line god function over completed state decomposition; `RuntimeRenderer`
as the next god object forming (~50 members, replay presentation state,
11-13-argument UI-text signatures); possible duplicated logic inside the
36,332-line replay subsystem; and an unruled 7-11-argument signature band
left by the 2026-07-15 wide-call inventory.

Owner decisions ratified at registration: the production solver is the only
physics simulation — the standalone world is deleted and its smoke re-hosted
on `PhysicsEngine`; `Run::Execute` phases stay on `Run` (a FrameDriver owner
is rejected as a forwarding shape); replay grading state belongs to the
Replay presentation domain; **replay is the engine's most important
subsystem — the dedup plan is an internal-quality pass, not a slimming
exercise, and size alone is not a finding**; the wide-signature inventory
threshold is ≥7 parameters with rulings allowed to accept width.

Standing rules binding every plan in this campaign: zero behavioral
baseline, golden, screenshot, replay, or physics CSV refresh — divergence is
reverted, never normalized; every DX12 slice runs the bounded
graphics-stress proof per inventory rule 10; replay-facing slices run the
one-invocation mega gate per inventory rule 11; one independent rubber-duck
review per plan at closure; no new compatibility spellings, forwarding
wrappers, context bags, callback packs, or hot-path inheritance artifacts;
prior R-/RC-era cohesion rulings are re-litigated only with the new evidence
a census records. Execution order is binding: 1 physics unification,
2 frame-phase decomposition, 3 renderer decomposition, 4 replay dedup audit,
5 wide-signature reduction.

| Plan | State | Verified phase count | Start condition / next action |
|---|---|---:|---|
| [replay-deduplication-audit](../Reports/2026-07-22/replay-deduplication-closure.md) | Complete | 4/4 | C1-C5 consolidated; C6-C7 retained; all Replay/full gates pass |
| [wide-signature-reduction](../Reports/2026-07-23/wide-signature-reduction-closure.md) | Complete | 5/5 | 301 → 285; all survivors ruled; independent review and final broad gate pass |
| [wide-signature-decomposition-round-2](../Reports/2026-07-23/wide-signature-decomposition-round-2-closure.md) | Complete | 5/5 | 8 → 0 threshold-16 rows; independent review and all closure gates pass |
| [wide-signature-decomposition-round-3](../Reports/2026-07-23/wide-signature-decomposition-round-3-closure.md) | Complete | 6/6 | 9 → 0 threshold-13 rows; independent review and all closure gates pass |
| [wide-signature-parameter-bag-remediation](../Reports/2026-07-23/wide-signature-parameter-bag-remediation-closure.md) | Complete | 6/6 | Mechanical campaign packs removed; empty threshold-13 scan, hostile review, and all closure gates pass |

## Architecture Follow-Up Campaign Round 3 (2026-07-23)

Source: the owner-requested from-source architecture review of 2026-07-23 on
`nightrunner-22nd-JUL-26`, conducted deliberately without consulting plan
files or git history; the three plan documents carry the dated file:line
evidence. The review found the class-level god objects closed and named the
remaining structural debts this campaign owns: the Runtime package as the
last structural god object (242 files / ~108K lines, 81 loose top-level
files, no internal layering or edge proofs); a bidirectional UI↔Runtime
package tangle (a complete 10-row UI→Runtime include census) that the
repository's dependency-proof regime does not police; and five source
blemishes — cold `contactMaterialName` bytes in the hot `ColliderRecord`
row, diagnostics names on the `PhysicsEngine::Step` signature, `Run*` file
names on ten files with zero `Run::` members, the Core `Profiler`
implemented inside Rendering, and one oversized development-tools TU.

Owner exclusions ratified at registration: Replay internals are frozen for
all three plans; wide parameter seams are excluded (they belong to the
active `wide-signature-parameter-bag-remediation` lane and its accepted
12-parameter ceiling); the engine/game content boundary
(`TornadoGameplay` in `SceneWorld`) is untouched. Owner direction ruling for
plan 2: Runtime may include UI; UI must never include Runtime — UI consumes
value snapshots and emits typed commands.

Standing rules binding every plan in this campaign: zero behavioral
baseline, golden, screenshot, replay, or physics CSV refresh — divergence is
reverted, never normalized; physics CSV stays byte-exact through every
phase; one independent rubber-duck review per plan at closure; no new
compatibility spellings, forwarding headers, context bags, callback packs,
or hot-path inheritance artifacts; census evidence is re-generated at
execution time because the active parameter-bag lane is touching the same
files. Execution order is binding: 1 source blemish remediation (its B3
renames precede any file moves), 2 UI/Runtime separation, 3 Runtime package
decomposition (its R1 census then sees final names and the relocated
navigation model).

| Plan | State | Verified phase count | Start condition / next action |
|---|---|---:|---|
| [source-blemish-remediation](../Reports/2026-07-23/source-blemish-remediation-closure.md) | Complete | 6/6 | Hot/cold collider data, diagnostic registration, honest Runtime names, profiler placement, owner ruling, independent review, and final gates complete |
| [ui-runtime-separation](../Reports/2026-07-23/ui-runtime-separation-closure.md) | Complete | 5/5 | Zero UI→Runtime includes; independent review finding remediated; all standing proofs and final gates pass |
| [runtime-package-decomposition](../Reports/2026-07-23/runtime-package-decomposition-closure.md) | Complete | 5/5 | 80 exact moves, one top-level residue, operational package proofs, independent review, and all closure gates pass |

Architecture Follow-Up Campaign Round 3 closed on 2026-07-23. Its 16/16
registered tasks are complete, so the active/future denominator returns
16 → 0 under rule 4. Runtime now has policed physical packages, UI is a
one-way presentation dependency below Runtime, and all five source blemishes
are resolved. Closure evidence is linked from the table above.

## Solar System Trajectory Planner (2026-07-23)

The owner registered one seven-task gameplay/replay feature after Architecture
Follow-Up Round 3 closed, growing the active/future denominator 0 → 7. Replay
remains the flagship prediction owner; Physics, Rendering, Scene, World, and
Core are frozen for this plan.

| Plan | State | Verified phase count | Next action |
|---|---|---:|---|
| [solar-system-trajectory-planner](../Reports/2026-07-24/solar-system-trajectory-planner-closure.md) | Complete | 7/7 | SS0-SS6 complete; rollback, state gating, default-off, epoch, review, and final gates closed |

The plan closed 7/7 on 2026-07-24 and leaves the live ledger under inventory
rule 4. The active/future denominator returns 7 → 0.

## Solar System Slingshot Usability (2026-07-24)

The owner registered one four-task corrective/demo plan, growing the
active/future denominator from 0 to 4. It corrects the existing solar XY/Z-up
presentation, replaces the obsolete release-only velocity-drag refresh policy
with bounded newest-state prediction restarts, separates the 20-second default
from an extended operator horizon, and adds a 32-body major-moon
Earth-flyby-to-Mars demonstration.

| Plan | State | Verified phase count | Next action |
|---|---|---:|---|
| [solar-system-slingshot-usability](../Reports/2026-07-24/solar-system-slingshot-usability-closure.md) | Complete | 4/4 | Closed; active/future denominator returned 4 → 0 |

SSU0-SSU3 closed on 2026-07-24. The completed plan left the active/future
ledger under inventory rule 4, returning the denominator from 4 to 0.

## Prediction Retained Rendering Merge Handoff (2026-07-24)

The owner directed the current bounded prediction-rendering stabilization branch
to be documented and committed for merge. This is corrective merge work rather
than a newly registered portfolio campaign, so the active/future denominator
remains zero.

The branch retains prediction trajectory commands incrementally, preserves the
frame-local deterministic golden as an independent oracle, forces large contact
scenes such as the 200-box wall onto amortized scheduling, restores ball picking
with the expanded Legacy UI visible, and publishes the acquire-latched build-root
trajectory prefix so the ball line grows before contact instead of popping in at
impact. The latest symptom is classified as retained-rendering publication, not
missing physics data.

  The replay visual-fidelity, DX12 renderer, bounded graphics stress, and full
  default PR gates pass. Merge state, validation evidence, hazards, and the
  required first owner-visible check are recorded in
  [`prediction-retained-rendering-handoff`](../Reports/2026-07-24/prediction-retained-rendering-handoff.md).

## Solar Prediction Presentation Correction (2026-07-25)

Complete at 4/4. The correction distinguishes authored escape from prediction
divergence, re-authors the 32-body Mars-assist scene, adds globally persisted
trajectory appearance controls with held-drag refresh, and makes the solar
shadow/reflection opt-out a real no-pass renderer policy. Independent review
findings and all validation evidence are recorded in
[`solar-prediction-presentation-correction-closure`](../Reports/2026-07-25/solar-prediction-presentation-correction-closure.md).
The active/future denominator returns to zero.

## UI / Renderer Hard Boundary (2026-07-25)

The owner registered one seven-task architecture plan after the solar
prediction correction closed, growing the active/future denominator 0 → 7.
The prior UI/Runtime separation remains binding: UI consumes detached values,
emits typed commands, and never includes Runtime. This plan closes the
remaining backend seam: 25 direct UI-to-Rendering include rows across 14 UI
files, UI-owned DX12 resource state, renderer-owned operator presentation, the
shared production build target, and the absence of a mandatory dependency
regression gate.

| Plan | State | Verified phase count | Next action |
|---|---|---:|---|
| [ui-renderer-hard-boundary](../Reports/2026-07-25/ui-renderer-hard-boundary-closure.md) | Complete | 7/7 | Backend-neutral UI and Runtime/Render composition boundary closed; owner-approved performance baselines pass |

Closure requires zero UI-to-Rendering/Runtime includes, zero
Rendering-to-UI/Runtime includes, a fixed-capacity backend-neutral UI draw
stream, Runtime/Render-owned submission and GPU lifetime, a standalone UI
build, mandatory dependency enforcement, unchanged visual behavior, mapped
DX12/UI/performance/full gates, and one independent review.

## Architecture Follow-Up Campaign Round 4 (2026-07-25)

Source: the owner-requested engine architecture review of 2026-07-25 at main
tip `c670e95f` (chat review; the plan documents carry the dated file:line
evidence). The review found the class-level god objects closed and the
top-level owner hierarchy sound, and named the two structural debts this
campaign owns plus one amendment set:

1. **Replay accretion** — `Runtime/Replay` is 72 files / 36,900 lines
   (18.5% of `SkullbonezSource`) and has absorbed orbital-planning product
   features (trip planner, porkchop panel, guide arcs, intercept readout)
   plus the whole prediction domain, because features that consume predicted
   data default into the package that owns prediction.
2. **Downward domain bleed** — the dependency proofs police include
   direction, not concept ownership: replay-prediction trajectory semantics
   (19-float segment layout, feature capacities, continuation repair) live in
   `Rendering/RenderCommandTypes.h` and the DX12 backend; Physics includes
   `World/Terrain.h` and stores a borrowed `Geometry::Terrain*` in every
   body record through an unpoliced Physics→World edge; and five fluid/
   buoyancy fields sit in the universal `PhysicsBodyRecord` for every body in
   every scene.
3. **UI plan amendments** — accepted improvements embedded into
   `ui-renderer-hard-boundary` without changing its task count.
4. **Header claim staleness** (registered 2026-07-25 from
   `../Reports/2026-07-25/header-comment-staleness-audit.md`) — the comment
   regime catches deleted symbols but not moved responsibilities. A stale
   `RenderGraph.h` header caused the architecture review to report a false
   "half-finished render graph" finding and nearly registered a six-task
   campaign to rebuild shipped work. The audit found a phantom `RunInput`
   owner in 14 comments across 9 files with zero declarations anywhere,
   three further falsified ownership claims, and 12 broken `Related:`
   pointers — four of which cite `TODO/` plans that inventory rule 4 deletes
   at closure, making them broken by construction.
5. **Process-generated complexity** (registered 2026-07-25 as the campaign's
   fifth plan) — the repository's own rules extrude wide transactions into
   an unbanned but worse shape. The scene-load transaction is the flagship:
   four sibling input/participant structs, an 11-parameter
   `ApplySceneLoadRuntimeReactions`, an 8-parameter
   `ApplySceneLoadPresentationOutputs` that must follow it, and two inline
   arbitration helpers whose comments carry the mid-batch which-value-wins
   rule. No type owns the sequencing or arbitration invariant, and four call
   sites re-encode the ordering by hand.
6. **Concrete parameter-bag elimination** (registered 2026-07-26 from the
   owner-requested current-tree audit) - 22 registered aggregate shapes plus
   eight PB0-added repair rows
   repeatedly flatten concrete owners, repack the same fields between layers,
   or bundle unrelated services for one operation. The scope includes Scene
   save/load, pointer routing, render/UI composition, Replay capture/focus/
   restore, and eight Physics stage contexts. The endpoint is concrete
   owner-produced values, focused direct operations, or concrete
   phase-checked transactions. Inheritance and interfaces are forbidden.

Owner decisions ratified at registration: the 2026-07-22 replay ruling stands
(replay remains the flagship subsystem; the partition is an ownership/naming
correction, not a slimming exercise, and performs zero deduplication);
enforcement is directional dependency rules, symbol-deletion checks, and
placement review rules only — no frozen counts, line budgets, or spelling
ratchets; the UR5 dependency validator is the single shared enforcement
vehicle, and RS4/DB4 extend it with rule data plus fixtures rather than new
mechanisms; physics relocations (terrain boundary, buoyancy store) are
byte-exact-or-revert — the bounded-divergence allowance does not apply. For
the fourth plan: the invariant-ownership test is the standing arbiter between
a banned authority-free bag and a legitimate invariant owner; the amendment
sharpens that test and repeals none of the context-bag, callback, reach-back,
or forwarding bans. RG1 of the 2026-07-28 principal-feedback campaign
subsequently replaced the former 12-parameter ceiling with an exact-current
qualitative ruling trigger; no counting or spelling ratchet was added.

Standing rules binding every plan in this campaign: zero behavioral baseline,
golden, screenshot, replay artifact, scene, config, or physics CSV refresh —
divergence is reverted, never normalized; every DX12 slice runs the bounded
graphics-stress proof per inventory rule 10; replay-facing slices run the
one-invocation mega gate per inventory rule 11; one independent rubber-duck
review per plan at closure; no new compatibility spellings, forwarding
headers, context bags, callback packs, or hot-path inheritance artifacts.
The original binding order was 1 UI/renderer hard boundary, 2 header claim
staleness remediation, 3 replay subsystem partition, 4 downward domain bleed
remediation (DB1 additionally requires RS1's `Runtime/Prediction` package to
exist), 5 invariant-ownership governance and transaction repair. The
2026-07-26 owner direction appends 6 concrete parameter-bag elimination after
GV4; GV2 supplies its scene-load transaction and the later plan owns closure
of the complete 30-row census plus three assigned ceiling defects. The two
authorized documentation-only exceptions are complete: GV0 landed during the
UI campaign, and HC0 ran beside the UI tail. HC1-HC2 then closed after UR6 as
sequenced. HC2 preserved GV0's aggregate-invariant procedure and added claim
verification as a separate step. Replay and Downward Domain Bleed are complete;
GV1 ratified the offender census, GV2 installed the scene-load transaction,
GV3 installed the generated-scene transaction, GV4 closed governance, and PB0
ratified the implementation-tip parameter-bag census. PB1 is the binding next
task. HC2 and
GV0 both amended
`Agentic/Skills/comment-style-audit/skill.md` in separate sections; HC2 landed
second and preserved GV0.

| Plan | State | Verified phase count | Start condition / next action |
|---|---|---:|---|
| [ui-renderer-hard-boundary](../Reports/2026-07-25/ui-renderer-hard-boundary-closure.md) | Complete | 7/7 | Closed and removed from the live ledger under inventory rule 4 |
| [header-claim-staleness-remediation](../Reports/2026-07-25/header-claim-staleness-remediation-closure.md) | Complete | 3/3 | Closed and removed from the live ledger under inventory rule 4 |
| [replay-subsystem-partition](../Reports/2026-07-26/replay-subsystem-partition-closure.md) | Complete | 6/6 | Closed and removed from the live ledger under inventory rule 4 |
| [downward-domain-bleed-remediation](../Reports/2026-07-26/downward-domain-bleed-remediation-closure.md) | Complete | 6/6 | Closed and removed from the live ledger under inventory rule 4 |
| [invariant-ownership-governance-and-transaction-repair](../Reports/2026-07-26/invariant-ownership-governance-and-transaction-repair-closure.md) | Complete | 5/5 | Closed and removed from the live ledger under rule 4 |
| [concrete-parameter-bag-elimination](../Reports/2026-07-26/concrete-parameter-bag-elimination-closure.md) | Complete | 8/8 | Closed and removed from the live ledger under inventory rule 4 |

## Architecture Follow-Up Campaign Round 5 (2026-07-26)

Source: the owner-requested from-source engine architecture review of
2026-07-26 on `nightrunner-26th-JUL-26` at tip `35f6de4e`. The review read only
source and tests — no plan files, no reports, no git history — so its findings
are independent of the campaign record. Each plan document carries the dated
file:line evidence.

The review found the class-level god objects closed, the layering real, the
determinism apparatus genuinely strong (zero `throw`, zero `virtual`, zero
`std::function` across ~180K lines; `#pragma fp_contract(off)` force-included;
worker-count-invariant byte-exact tests), and named two structural debts plus
the owner's two same-day requests:

1. **Governance enforced by shape rather than by ownership.** The aggregate bans
   are satisfied by renaming. 99 aggregate parameter types match the banned
   suffix families; six carry one member, two of those wrap the *same* single
   reference, and one is passed by value where a plain reference would be
   shorter. Four physics translation units preserve lifted god-class bodies by
   rebinding parameters to `m_`-prefixed locals, which no checker can see.
   `RuntimeFrameViews.h:24` states "No capability slice spans the complete frame
   surface" and then declares four views totalling 23 references that two
   operations receive in full. The two independent-review skills that `AGENTS.md`
   delegates enforcement to contain no aggregate criteria at all.
2. **Fixed-capacity policy applied to the wrong containers, at the wrong
   scale.** `PhysicsFixedList` fails loud at capacity and is used by 25 members
   under `Physics/`; forty other members are plain `std::vector` that grow
   silently, two of them `reserve`d inside the fixed tick. Separately,
   `ColliderStore::m_colliders` is 7,228 × 8,192 = **56.5 MiB resident in every
   scene** because `CollisionShape` is a variant sized by `ConvexHullShape`'s
   inline 64-vertex / 96-face / 160-edge / 1,536-index arrays, so a sphere pays
   for a hull. Prediction's second engine pays it again.
3. **Owner request — scene-sized capacity.** Stores size from the loaded scene;
   a scene that fits allocates nothing; a larger scene allocates once at load.
   Owner decision ratified at registration: `MAX_SCENE_OBJECTS = 8192` is
   retained as an absolute fail-loud ceiling, capacity is monotonic within a
   process, and there is no shrink path or deferred follow-up for one.
4. **Owner request — closer memory tracking.** Per-owner capacity, live count,
   and session high-water, surfaced in the memory tab and dumped at scene
   unload. `RuntimeReserveAllocator` already carries the registry and
   `PhysicsFixedList` already tracks `m_highWater` and discards it. A committed
   memory budget gate and per-frame allocation breakpoints were considered and
   rejected at registration.

Owner decisions ratified at registration: one plan per review finding, with
trivial one-line items folded into their owning plan rather than given their own
ledger row (`FenceHandle`, the 20 `mutable` hot arrays, and the
`PhysicsFixedList` container defects join `scene-sized-store-capacity`). The
`render-interface-retirement` ruling stands — no plan may reintroduce a render
interface, virtual dispatch, or type erasure. Every PB0 and GV1 explicit retain
ruling is carried forward untouched. No plan may refresh a physics, replay,
visual, or DX12 baseline; Following the 2026-07-27 rulings below, no plan
requires owner divergence authority and the campaign is byte-exact throughout.

| # | Plan | State | Verified phase count | Start condition / next action |
|---:|---|---|---:|---|
| 1 | [governance-shape-to-judgment-conversion](../Reports/2026-07-27/governance-shape-to-judgment-conversion-closure.md) | Complete | 6/6 | Closed 2026-07-27 and removed from the live ledger under rule 4 |
| 2 | [scene-sized-store-capacity](../Reports/2026-07-27/scene-sized-store-capacity-closure.md) | Complete | 8/8 | Closed 2026-07-27 and removed from the live ledger under rule 4; 98 retained rows are scene-committed and the final 200-body payload is 89.06% smaller |
| 3 | [store-capacity-memory-reporting](../Reports/2026-07-27/store-capacity-memory-reporting-closure.md) | Complete | 4/4 | Closed 2026-07-27 and removed from the live ledger under rule 4; 95 sorted production rows expose capacity/live/session peaks, and the three-scene handoff identifies the retained contact-bound candidates |
| 4 | [ceremonial-aggregate-elimination](../Reports/2026-07-27/ceremonial-aggregate-elimination-closure.md) | Complete | 5/5 | Closed 2026-07-27 and removed from the live ledger under rule 4; all 35 couriers are gone, independent no-bag review is clear, and the default full gate passes byte-exact baselines |
| 5 | [runtime-frame-view-retirement](../Reports/2026-07-27/runtime-frame-view-retirement-closure.md) | Complete | 4/4 | Closed 2026-07-27 and removed from the live ledger under rule 4; all four views and rejected broad wrappers are gone, review is clear, and every mapped gate passes |
| 6 | [render-backend-service-bag-removal](../Reports/2026-07-27/render-backend-service-bag-removal-closure.md) | Complete | 4/4 | Closed 2026-07-27 and removed from the live ledger under rule 4; the eleven-pointer bag is deleted, optional capability presence is explicit, and all final gates pass without baseline refresh |
| 7 | [extraction-scar-remediation](../Reports/2026-07-27/extraction-scar-remediation-closure.md) | Complete | 3/3 | Closed 2026-07-27 and removed from the live ledger under rule 4; 88 repairs removed and the sole WorkerPool retain preserved |
| 8 | [scene-runtime-verb-partition-consolidation](../Reports/2026-07-27/scene-runtime-verb-partition-closure.md) | Complete | 4/4 | Closed 2026-07-27 and removed from the live ledger under rule 4; seven verb units are gone, residual names match owners/domains, and review plus all final gates are clear |
| 9 | [operator-command-invariant-ownership](../Reports/2026-07-27/operator-command-invariant-ownership-closure.md) | Complete | 4/4 | Closed 2026-07-27 and removed from the live ledger under rule 4; one value-only transaction owns phase order, arbitration, and the ledger, and every final gate is clear |
| 10 | [coverage-gate-test-reorganization](../Reports/2026-07-27/coverage-gate-test-reorganization-closure.md) | Complete | 3/3 | Closed 2026-07-27 and removed from the live ledger under rule 4; subsystem-owned tests preserve every assertion and exact coverage result |
| 11 | [allocator-foreign-pointer-safety](../Reports/2026-07-27/allocator-foreign-pointer-safety-closure.md) | Complete | 3/3 | Closed 2026-07-27 and removed from the live ledger under rule 4; guarded complete-header provenance, zero foreign frees, measured performance, and independent review are clear |
| 12 | [sbresult-frame-path-cost](../Reports/2026-07-27/sbresult-frame-path-cost-closure.md) | Complete | 3/3 | Closed 2026-07-27 and removed from the live ledger under rule 4; sentinel-only success preserves all failure diagnostics, review is clear, and every mapped gate passes |
| 13 | [terrain-legacy-and-contact-seed-remediation](../Reports/2026-07-27/terrain-legacy-contact-seed-remediation-closure.md) | Complete | 5/5 | Closed 2026-07-27 and removed from the live ledger under rule 4; axes and bounds are explicit, the ratified seed is fully pinned, review is clear, and every final gate passes byte-exact |
| 14 | [new-aggregate-ruling-gate](../Reports/2026-07-27/new-aggregate-ruling-gate-closure.md) | Complete | 3/3 | Closed 2026-07-27 and removed from the live ledger under rule 4; 86 bounded rows are ruled, the transition is unusable, bypass review is clear, and all CPU gates pass |

## Principal Engineer Feedback Campaign (2026-07-28)

Source: owner-supplied principal-engineer review verified against main tip
`0768593d` on `nightrunner-28th-JUL-26`. The bounded immediate response fixes
only behavior-neutral, locally provable items. Ownership, ABI, lifetime,
performance, and governance redesigns remain explicit plans.

Binding owner directions:

- Retain the Physics body SoA for the completed campaign. A future AoS proposal
  may proceed only if representative evidence shows no meaningful degradation
  relative to SoA. **Owner question before such work begins:** should
  "meaningful" mean more than 2%, more than 5%, or only a statistically
  significant regression?
- The warm-start key capacity guard was evaluated against all three proposed
  dimensions and accepted. Physics remains byte-exact; DX12 and focused-bench
  Physics/solver markers remain inside absolute and relative performance
  policy; review is clear. The exact evidence is in
  `../Reports/2026-07-28/persistent-contact-key-capacity-closure.md`, and no
  baseline was refreshed.
- Plan 2 replaces the hard 12-parameter ceiling with mandatory qualitative
  ownership review and reopens every current exact-12 row. No implementation
  may hide a wide operation in a courier, forwarding facade, capability slice,
  callback pack, or renamed context.

| # | Plan | State | Verified phase count | Start condition / next action |
|---:|---|---|---:|---|
| 1 | [physics-body-hot-layout-evidence](../Reports/2026-07-28/physics-body-hot-layout-closure.md) | Complete | 4/4 | Closed 2026-07-28 and removed from the live ledger under rule 4; SoA retained, inert control-block alignment removed, and final review/gates clear |
| 2 | [replay-restore-wide-signature-governance](../Reports/2026-07-28/replay-restore-wide-signature-governance-closure.md) | Complete | 4/4 | Closed 2026-07-28 and removed from the live ledger under rule 4; five Replay repair rows are gone, terminal state is enforced by the transaction, review is clear, and all gates pass without refresh |
| 3 | [physics-fixed-list-copy-contract](../Reports/2026-07-28/physics-fixed-list-copy-contract-closure.md) | Complete | 3/3 | Closed 2026-07-28 and removed from the live ledger under rule 4; implicit transfers are deleted, production construction/seeding is exact-owner scoped, lifecycle and phase proofs pass, and independent review is clear |
| 4 | [sbresult-compact-success-path](../Reports/2026-07-28/sbresult-compact-success-path-closure.md) | Complete | 4/4 | Closed 2026-07-28 and removed from the live ledger under rule 4; 16-byte carrier, exact diagnostics, 252/256 conservative bound, performance/full gates, and independent review are clear |
| 5 | [vector-dot-product-api](../Reports/2026-07-28/vector-dot-product-api-closure.md) | Complete | 3/3 | Closed 2026-07-28 and removed from the live ledger under rule 4; 173 configuration-complete uses are explicit, arithmetic order is pinned, and every mapped gate passes |
| 6 | [determinism-terrain-fixture-isolation](../Reports/2026-07-28/determinism-terrain-fixture-isolation-closure.md) | Complete | 3/3 | Closed 2026-07-28 and removed from the live ledger under rule 4; all fixture predecessor transitions, randomized determinism orders, byte-exact/deep Physics, full gates, and independent review are clear |
| 7 | [dependency-proof-generation](../Reports/2026-07-28/dependency-proof-generation-closure.md) | Complete | 3/3 | Closed 2026-07-28 and removed from the live ledger under rule 4; planted drift, parser limits, exact project ownership, independent review, and all mapped gates pass |

## Fresh-Read Engine Review Campaign (2026-07-29)

Source: source-and-tests-only engine review at main tip `90e4d52f`, deliberately
conducted without reading `Agentic/` or commit history so the findings reflect
what a new reader sees in the code alone. Evidence is dated 2026-07-29 and
recorded in each plan; no historical measurement is reused.

The review's positive findings are recorded here so later work does not
regress them: the persistent contact solver's Catto citation discipline, the
force-included FP-contraction contract, canonical broadphase pair emission,
disjoint-set narrowphase island equivalence, and the registered-owner allocation
policy are all load-bearing and must survive every plan in this campaign.

Binding owner directions:

- Plan 7 is last. It is the only campaign plan permitted to regenerate a
  baseline, and QN4 is a hands-on owner visual acceptance gate: no baseline file
  may change before that checkbox is recorded with its reviewed scene list.
- Plans 1-6 are strictly byte-exact. A differing physics byte in any of them
  means the change altered evaluation rather than expression, and the task is
  reverted rather than baselined. None of them carries a bounded-divergence
  allowance.
- Plan 2 adds a fourth repeatable inventory measuring function extent. Like the
  existing three, no threshold in it is an allowance, budget, or ratchet, and a
  ruling records a judgement rather than managing a number.

Dependency barriers:

- Satisfied: plan 1 closed before plan 5; both edit `SpatialGrid`, and the guard
  landed first.
- Plan 2 CX1 before plan 3 CS4 — CS4 clears the `repair-plan` rulings CX1 seeds.
  Plan 3 CS0-CS3 may run earlier.
- Plan 5 before plan 6 — plan 6 breaks the include chain that reaches
  `SpatialGrid.h`, which plan 5 is still reshaping.

| # | Plan | State | Verified phase count | Start condition / next action |
|---:|---|---|---:|---|
| 1 | [broadphase-canonical-order-guard](../Reports/2026-07-29/broadphase-canonical-order-guard-closure.md) | Complete | 2/2 | Closed 2026-07-29 and removed from the live ledger under rule 4; compile-time capacity guards, both ceiling-order paths, byte-exact Physics, performance, ownership inventories, and independent review are clear |
| 2 | [function-complexity-review-trigger](../Reports/2026-07-29/function-complexity-review-trigger-closure.md) | Complete | 3/3 | Closed 2026-07-29 and removed from the live ledger under rule 4; 40/40 current-body rulings, fail-closed fixtures, governance, mapped validation, and independent review are clear |
| 3 | [contact-solve-phase-ownership](../Reports/2026-07-29/contact-solve-phase-ownership-closure.md) | Complete | 5/5 | Closed 2026-07-29 and removed from the live ledger under rule 4; guarded phase ownership, byte-exact Physics, ordered parser dispatch, all closure gates, 9/9 comment audit, and independent review are clear |
| 4 | [collision-hull-shape-instancing](../Reports/2026-07-29/collision-hull-shape-instancing-closure.md) | Complete | 4/4 | Closed 2026-07-29 and removed from the live ledger under rule 4; 57.4803% measured acceptance-scene hull-store reduction, byte-exact Physics, stable narrowphase markers, 18/18 comment audit, all closure gates, and independent review are clear |
| 5 | [broadphase-capacity-right-sizing](../Reports/2026-07-29/broadphase-capacity-right-sizing-closure.md) | Complete | 4/4 | Closed 2026-07-29 and removed from the live ledger under rule 4; exact 10.170568x acceptance reduction, nine SceneLoad-only owners, byte-exact Physics, all closure gates, 9/9 comment audit, and independent review are clear |
| 6 | [runtime-include-closure-reduction](../Reports/2026-07-29/runtime-include-closure-reduction-closure.md) | Complete | 4/4 | Closed 2026-07-29 and removed from the live ledger under rule 4; heavy closure, zero non-Physics solver reach, exact accounting, mapped gates, 30/30 comment audit, and independent review are clear |
| 7 | [quaternion-convention-normalization](../Reports/2026-07-29/quaternion-convention-normalization-closure.md) | Complete | 6/6 | Closed 2026-07-29 and removed from the live ledger under rule 4; canonical Hamilton math, scene v3/replay v5 migration, prior owner visual acceptance, inspected baseline regeneration, all mapped gates, 41/41 comment audit, and independent review are clear |
| 8 | [box-vibration-and-warm-start-integrity](../Reports/2026-07-31/pre-536-physics-oracle-restoration.md) | Superseded | 0/7 accepted | Owner rejected the `536e0a60` golden transition on 2026-07-31; BV1/BV2/BV3/BV5 behavior and all four changed physics goldens are restored to the pre-campaign policy/oracle without rewriting history |
| 9 | [persistent-contact-convergence-early-out](../Reports/2026-07-30/persistent-contact-convergence-early-out-closure.md) | Complete | 4/4 | Closed 2026-07-30 and removed from the live ledger under rule 4; owner-approved retain decision preserves the honest row-level stopping criterion, exact diagnostics, deterministic repeats, and final-source validation |

### Plan 8 Ordering Consequences

Plan 8's own section below carries its owner directions, evidence, and
superseded ruling. Two consequences follow specifically from placing it last,
and BV0 must account for both:

- **Its evidence aged by two baseline transitions.** Every measurement in plan 8
  was taken at tip `0768593d`; the plan already warns that line numbers moved at
  `90e4d52f`, and plan 7 moved physics baselines again before plan 8 starts.
  Inventory Rule 7 applies twice over. BV0's T0 harness must be recorded against
  the post-plan-7 tree, not against the numbers in the plan body, and the
  feature-ID bit layout must be re-confirmed against the current key schema.
- **The contact-identity regression must survive 34 intervening tasks.** It
  fails 338 assertions without the SAT fix and is the most reusable artifact of
  the investigation. It is now preserved at
  `origin/codex/contact-identity-regression-29th-jul-26` commit `27906417`.
  Its stale source/baseline companions were not imported, and the original
  stash was dropped after the preservation branch was pushed.

## Box Vibration And Warm-Start Integrity (2026-07-29)

Source: owner-reported box vibration in `prediction_ragdoll_wall_200`,
investigated 2026-07-27/28 at main tip `0768593d` with SkullScope. The owner's
two stated goals are to stop the boxes vibrating and to make solver warm
starting stop being a hack.

The investigation is complete and the cause is measured: object/object
restitution fires on contacts whose pre-solve closing speed is dominated by the
rotational `omega x r` term inside a dense pile rather than by a genuine impact.
Disabling that branch cut bounce oscillations 91%, cut the warm-start cache miss
rate from 12.8% to 2.4%, and let the solver reach its convergence early-out for
the first time in that scene. Three competing hypotheses were tested and
falsified; they are recorded in the plan so they are not re-investigated.

Binding owner directions:

- **Terrain restitution does not change.** It is what brings rolling balls to a
  stop. Only the object/object branch is in scope.
- **This plan is not byte-exact and carries a bounded-divergence allowance.**
  BV1-BV3 deliberately change contact response and will move physics baselines.
  This is the only currently registered plan permitted to do so; the Fresh-Read
  campaign remains strictly byte-exact.
- **BV3 supersedes the 2026-07-27 Round 5 ruling** that rejected replacing the
  terrain contact seed. That ruling rejected it "for no visible gameplay gain";
  a visible gameplay motivation now exists. BV3 is droppable without affecting
  the vibration fix.

Sequencing, resolved 2026-07-29: this plan and Fresh-Read plan 3
(`contact-solve-phase-ownership`) both edit `PersistentContactSolver.cpp`, and
that plan is strictly byte-exact while this one is not, so **they must not run
concurrently.** The owner placed this plan after
`quaternion-convention-normalization`, which satisfies that requirement by
construction: plan 3 closes long before this one starts, and BV1/BV3 then
operate on a decomposed solver rather than inside the 1,721-line body.

This plan is tracked as **plan 8 of the Fresh-Read Engine Review Campaign**
above; it does not carry a second ledger row. Its start condition, ordering
consequences, and the `stash@{0}` recovery note live there. The owner directions
and superseded ruling above remain the authority for how it is executed.

## Claim Integrity Campaign (2026-07-30)

Source: source-and-tests-only engine review at tip `91a8403d` on 2026-07-30,
deliberately conducted without reading `Agentic/`, plan files, reports, or
commit history, so the findings reflect what a new reader sees in the code
alone. Evidence is dated 2026-07-30 and recorded in each plan; no historical
measurement is reused.

**Campaign thesis.** Every finding is the same shape: an artifact states a
property the built code does not deliver. `SKULLBONEZ_CORE.vcxproj` declares a
JSON abort policy the test binary does not receive.
`Core/FloatingPointContract.h` states "every project, every configuration" while
seven translation units override it away. `Maths/GeometricMath.h` publishes three
functions nothing calls. `Runtime/Camera/Camera.cpp:462` says pitch caps cannot
flip through the up axis, and a NaN makes them.
`Rendering/DX12/Dx12DeferredReleaseOwner.cpp:41` prints a high-water value the
type never records. Each plan proves one stated claim is actually delivered, or
corrects the claim.

The review's positive findings are recorded here so later work does not regress
them: the force-included FP-contraction contract and `/fp:precise`, the
worker-count-invariant byte-exact determinism suite, `PhysicsFixedList`'s
placement-new/`std::launder`/`move_if_noexcept` lifetime handling and its English
capacity-reason strings, the fence-proven DX12 retirement quarantine, the
`PERSISTENT_CONTACT_BODY_MASK` static assertion tying the solver key to
`MAX_SCENE_OBJECTS`, and the Catto page-and-equation citation discipline with
explicit `ENGINE-SPECIFIC` deviation markers. All are load-bearing and must
survive every plan in this campaign.

**Structural finding recorded, not scheduled.** The review also measured Runtime
at 119,342 of 230,923 first-party source lines — 52% of the engine, against
30,503 Physics and 31,754 Rendering. Excluding Replay, Prediction, and Planning
still leaves roughly 86,000 lines. `Run.h` is genuinely clean at 304 lines with
no state bag, so the god object was really dismantled; the observation is that
the dependency rules police direction and nothing polices mass, and the Runtime
allow-table admits App to all 20 packages. That is a design question for an owner,
not a defect with a mechanical repair, and it is deliberately **not** registered
as a plan here. It is recorded so the next review does not re-derive it.

Binding owner directions:

- No plan in this campaign carries a bounded-divergence allowance. All five
  registered plans are strictly byte-exact for physics. A differing physics
  byte means the change
  altered evaluation rather than configuration or domain guarding, and the task
  is reverted rather than baselined.
- `inverse-trig-domain-guards` inverts the usual reading of a byte difference.
  A clamp that never fires is byte-exact by construction, so a CSV difference
  there proves the clamp *is* firing in a baseline scene — a live out-of-domain
  value in accepted artifacts. That is a defect discovery to escalate, never a
  baselining opportunity.
- Plans 1 and 2 each add a repeatable inventory, bringing the total from four to
  six. Like the existing four, no threshold in either is an allowance, budget, or
  ratchet, and a ruling records a judgement rather than managing a number.
- Owner ruling for plan 2: delete the unreachable `GeometricMath` surface rather
  than harden it. Hardening would retain a public maths API with no runtime
  caller. MR0 may escalate a specific symbol back only on evidence that a live
  caller was intended and lost its edge in a refactor.
- Plan 2 MR2 registered plan 5 after the first compiler-backed inventory exposed
  a repository-wide population that cannot be adjudicated honestly inside the
  focused Maths plan. Its exact rows are current judgements, never allowances.

Dependency barriers:

- Plan 1 BP1 before plan 2 MR1 — BP1 changes which engine TUs the test binary
  compiles, so MR1's coverage-floor recheck must measure the post-BP1 tree.
- Plan 2 MR1 before plan 3 TD1 — MR1 deletes
  `GeometricMath::GetHeightFromPlane`, which holds one of the open inverse-trig
  sites. Running TD1 first would harden code that is about to be removed.
- Plan 4 has no barrier in either direction and is sequenced last only because it
  is the smallest and lowest-risk.
- Plan 5 starts after the four originally registered plans. Their source changes
  must settle before the repository-wide reachability population is adjudicated.

| # | Plan | State | Verified phase count | Start condition / next action |
|---:|---|---|---:|---|
| 1 | [build-configuration-parity](../Reports/2026-07-30/build-configuration-parity-closure.md) | Complete | 6/6 | Closed 2026-07-30 and removed from the live ledger under rule 4; production JSON semantics, inherited forced includes, exact rulings, clear review, and all final gates complete |
| 2 | [maths-surface-reachability](../Reports/2026-07-30/maths-surface-reachability-closure.md) | Complete | 4/4 | Closed 2026-07-30 and removed from the live ledger under rule 4; eight dead definitions removed, 407 exact follow-up rulings registered, review clear, coverage and all final gates pass |
| 3 | [inverse-trig-domain-guards](../Reports/2026-07-30/inverse-trig-domain-guards-closure.md) | Complete | 4/4 | Closed 2026-07-30 and removed from the live ledger under rule 4; one shared clamp policy, explicit Camera and antiparallel fallbacks, focused finite-output tests, byte-exact Physics, clear review, and all final gates complete |
| 4 | [retirement-diagnostic-honesty](../Reports/2026-07-30/retirement-diagnostic-honesty-closure.md) | Complete | 2/2 | Closed 2026-07-30 and removed from the live ledger under rule 4; truthful bounded retirement accounting, corrected readback diagnostics, fatal probes, clean DX12 baselines, one-minute graphics stress, clear review, and all final gates complete |
| 5 | [unreachable-symbol-remediation](../Reports/2026-07-30/unreachable-symbol-remediation-closure.md) | Complete | 4/4 | Closed 2026-07-30 and removed from the live ledger under rule 4; 181 unreachable functions removed, 79 exact retain rulings remain, Automation/Debug/Profile reachability is mandatory, review is clear, and every final gate passes byte-exact Physics |

### Governance Gap This Campaign Closes

The four original findings share a cause worth naming, because it predicts where the next
one appears. Everything mechanically enforced in this repository is at 100%:
all 573 first-party `SkullbonezSource` `.cpp`/`.h`/`.hpp`/`.inl` files carry a
learning header, and that source has zero `throw`, zero `catch`, zero `goto`,
zero `TODO`/`FIXME`/`HACK`, and one `const_cast`. Everything enforced only by review sits at whatever the last
reviewer happened to look at — and all four original findings are review-only.

Plans 1 and 2 convert two of those review-only rules into inventories:
`check_build_config_consistency.py` reports files compiled under divergent
settings, and `inventory_unreachable_symbols.py` reports symbols with no
non-test caller. Both follow the established unruled-fails/ruled-passes contract
and neither introduces a threshold. Plan 5 owns the repository-wide remediation
population discovered by the second inventory. Plans 3 and 4 stay
review-enforced because
their subject matter has no honest mechanical proxy; a regex that flags every
`acosf` without an adjacent `clamp` would fire on the four correctly guarded
sites and miss a guard placed three lines earlier.

## Gate Blind Spot Campaign (2026-07-31)

Source: source-and-tests-only engine review at tip `1967a863` on 2026-07-31,
deliberately conducted without reading `Agentic/`, plan files, reports, audits,
or commit history, so the findings reflect what a new reader sees in the code
alone. Evidence is dated 2026-07-31 and recorded in each plan; no historical
measurement is reused.

**Campaign thesis.** The previous campaign named the gap between mechanically
enforced rules (100%) and review-only rules (whatever the last reviewer looked
at). This campaign names the next one: **a passing gate proves consistency, not
correctness and not cost.** Every finding is something the existing gates
reproduce perfectly and therefore cannot see.

- `ApplyPendingImpulse` divides a world torque by body-frame inertia. It is
  deterministically wrong, so byte-exact CSV baselines lock the wrong value in
  and re-prove it every run.
- The solver pipeline trace runs in the innermost PGS loop in every
  configuration to produce a `uint16_t` count. Correctness gates are silent
  because nothing is wrong; the perf gate is silent because the cost has always
  been there.
- Mutual gravity re-derives its entire triangular predicate set on the main
  thread. The determinism suite passes precisely because the redundant pass is
  faithful.
- A frame phase that returns Lane R failure without latching it exits `0`. Every
  current phase latches, so no gate fires; nothing enforces that the next one
  will.
- 570 of 576 files carry a glossary block, with `Draw command` defined 46 times
  and `Broadphase` 30 times. The header-presence rule is at 100%; the
  information content is not what the rule measures.

The review's positive findings are recorded so later work does not regress them:
the pair-force table that makes mutual-gravity accumulation order invariant
under worker scheduling (`Stages/PhysicsForceStage.cpp:344`), the
worker-count-invariant byte-exact determinism suite, the `CATTO REF:` /
`ENGINE-SPECIFIC:` citation-and-deviation discipline, the SAT plus
Sutherland-Hodgman narrowphase with feature-ID warm-start keys and the measured
25% cross-family challenger margin, fence-proven DX12 retirement quarantine,
`PhysicsFixedList`'s named English capacity reasons in its fatal diagnostics,
handle-and-generation body identity with stale-hint fallback, the 80-line
phase-named `Run::Execute` with RAII allocation-phase scopes, and zero
`TODO`/`FIXME`/`HACK` at `/W4` with warnings-as-errors. All are load-bearing.

**Structural findings recorded, not scheduled.** The 20-stream SoA hot-field
body store is paid for and not cashed in: `LoadPhysicsBodyHotState` touches 18
streams per body, physics contains no SIMD (the only `_mm_*` in the tree is
`Matrix4::operator*`), and adding a hot field costs edits in roughly eight
places including a clone macro. The owner is aware and has explicitly excluded
it from this campaign. Also recorded: 57 `*View` types and 138 owning
`std::vector` members in Runtime/Physics/Rendering headers against a stated
global zero-allocation-by-default policy. Neither is registered as a plan.

Binding owner directions:

- Plans 1, 2, and 3 are strictly byte-exact and land without an owner decision.
  Plan 1 must not move a replay hash, plan 2 must not move a physics byte, and
  plan 3 is documentation-only.
- Plan 4's AI4 owner gate was accepted on 2026-08-01 after its zero-delta proof.
  It did **not** receive task-scoped
  bounded-divergence authority: AI0 proved the existing artifacts do not reach
  the defect, so AI1 and AI2 must leave them byte-exact. Any movement blocks
  closure and reopens the census or implementation; no regeneration is
  authorized.
- Plan 1 explicitly rejects "compile the trace out of Release." Removing it from
  Release alone leaves the cost in Profile, which is where `validate_perf`
  measures, and compiling it out of Profile would move every replay sample hash
  because `pipelineRecordCount` is hashed at `ReplayRecorder.cpp:1576`, `:1815`,
  and `:2033` while replay fidelity validates from the Profile test binary. The
  accepted design keeps the count exact everywhere and deletes only the payload
  work, so inventory rule 11 is never engaged.
- Plan 3 retains the `Summary:` field by owner ruling; the original review
  proposed removing it. Only `Glossary` handling changes, plus repair of Summary
  lines that restate the filename. The shared glossary is
  `Agentic/Reference/engine-glossary.md`, not `Core/Common.h`, because that
  header states an invariant against regaining domain content and `Core`
  defining Rendering and Physics vocabulary would invert the mechanically
  enforced dependency direction.
- Plan 3 adds a sixth repeatable inventory. Like the existing five, no count in
  it is an allowance, budget, or ratchet.
- Plan 4 AI3 is investigation-only and registers follow-up plans rather than
  changing behavior, preserving AI0's zero-delta oracle through AI4.
- Plan 5 was the registered AI3 follow-up. It completed without reaching a
  committed artifact or exercising baseline-refresh authority.

Dependency barriers:

- Plan 4 AI1 before AI2 — AI1 must prove it moved zero baseline bytes before
  AI2 applies the focused correction and independently proves the same
  zero-artifact result. Reversing the order destroys the separate neutrality
  evidence, which is the reason both findings share one plan.
- Plan 4 AI0 before AI2 — AI0 predicts the expected baseline delta before any
  production code changes. That prediction is the oracle AI4 verifies against;
  producing it after the fact proves nothing.
- Plan 1 before plan 4 — both touch physics byte-exactness. Landing plan 1's
  strictly-neutral change first means plan 4's AI1 neutrality proof is measured
  against a settled tree.
- Plans 2 and 3 have no barrier in either direction and may run at any point.
- Plan 4 AI4 before plan 5 — the original pending-impulse zero-delta correction
  must be accepted independently before any newly discovered convention is
  changed.

| # | Plan | State | Verified phase count | Start condition / next action |
|---:|---|---|---:|---|
| 1 | [solver-diagnostic-hot-path-cost](../Reports/2026-07-31/solver-diagnostic-hot-path-cost-closure.md) | Complete | 4/4 | Closed and removed from the live ledger under rule 4; exact artifacts and the measured Profile win are recorded |
| 2 | [runtime-contract-hygiene](../Reports/2026-07-31/runtime-contract-hygiene-closure.md) | Complete | 3/3 | Closed and removed from the live ledger under rule 4; exit, Quaternion, and zero-throw contracts are closed |
| 3 | [engine-glossary-consolidation](../Reports/2026-07-31/engine-glossary-consolidation-closure.md) | Complete | 4/4 | Closed and removed from the live ledger under rule 4; canonical glossary, strict inventory, 575-file source pass, and non-tautological summaries are closed |
| 4 | [angular-impulse-frame-correctness](../Reports/2026-07-31/angular-impulse-frame-correctness-closure.md) | Complete | 5/5 | Closed and removed from the live ledger under rule 4; owner accepted the zero-delta proof and post-acceptance gates passed without refresh |
| 5 | [vector-frame-contract-closure](../Reports/2026-07-31/vector-frame-contract-closure.md) | Complete | 5/5 | Closed and removed from the live ledger under rule 4; explicit frames, corrected conventions, unchanged artifacts, final gates, and independent review are clear |

### Governance Gap This Campaign Closes

The previous campaign converted two review-only rules into inventories. This one
addresses a different gap: the repository has strong gates for *drift* and no
gate for *initial wrongness or standing cost*.

Byte-exact baselines are the primary physics contract, and they are excellent at
what they do. They also mean a value that was wrong the first time is re-proven
correct on every run, forever, with increasing confidence. `ApplyPendingImpulse`
is the concrete instance: three paths compute the same physical quantity, two
agree, one has disagreed since it was written, and no gate in the repository is
capable of noticing because all three are deterministic.

Plan 4 AI3 is the structural response — a sweep for other places where a
convention is documented in one direction and implemented in the other, asking
of each whether a byte-exact baseline would hide it. Its output is follow-up
plans, not a new mechanical checker, because the honest mechanical proxy does
not exist: no regex distinguishes a correct frame conversion from an incorrect
one. The counterweight to "deterministic" is a second independent derivation of
the same quantity, which is what AI0's cross-path equivalence test installs and
what future physics work should imitate.

That sweep registered plan 5 for the mixed-frame anisotropic angular-drag
clamp, authored impulse-offset schema, ambiguous test-only vector reflection,
and explicit public descriptor frame matrix. Registration grows this campaign
from 16 to 21 tasks; AI4 acceptance is complete and plan 5 may begin.

Plan 1 records the matching cost lesson. The convergence diagnostics and the
pipeline trace sit in the same loop with the same purpose; one was designed with
an explicit comment about not spending simulation budget on observational work,
and the other was not. Nothing enforced consistency between two sibling
diagnostics, and the shipping binary paid for years.

## Active Plans (2026-08-01)

| # | Plan | State | Verified phase count | Start condition / next action |
|---:|---|---|---:|---|
| 1 | [look-lab-random-style-authoring](TODO/look-lab-random-style-authoring.md) | Active | 1/7 | LL1 implements the pure SplitMix64 generator-v1 candidate and its complete deterministic seed matrix |
| 2 | [contact-energy-and-warm-start-integrity](TODO/contact-energy-and-warm-start-integrity.md) | Registered | 0/7 | After Look Lab closes, ES0 measures the authoritative 4/8/16/32/64/128 tower sweep, four-brick reproduction, and 200-box topple before any solver edit |

## Look Lab Random Style Authoring (2026-08-01)

Source: owner request for an engine-native visual exploration workflow that does
not require a predetermined art direction. The existing standalone style schema
and live application path already expose the right presentation vocabulary, but
there is no coherent random candidate owner or exact paired style/screenshot
save transaction.

Binding owner directions:

- preserve F5 performance-histogram and F6 memory-overlay behavior exactly;
- bind F10 to a deterministic presentation-only reroll and F11 to automatic
  root `LookLab/<datetime>_<seed>/` bundle saving;
- write `look.style.json`, a human-readable `look.txt` containing metadata,
  complete resolved settings and final status, and `look.png` inside that
  ignored directory without modifying the active scene or existing curated
  styles;
- randomize supported cinematic, lighting, sky, fog, volumetric, bloom, grading,
  terrain, water, object-style, color, and material presentation coherently;
- exclude lenses, FOV, camera pose/motion, simulation, assets, transforms, shader
  source/recompilation, renderer resource-quality policy, and debug settings; and
- require deterministic generation, exact full-field round-trip, fresh-process
  reapplication, paired-save failure honesty, zero idle cost, visible DX12 proof,
  unchanged default baselines, comment audit, inventories, and independent review.

LL0 completed on 2026-08-01. The current-source census locks all 80 cinematic
atoms, 14 material kinds and their targeting surface, all 23 tracked schema-v1
styles, the live merge/reset/capture path, SplitMix64 generator version 1, 14
recipe families, the exact bundle/output grammar, and current idle/style-apply
measurements. No production source, style, configuration, or baseline changed.
The live ledger is therefore 1/14 (7%). Evidence is in
`../Reports/2026-08-01/look-lab-random-style-authoring-ll0-census.md`.

## Contact Energy And Warm-Start Integrity (2026-08-01)

Source: owner review of the rejected Box Vibration And Warm-Start Integrity
transition and the standing energy coverage. The current four-brick fixture
already fails to settle, while the rejected cumulative transition visibly
launched boxes and was correctly restored. The new plan treats collision energy
as an independent physical invariant rather than accepting a deterministic
replacement oracle.

Binding owner directions:

- retain the working terrain support seed; correctness-only cleanup is not a
  reason to replace it;
- install energy/momentum oracles and the giant tower/200-box semantic gate
  before changing production contact behavior;
- keep the 12-iteration cap, gravity, timestep, friction, restitution, damping,
  and sleep settings unchanged while measuring the correction;
- authorize only the object-contact restitution correction initially; any
  warm-start identity repair is conditional on residual attributed evidence;
- require 32 levels as the corrected floor, 64 levels as the giant-scene
  acceptance target, and report 128 levels as a stretch measurement; and
- permit no physics, SkullScope, Replay, or visual golden transition without
  explicit owner acceptance of the exact final visible and semantic evidence;
  implementation, candidate generation, complete comparison, and every
  baseline-independent validation must finish before that terminal request.

## Features

| Plan | State | Verified phase count | Start condition |
|---|---|---:|---|
| [shadow-edge-quality](../Reports/2026-07-12/shadow-edge-quality-closure.md) | Complete | 5/5 | Fixed Poisson filtering, detail-first terrain sampling, texel snapping, measured presets, and no-cascades decision complete |

Fracture replay was moved to `WNF/` by the owner on 2026-07-11 (inventory
rule 9 applies — it is not live work and is not tracked here).

## Binding Decisions And Open Decisions

Binding:

- 2026-07-26 concrete parameter-bag ruling: the 22 registered shapes preserved
  in `../Reports/2026-07-26/concrete-parameter-bag-elimination-closure.md` were
  repair-required. Their
  replacements use concrete owner-produced values, focused direct operations,
  or concrete stack-scoped invariant owners. This remediation may introduce no
  inheritance, abstract or pure-virtual interface, virtual dispatch, CRTP
  policy base, type erasure, callback interface, service registry, or renamed
  context bag. `SceneSaveRequest` may retain its name only in the owner-directed
  four-field form: output path plus Scene, controller-state, and presentation
  save values.
- 2026-07-25 invariant-ownership ruling: an aggregate type is legitimate only
  when it names and enforces an invariant (header `Invariant:` block plus a
  focused test); an aggregate that only carries data to shorten a signature
  remains a banned bag. Multi-step operations whose correctness depends on
  call order must enforce that order in a type, not in comments or caller
  discipline. When a review finds three or more sibling structs plus wide
  apply free functions plus ordering/arbitration comments around one
  operation, it must report "this operation needs an invariant owner" — a
  parameter reshuffle is not an accepted remediation. This test repeals no
  existing ban. GV0 installed the standing rule in `AGENTS.md`, added the
  aggregate-invariant check to the comment-audit skill, and made the extrusion
  signal mandatory review output. The later 2026-07-28 RG1 governance change
  replaced the former ceiling with the 12-or-more qualitative owner-review
  trigger and exact current-signature rulings.
- Satisfied 2026-07-20 by the owner's render-graph decision: RenderGraph is the
  owner of pass scheduling and ordinary frame-resource barrier emission.
  Closure and bounded edge exceptions are recorded in
  [`render-graph-completion-closure`](../Reports/2026-07-20/render-graph-completion-closure.md).
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

## Nightrunner 26 July (2026-07-26)

| Plan | State | Verified phase count | Next action |
|---|---|---:|---|
| [closure report](../Reports/2026-07-26/nightrunner-26-july-closure.md) | Complete | 3/3 | None; completed TODO deleted under inventory rule 4 |

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
