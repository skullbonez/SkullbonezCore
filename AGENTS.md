# Agent Instructions

> Universal contract for any AI agent working on this repository.
> Framework-agnostic: applies to any current or future AI coding agent.

**Do not** force-push, rebase, or rewrite git history. Feature-branch commits
and normal pushes are allowed without asking. Agents may submit or merge PRs only
when explicitly requested by the user. Direct commits or pushes on `main` still
require explicit user confirmation.

---

## Agent Startup Contract

Before editing, all agents must read:

1. `AGENTS.md`.
2. `README.md`.
3. `Agentic/README.md`.
4. `Agentic/SessionState.md`.
5. Run `git status --short --branch` and treat any pre-existing dirty files as
   user-owned.

If this is a fresh machine or a required tool lookup fails, read
`FIRST_TIME_SETUP.md`. Load deeper skills, plans, audits, reports, or reference
files only when the current task calls for them.

Optional CodeGraph support may be present for local code-intelligence lookups.
Do not require CodeGraph for startup, validation, or ordinary work. If it is
missing or stale, continue with the normal `rg`/targeted-read workflow.

When CodeGraph is installed and `.codegraph/` exists, use it as a first-pass
map before opening large source files:

1. Run `codegraph status .` to check whether the local index is current.
2. If CodeGraph MCP tools are available in the current Codex session, prefer
   them over shelling out. Use `codegraph_explore`/`codegraph_node` style tools
   for focused symbol, file, caller/callee, and impact lookups.
3. If MCP tools are unavailable, use the CLI: `codegraph query <name-or-topic>`
   or `codegraph explore "<area>"` to find likely symbols and files.
4. Use `codegraph node <symbol-or-path>` for focused source context.
5. Use `codegraph callers <symbol>`, `codegraph callees <symbol>`, or
   `codegraph impact <symbol>` before refactors, API changes, or bug fixes.
6. Confirm important findings against the actual files before editing; the
   graph is an index, not a substitute for source review.

Refresh an existing local index with `codegraph sync .` after large source
changes, or `codegraph index .` if the graph appears inconsistent.

---

## Plan Implementation Mode

When implementing work from `Agentic/Plans`, use the repo-local orchestrator
skill as the default coordination path:
`Agentic/Skills/orchestrator/SKILL.md`. The skill owns plan selection, branch
choice, fresh worker-agent delegation, independent rubber-duck review, required
validation selection, commits, pushes, and handoff reporting. Reading, updating,
or drafting a plan is normal documentation work; turning a plan into repository
changes should follow the orchestrator skill unless the user explicitly asks to
bypass it.

---

## Before Editing

1. Follow the Agent Startup Contract above.
2. Identify your change's impact area: DX12, physics, scene system, tests, documentation.
3. State whether validation is required now. For normal implementation work, do not run repository validation scripts while iterating; name the targeted validation command to defer until PR-bound commit/PR prep. For documentation-only changes, state that no validation is required.
4. If unrelated dirty files are present, leave them alone. Do not overwrite,
   revert, stage, or format user-owned changes unless explicitly requested.
5. For any source-bearing file you edit (`.cpp`, `.h`, `.hpp`, `.inl`,
   `.hlsl`, or substantial tool scripts), apply the comment standard in
   `Agentic/Reference/comment-style-guide.md`. A learning header alone is not
   enough: dense or risky code also needs nearby `Concept:`, `Why:`,
   `Invariant:`, `Lifetime:`, or `Hazard:` comments where the guide calls for
   them.

## Dependency Direction Rule

Dependency direction must remain visible from physical include paths. `Core/`
is the infrastructure floor and must not include Assets, Gameplay, Physics,
Rendering, Scene, World, Runtime, or UI. Physics and Rendering may include Core
and Maths, but must not include Gameplay, Runtime, or UI. Gameplay may include
Core, Maths, and stable Physics/Rendering value or registration seams, but must
not include Assets, Scene, World, Runtime, or UI. Move a misplaced value or
implementation to its owning layer instead of adding a forwarding header,
compatibility alias, callback pack, service bag, or upward include.

Before closing a dependency-direction change, run these exact review proofs;
all commands must return no rows:

```powershell
rg -n '^#include[[:space:]]+.*(Assets|Gameplay|Physics|Rendering|Scene|World|Runtime|UI)/' SkullbonezSource/Core
rg -n '^#include[[:space:]]+.*(Gameplay|Runtime|UI)/' SkullbonezSource/Physics SkullbonezSource/Rendering
rg -n '^#include[[:space:]]+.*(Assets|Scene|World|Runtime|UI)/' SkullbonezSource/Gameplay
```

If an edge cannot be inverted in the owning task, record it in that task's
exception table with the owner, reason, and deletion condition. An unrecorded
edge or a compatibility spelling that hides it is a closure failure.

UI is a presentation library below Runtime. Runtime may include UI to compose,
route, and draw the operator surface; UI must not include Runtime. UI consumes
detached value snapshots and emits typed command values that Runtime owners
apply. Move misplaced values into UI or build them at the Runtime call site
instead of hiding an upward edge behind a forwarding header, alias, callback
pack, service bag, or broad context object.

Before closing a UI/Runtime dependency change, run this exact review proof; it
must return no rows:

```powershell
rg -n '^#include[[:space:]]+.*Runtime/' SkullbonezSource/UI
```

## Runtime Package Direction Rule

Physical sub-packages inside `SkullbonezSource/Runtime/` expose ownership and
must not collapse back into a flat Runtime god package. `App` is the composition
root; `RuntimeFrameViews.h` is the only allowed top-level source-bearing file
and contains values/forward declarations only. A package may include itself,
lower engine layers allowed by the standing dependency rules, and only these
Runtime targets:

| Source package | Allowed Runtime package targets |
|---|---|
| `App` | Every Runtime package |
| `Automation` | `App`, `Camera`, `Capture`, `DevelopmentTools`, `Diagnostics`, `Direction`, `Editor`, `Input`, `Interaction`, `Replay`, top-level frame views, `Scene`, `Tools` |
| `Camera` | `App` process values, `Direction`, `Input`, `Interaction`, `Scene`; never `Render` |
| `Capture` | `App`, `Automation`, `Camera`, `Diagnostics`, `Input`, `Interaction`, `Render`, `Replay`, top-level frame views, `Scene`, `Simulation`, `Tools` |
| `DevelopmentTools` | `App`, `Input`, `Replay` |
| `Diagnostics` | `App`, `Automation`, `Capture`, `Debug`, `Input`, `Render`, `Replay`, `Scene` |
| `Direction` | `Camera`, `Capture`, `Scene`, `Tools` |
| `Editor` | `App`, `Camera`, `Capture`, `Diagnostics`, `Input`, `Interaction`, `Replay`, top-level frame views, `Scene`, `Tools` |
| `Input` | `App/Window` platform capability, `Camera`, `Interaction`, `ReplayEventCommand`, `SceneLifecycle`; never `Render`, `UI`, or `DevelopmentTools` |
| `Interaction` | `Camera`, `Diagnostics`, `Render`, `Scene`, `Simulation` |
| `Render` | `App`, `Camera`, `Debug`, `DevelopmentTools`, `Diagnostics`, `Input`, `Interaction`, `Replay`, top-level frame views, `Scene`, `Tools`, `UI` |
| `Replay` | `Camera`, `Diagnostics`, `Editor`, `Input`, `Interaction`, `Render`, `Scene`, `Simulation`, `Tools`, `UI`; Replay remains an upper Runtime consumer |
| `Scene` | `App`, `Automation`, `Camera`, `Debug`, `Diagnostics`, `Editor`, `Input`, `Interaction`, `Render`, `Replay`, `Simulation`, `Tools` |
| `Simulation` | `Interaction`, `Scene` |
| `Startup` | `App`, `Replay`, `Scene` |
| `Tools` | `Camera`, `Editor`, `Input`, `Interaction`, `Replay`, `Scene` |
| `UI` | `App`, `Automation`, `Capture`, `Diagnostics`, `Editor`, `Replay`, top-level frame views, `Scene` |
| `Debug` | No other Runtime package |
| top-level frame views | No Runtime package |

`InputRouter` is the only retained input routing/context/pointer owner.
`Input` samples devices, `InputController.Bindings` owns the immutable binding
table, `InputFrame` assembles frame-local values, `InputFrameExecution`
sequences the turn, `InputRouter.Interactions` remains part of the same router
owner, and stateless `InputController` applies mode/camera policy. Do not add a
second retained input state owner or move frame orchestration down into Input.

Before closing a Runtime package change, run every proof below; every command
must return no rows. Each pattern is the complement of the source package's
allowed Runtime targets, so a new edge requires an owner-approved table and
proof update rather than a forwarding header, callback facade, or context bag:

```powershell
rg -n '^#include[[:space:]]+\x22(\.\./)?(Debug|Render|Simulation|Startup|UI)/' SkullbonezSource/Runtime/Automation
rg -n '^#include[[:space:]]+\x22((\.\./)?(Automation|Capture|Debug|DevelopmentTools|Diagnostics|Editor|Render|Replay|Simulation|Startup|Tools|UI)/|\.\./RuntimeFrameViews\.h)' SkullbonezSource/Runtime/Camera
rg -n '^#include[[:space:]]+\x22(\.\./)?(Debug|DevelopmentTools|Direction|Editor|Startup|UI)/' SkullbonezSource/Runtime/Capture
rg -n '^#include[[:space:]]+\x22((\.\./)?(Automation|Camera|Capture|Debug|Diagnostics|Direction|Editor|Interaction|Render|Scene|Simulation|Startup|Tools|UI)/|\.\./RuntimeFrameViews\.h)' SkullbonezSource/Runtime/DevelopmentTools
rg -n '^#include[[:space:]]+\x22((\.\./)?(Camera|DevelopmentTools|Direction|Editor|Interaction|Simulation|Startup|Tools|UI)/|\.\./RuntimeFrameViews\.h)' SkullbonezSource/Runtime/Diagnostics
rg -n '^#include[[:space:]]+\x22((\.\./)?(App|Automation|Debug|DevelopmentTools|Diagnostics|Editor|Input|Interaction|Render|Replay|Simulation|Startup|UI)/|\.\./RuntimeFrameViews\.h)' SkullbonezSource/Runtime/Direction
rg -n '^#include[[:space:]]+\x22(\.\./)?(Automation|Debug|DevelopmentTools|Direction|Render|Simulation|Startup|UI)/' SkullbonezSource/Runtime/Editor
rg -n '^#include[[:space:]]+\x22((\.\./)?(Automation|Capture|Debug|DevelopmentTools|Diagnostics|Direction|Editor|Render|Simulation|Startup|Tools|UI)/|\.\./RuntimeFrameViews\.h)' SkullbonezSource/Runtime/Input
rg -n '^#include[[:space:]]+\x22((\.\./)?(App|Automation|Capture|Debug|DevelopmentTools|Direction|Editor|Input|Replay|Startup|Tools|UI)/|\.\./RuntimeFrameViews\.h)' SkullbonezSource/Runtime/Interaction
rg -n '^#include[[:space:]]+\x22(\.\./)?(Automation|Capture|Direction|Editor|Simulation|Startup)/' SkullbonezSource/Runtime/Render
rg -n '^#include[[:space:]]+\x22((\.\./)?(App|Automation|Capture|Debug|DevelopmentTools|Direction|Startup)/|\.\./RuntimeFrameViews\.h)' SkullbonezSource/Runtime/Replay
rg -n '^#include[[:space:]]+\x22((\.\./)?(Capture|DevelopmentTools|Direction|Startup|UI)/|\.\./RuntimeFrameViews\.h)' SkullbonezSource/Runtime/Scene
rg -n '^#include[[:space:]]+\x22((\.\./)?(App|Automation|Camera|Capture|Debug|DevelopmentTools|Diagnostics|Direction|Editor|Input|Render|Replay|Startup|Tools|UI)/|\.\./RuntimeFrameViews\.h)' SkullbonezSource/Runtime/Simulation
rg -n '^#include[[:space:]]+\x22((\.\./)?(Automation|Camera|Capture|Debug|DevelopmentTools|Diagnostics|Direction|Editor|Input|Interaction|Render|Simulation|Tools|UI)/|\.\./RuntimeFrameViews\.h)' SkullbonezSource/Runtime/Startup
rg -n '^#include[[:space:]]+\x22((\.\./)?(App|Automation|Capture|Debug|DevelopmentTools|Diagnostics|Direction|Render|Simulation|Startup|UI)/|\.\./RuntimeFrameViews\.h)' SkullbonezSource/Runtime/Tools
rg -n '^#include[[:space:]]+\x22(\.\./)?(Camera|Debug|DevelopmentTools|Direction|Input|Interaction|Render|Simulation|Startup|Tools)/' SkullbonezSource/Runtime/UI
rg -n '^#include[[:space:]]+\x22((\.\./)?(App|Automation|Camera|Capture|DevelopmentTools|Diagnostics|Direction|Editor|Input|Interaction|Render|Replay|Scene|Simulation|Startup|Tools|UI)/|\.\./RuntimeFrameViews\.h)' SkullbonezSource/Runtime/Debug
rg -n '^#include[[:space:]]+\x22(\.\./)?(App|Automation|Camera|Capture|Debug|DevelopmentTools|Diagnostics|Direction|Editor|Input|Interaction|Render|Replay|Scene|Simulation|Startup|Tools|UI)/' SkullbonezSource/Runtime/RuntimeFrameViews.h
```

## Replay Boundary Rule

Replay is an upper Runtime consumer, not a type provider to engine layers.
`Physics/`, `Rendering/`, `Scene/`, `World/`, and `Core/` must not include
`Runtime/Replay/*`. Replay reads Physics through `PhysicsApi.h`,
`PhysicsEngine`, and Physics-owned value snapshots; render presentation crosses
as value packets or bounded queues. Do not move a Replay type downward or hide
one behind a forwarding header, alias, callback pack, or broad context object.

Before closing replay-facing work, run this exact review proof; it must return
no rows:

```powershell
rg -n '^#include[[:space:]]+.*Runtime/Replay/' SkullbonezSource/Physics SkullbonezSource/Rendering SkullbonezSource/Scene SkullbonezSource/World SkullbonezSource/Core
```

Replay retains the only post-gameplay growth privilege, and only through a
`RuntimeReserveAllocator`-registered owner with a replay-phase check, hard cap,
logged growth counter, and owner-specific policy comment. The authoritative
replay-boundary plan or closure report carries the registration inventory:
owner, phase gate, cap, high-water/growth counter, policy comment, and status.
Adding a registration, increasing a cap, weakening a phase gate, or changing
counter coverage must update that inventory in the same commit. Reviews of
replay-touching work must answer both questions: did a downward Replay include
appear, and did a growth privilege appear or expand outside the inventory?
Either finding blocks the touching change.

## Comment Quality Gate

Comment quality is part of completion, not a follow-up nicety.

- If a task touches source for meaningful work, inspect every touched
  source-bearing file with `Agentic/Skills/comment-style-audit/skill.md` before
  reporting done.
- Do not treat "file has a learning header" as full compliance. The body of the
  file must also teach local vocabulary, non-obvious ownership/lifetime rules,
  invariants, hazards, units, and validation-sensitive behavior.
- For a subsystem or full-repository comment pass, first create or update an
  explicit checklist plan under `Agentic/Plans/` that lists every tracked source
  file in scope. Use `git ls-files`, not `rg`, for the inventory because ignored
  directory names such as `Physics/Debug` can still contain tracked source.
- The checklist is the source of truth for comment-pass completion. It must
  include one checkbox per tracked source-bearing file in the scoped inventory
  (`.cpp`, `.h`, `.hpp`, `.inl`, `.hlsl`, and substantial `.py`/`.bat`/`.ps1`
  tools when they are in scope). Do not report a subsystem as complete from a
  sample, directory glance, or search result alone.
- Tick a checklist item only after the file has been inspected against the
  guide. If a file is intentionally deferred, leave it unchecked and add a
  reason; never silently skip a file.
- A checked item means the file has both the required learning-header sections
  and any nearby `Concept:`, `Why:`, `Invariant:`, `Lifetime:`, or `Hazard:`
  comments needed by the guide for non-obvious code. Missing either part keeps
  the item unchecked.
- Before final reporting on a comment pass, rerun the scoped `git ls-files`
  inventory and reconcile it against the checklist. The final answer or handoff
  must include the checklist path, checked count, deferred count, and any files
  still unchecked.
- Trivial wrappers, link stubs, one-line forwarding files, and tiny batch or
  PowerShell helpers do not need a full learning header when the diff is
  self-explanatory. Add local comments only for non-obvious validation purpose,
  shell hazards, ownership, or runtime behavior.
- Comment-only source edits count as documentation-only for repository
  validation, provided the diff is strictly comments/docs. If code behavior
  changes accidentally, stop and switch to the validation map below.

## Governance Review Model

The deleted runtime-boundary regex checker is not part of repository
enforcement. Do not recreate frozen-count or spelling-budget checks for
migration vocabulary, inheritance, `Run` size, throw counts, or similar
historical debt. These policies are enforced by code review, owning plans,
focused behavioral tests recorded in
`Agentic/Reports/behavioral_test_depth_closure_20260711.md`, and
the targeted validation gates below.

## God-Object Closure Rule

Ownership cleanup is judged across a logical type or module, not one physical
file. For `Run`, review `Run.h`, every `Run*.cpp`, shared internal headers,
callback/context types, and forwarding facades as one surface. Making
`Run.cpp` short while the same authority remains reachable through sibling
translation units is not decomposition.

At closure, `Run` may construct and wire concrete owners, sequence
startup/shutdown, pump operating-system messages, establish top-level frame
order, and report the final application result. It must not own or decide
input, scene, replay, render, UI, physics, tools, capture, defaults, or
diagnostics business state. Those domains must have concrete owners with typed
value boundaries.

The following are closure failures, even when presented as temporary or
compatibility architecture:

- mutable multi-domain state or queues collected in `Run` or a replacement
  `*Internal`, `*Context`, `*Services`, `*Bindings`, or similarly broad bag;
- `void*`, stored host pointers/references, callback packs, friend access, or
  lambdas that let an extracted owner reach back into `Run` state;
- `Run::*` forwarding wrappers or nominal owner types that merely relay
  business operations while authority remains in `Run`;
- an extracted owner that absorbs unrelated domains and becomes the next god
  object;
- a completion claim based only on line count, file count, or a mechanical
  translation-unit split.

A final independent ownership review is mandatory for a god-object cleanup.
A finding is credible when it identifies concrete unrelated responsibilities,
state, or dependency authority and the owner boundary they violate. Any such
finding reopens the owning checklist item and blocks plan/campaign completion;
it cannot be waived as follow-up debt. Large cohesive files are allowed only
when the review records why their state and invariants belong to one owner.

## Migration Cleanup Review Rule

Compatibility code is allowed only when it is honest, bounded, and guarded.
Do not introduce new types, functions, fields, or modules named around migration
mechanics such as `Runtime`, `Snapshot`, `Compatibility`, `Transitional`,
`Bridge`, `Tuning`, `ForCompatibility`, or raw `Model`/`GameModel` access unless
the change names all four of these in the owning plan, source comment, or commit
body: owner, reason, deletion condition, and review evidence.

- Prefer domain nouns over migration nouns. For example, split values into
  `PhysicsMaterial`, `BodySimulationLimits`, `ContactPolicy`,
  `WaterRenderStyleSettings`, `FluidForceSettings`, `RenderResourceContext`, or
  other owner-specific concepts instead of creating a catch-all runtime bag.
- A bridge that mostly answers "how did we avoid the old global/service/storage
  path?" is not done until it either becomes a domain API or has an explicit
  follow-up row with a deletion condition.
- When deleting a migration artifact, update the owning plan and add or extend
  behavioral coverage when the regression can be tested practically.
- Do not mark a kill-list row or migration-cleanup plan complete while source
  still exposes the deleted shape under a new compatibility spelling. If a real
  model-owner command or context remains, describe it as the remaining domain
  boundary and move strict-authority work to the appropriate follow-up plan.
- Use a single independent rubber-duck review at the end of a whole cleanup
  plan, not one review per tiny slice, unless the user explicitly asks for more.

## Hot-Path Data and Inheritance Review Rule

Physics, collision, solver, audio classification, render submission, and other
per-frame hot paths must operate on compact store arrays, value records,
bounded scratch buffers, and explicit post-pass side-effect queues. Do not put
polymorphic service objects, callback chains, handle lookups, scattered
`GameModel` access, or owner-side compatibility commands inside hot loops.

- New inheritance is banned unless it is a stable boundary with a real need for
  runtime polymorphism. The owning plan, source comment, and commit body must
  name the owner, why value/data composition is insufficient, the expected
  call frequency, and the validation or perf evidence required.
- Migration cleanup must not introduce `*Sink`, `*Bridge`, `*Adapter`,
  `*Compatibility`, or callback-style interfaces on hot paths as a way to hide
  old ownership. Prefer a plain output buffer that the owner applies after the
  pass.
- Physics hot paths should read/write `PhysicsBodyStore`, `ColliderStore`,
  solver scratch arrays, and bounded side-effect arrays. Any required
  `PhysicsModelAccess`, `PhysicsBodyEventSink`, UI, audio service, or runtime
  owner work belongs outside the solver/broadphase/narrowphase loop.
- When deleting a hot-path inheritance or callback artifact, extend the focused
  tests or validation evidence that would catch the regression. Do not replace
  the deleted artifact with a new compatibility spelling.

## Scene Object Identity Policy

`PhysicsSceneObjectId` is the single stable cross-system identity for a scene
object. Undo, scene serialization, picking, logging, replay correlation, and
future cross-system features must use it rather than introducing `EntityId` or
another unified id. Physics, rendering, audio, and other hot subsystem paths
use their own typed handles or dense rows after resolving the scene id at the
owner boundary; a dense row is never durable identity.

## Runtime Static Allocation Policy

Runtime allocation policy is global zero allocation by default. Dynamically
growing STL types, STL growth calls, direct heap calls, `std::make_unique`,
`std::make_shared`, and equivalent heap paths are banned in steady runtime code
unless an owner-approved exception routes through the allocation policy path.
Gameplay storage must be fixed or preallocated before steady gameplay begins,
and pool exhaustion must assert in Profile/Debug or fail fatally in Release with
owner, capacity, high-water, and phase diagnostics; there is no gameplay growth
fallback.

Replay is the only runtime subsystem allowed to grow after steady gameplay
starts, and that growth must be approved by `RuntimeReserveAllocator` through a
registered owner, replay phase check, hard cap, logged growth counter, and
policy comment. Unregistered replay allocations are allocation-guard failures.

`new`, `delete`, `malloc`, `free`, STL reserve/growth, `std::make_unique`,
`std::make_shared`, and equivalent heap paths are banned at runtime outside
pre-gameplay phases, allocator/wrapper internals, and explicit cold utility
actions such as screenshot/readback, file save/load, replay artifact IO,
diagnostics dumps, and editor mutation actions. Violations are allocation-policy
validation and review failures.

Static enforcement lives in `tools/check_allocation_policy.py` and
`tools/allocation_policy_allowlist.json`. The checker scans configured source
roots for direct heap/reserve APIs, owning dynamic STL members, and STL growth
calls. Allowlist rows must name the owner, phase, reason, cap, and
removal/wrapper plan; do not add ad-hoc regex gates, frozen count ratchets, or
new runtime allocation exceptions without an owner decision.

## Error Handling Policy

Exceptions are banned for engine code. The strict source throw inventory is
zero as of 2026-07-10. Do not add a new throw-count ratchet or frozen budget;
any new `throw` is a review failure.

| Lane | Use For | Mechanism |
|------|---------|-----------|
| F: Fatal invariant | Should-never-happen engine state in physics, stores, solver, frame loop, replay internals, or other owned runtime logic | `SB_FATAL(owner, ...)`; logs owner/diagnostics, flushes, breaks in Debug/Profile, and never returns |
| R: Recoverable result | External input or environment failure: scene/asset files, editor commands, automation input, device support, file IO | `SbResult`/future value-carrying result; operation fails and reports an owner/message to the UI or log boundary |
| P: Probe assertion | Validation, interaction, replay, scrub, and stress probes that should become machine-readable failures | Existing probe/report channel such as `FailAutomation(...)`, with interaction report `ok=false` and a failure message |

New fatal or recoverable paths must name their lane in source comments or the
owning plan when the lane is not obvious from the API being used.

## After Editing

Do not run validation scripts automatically after every edit. Formal repository
validation runs only as a pre-commit/PR gate, or when the user explicitly asks
for it. When preparing PR-bound work, choose the smallest script from `tools\`
that matches the fix. The default broad PR gate runs the mandatory CPU umbrella
first, then three runtime lanes: the Automation boundary plus replay/prediction
smoke, one DX12 renderer process, and the physics lane's standalone-smoke plus
regression-scene processes (five engine processes total).
Use deep, perf, and UI validation only when the change actually needs them.
Every DX12 modification also requires the mandatory bounded graphics-stress
run defined below.

`validate_full.bat` and its `agent_validate.bat` alias are the mandatory broad
superset. The CPU umbrella includes the ratified product coverage floors. A new
standalone CPU test executable must join
`validate_all_cpu_tests.bat`, `tools/README.md`, and the file-to-gate mapping in
the same commit; a test target reachable only through a direct script or
`validate_select.bat` is not merge-gated.

Coverage-floor enforcement has three invocation rules:

- Run `tools\validate_coverage.bat` directly when changing coverage floors,
  exclusions, instrumentation scope, coverage tooling, or tests whose purpose
  is to raise subsystem coverage.
- Run it at the final pre-commit/PR gate when the changed scope needs an
  explicit confirmation that every subsystem remains above its ratified floor.
- Do not duplicate it when running `tools\validate_all_cpu_tests.bat`:
  that umbrella runs the coverage gate automatically, as do
  `tools\validate_full.bat`, `tools\agent_validate.bat`, and the hosted
  mandatory CPU CI lane through the same call chain.

| Change Type | Pre-Commit/PR Command | Runtime |
|-------------|---------|---------|
| Documentation only | No validation required | N/A |
| Main doctest unit tests only | `tools\validate_tests.bat` | build + console test runner |
| Standalone/combined CPU test targets | `tools\validate_all_cpu_tests.bat` | incremental builds + 6 console test launches |
| Small refactor, no render or physics changes | `tools\validate_fast.bat` | ~30s |
| Shader or render backend | `tools\validate_dx12_renderer.bat`, then `tools\run_graphics_stress.bat 1` | ~3 min |
| DX12 renderer validation tooling | `tools\validate_fast.bat`, then `tools\validate_dx12_renderer.bat`, then `tools\run_graphics_stress.bat 1` | ~3 min |
| Physics, collision, solver, or rigid body changes | `tools\validate_physics.bat` | 2 engine processes |
| Broad physics baseline, bullet sweep, or SkullScope diagnostics | `tools\validate_physics_deep.bat` | ~45s+ |
| Performance-sensitive hot path | `tools\validate_perf.bat` | ~1 min |
| General DX12 graphics stress, crash reproduction, or memory-growth investigation | `tools\run_graphics_stress.bat 1`; use `overnight` only when intentionally soaking | bounded or overnight |
| Broad or uncertain scope | `tools\validate_full.bat` | CPU tests + 5 engine processes |
| Unsure what to run at the PR gate | `tools\agent_validate.bat` | CPU tests + 5 engine processes |
| Comment-only source or documentation cleanup | No repository validation required; prove the diff is comments/docs only | N/A |

Profiling marker or platform-profiler changes must also run:

```bat
Profile\SKULLBONEZ_CORE.exe --platform-profiler-markers
```

### File To Validation Mapping

Rows are cumulative when a change matches more than one entry. In particular,
the replay visual-fidelity gate supplements the normal source, test, physics,
render, or tool gate; it does not replace it.

| Files Changed | Required Pre-Commit/PR Script |
|---------------|-----------------|
| `RenderBackendDX12*.cpp/h`, `Rendering/DX12/*` | `validate_dx12_renderer` + `run_graphics_stress.bat 1` |
| `SkullbonezData/shaders/*` | `validate_dx12_renderer` + `run_graphics_stress.bat 1` |
| `RigidBody*`, `PhysicsWorld*`, `SimulationSystem*` | `validate_physics` |
| `SceneController.Objects*` physics coordination changes | `validate_physics` |
| `BoundingSphere*`, `BoundingBox*`, `ConvexHullShape*`, `CollisionShape*` | `validate_physics` |
| `GameModel*` physics body/state changes | `validate_physics` |
| `WorldEnvironment*` | `validate_physics` |
| `SpatialGrid*` | `validate_physics` + `validate_perf` |
| `SceneController.Objects*` render stream or hot-loop changes | `validate_dx12_renderer` + `validate_perf` |
| `Config*`, `SkullbonezData/engine.cfg` physics defaults such as gravity, fluid, drag, friction, sleep, solver, or broadphase values | `validate_physics` |
| `TestOutput/baselines/physics_regression_varied.csv` | `validate_physics` |
| Other physics CSV baselines or `TestOutput/baselines/physics_query*.json` | `validate_physics_deep` |
| `Common.h` | `validate_full` |
| `SkullbonezTests/*`, `SKULLBONEZ_TESTS.vcxproj`, `SKULLBONEZ_TESTS.vcxproj.filters` | `validate_tests`; add `validate_coverage` when the tests are intended to raise subsystem coverage |
| `SkullbonezSource/Runtime/Replay/*`, `RunReplay*`, or replay-facing presentation/submission changes in `EditorTracer*` or `RuntimeRender*` | `validate_replay_visual_fidelity.bat` in addition to the normal mapped gate |
| `SkullbonezTests/TestReplay*` or replay artifact/presentation test changes | `validate_tests`, then `validate_replay_visual_fidelity.bat` |
| `SkullbonezData/scenes/prediction_ragdoll_wall_200.scene.json`, `SkullbonezData/interaction/prediction_ragdoll_wall_200_*.json`, or `TestOutput/baselines/replay_visual_fidelity_200_box*.json` | `validate_replay_visual_fidelity.bat` |
| `tools/check_replay_visual_fidelity.py`, `tools/replay_query.py`, `tools/validate_replay_visual_fidelity.bat`, or `tools/validate_replay_scrub.bat` | `validate_fast`, then `validate_replay_visual_fidelity.bat` |
| `Agentic/Tests/*` or a new standalone CPU test project/script | `validate_all_cpu_tests` |
| `Core/Allocation/*` | `validate_perf` |
| `tools/check_allocation_policy.py`, `tools/allocation_policy_allowlist.json` | `validate_fast`, then `python tools\check_allocation_policy.py --self-test` and `python tools\check_allocation_policy.py --repo .`; add `validate_perf` if runtime guard or reserve semantics change |
| `tools/check_coverage.py`, `tools/coverage_floors.json`, `tools/validate_coverage.bat`, or coverage exclusions/instrumentation scope | `validate_fast`, then run `tools\validate_coverage.bat` directly |
| `Run*`, `Runtime/*` | `validate_full` |
| `Window*` | `validate_full` |
| `Init*` | `validate_full` |
| `SkullbonezData/assets/*.assets.json` | `validate_full` |
| `SkullbonezData/hulls/*.hull` | `validate_full` |
| `SkullbonezData/scenes/*.scene.json` | `validate_full` |
| Multiple areas or unsure | `validate_full` |
| Documentation-only `Agentic/*` (excluding `Agentic/Tests/*`), `*.md`, docs | No validation required when documentation-only |
| `tools/*` | `validate_fast`, then run the changed script; `validate_fast` includes `validate_tests` |

---

## Rules

- **Repository validation scripts are PR/commit gates.** Do not run `tools\validate_*` merely as you go. During iteration, use targeted builds, launches, focused tests, or inspections only when they answer a specific question about the fix.
- **Renderer validation must fail fast.** `tools\validate_dx12_renderer.bat` builds `Profile` first and must stop before launching DX12 if compilation fails. Renderer launches in that script use PID-scoped timeouts, then `tools\check_dx12_baselines.py` handles image comparison artifacts.
- **DX12-only validation is the production safety net.** `tools\validate_dx12_renderer.bat` builds `Profile`, launches only DX12, checks `dx12_validation.txt`, and compares captures against committed DX12 baselines.
- **Every DX12 modification requires a crash-free stress run of at least 10 seconds.** This applies to DX12 runtime source, shaders, resource/binding contracts, and DX12 validation tooling. The standard bounded command is `tools\run_graphics_stress.bat 1` (one minute); record its command, measured runtime, and successful exit evidence. A shorter custom launch is acceptable only when its measured runtime is at least 10 seconds and the process exits without a crash.
- **Never claim validation success without command output.** Paste the validation output when validation is required.
- **Never skip required pre-commit/PR validation** for code, tool, scene, shader, baseline, or runtime behavior changes unless the user explicitly says to.
- **Documentation-only changes require no validation.** Do not run `validate_fast` for prose-only edits.
- **Physics baseline refreshes require a final physics gate.** Regenerate CSV or SkullScope baselines only from the final Debug executable, scene files, and config that will be committed, then rerun the matching gate after the baseline files are updated: `tools\validate_physics.bat` for the core varied-scene baseline, or `tools\validate_physics_deep.bat` for bullet sweep, shooting, known-issue, or SkullScope baselines. A copied physics artifact is not trustworthy until the gate compares it byte-exactly against the committed baseline.
- **`tools\update_baselines.bat` is visual/perf only.** Do not use it for physics CSV or SkullScope baselines unless the script explicitly grows that support; use the Debug physics artifacts generated by the validation commands and rerun the matching physics gate.
- **Time all user-requested work.** Record elapsed wall-clock time for every task from the start of work to the final response. Report the time taken in the final answer, and call out timings for substantial sub-runs such as builds, validation scripts, game launches, SkullScope trace generation, or long investigations.
- **Protect dirty worktrees.** Run `git status --short --branch` before edits and before commits. Treat pre-existing changes as user-owned. Never use `git reset --hard`, destructive `git clean`, checkout/discard commands, or broad formatter runs that touch unrelated files unless the user explicitly requested that operation.
- **Keep physics debug data cheap for model analysis.** Use SkullScope. Do not paste or ingest whole physics CSV, NDJSON, or SQLite diagnostic files unless the user explicitly requests raw logs. When available, prefer the queryable diagnostics workflow in `Agentic/Reference/physics-query-reference.md` and load `Agentic/Skills/skore-skullscope/skill.md` for the compact runbook: generate a deterministic trace with `--physics-diag`, run `tools\physics_query.bat <trace> summary` and `tools\physics_query.bat <trace> events`, or expand a pre-baked question with `tools\physics_query.bat <trace> questions <name>`, then ask focused frame/body/contact/island queries. The deterministic physics CSV remains the byte-exact validation artifact.
- **Report SkullScope query cost.** Whenever SkullScope is used, print the exact trace command, every `tools\physics_query.bat` query, and the data-size accounting in the final answer or handoff. Report raw artifact sizes separately from model-ingested data: trace NDJSON bytes and SQLite cache bytes are on-disk artifact sizes, while GPT-read size is only the bounded query output text actually exposed to the model. Include per-query output size and the total GPT-read characters/bytes. If output was truncated by the shell/app, mark it as truncated and rerun a narrower query before drawing conclusions.
- **Run builds, game launches, and validation/test scripts in a visible console window when available.** Use `cmd.exe` or PowerShell so the user can watch compile and test progress, and mirror output to a log when possible so the final response can quote the result. In headless or cloud contexts where a visible console is impossible, state that limitation, run through the available shell, mirror output to a log when practical, and quote the key result lines plus the log path.
- **Kill processes by PID only**; never use `taskkill /IM` or `Stop-Process -Name`.
- **Zero warnings** at `/W4`; no exceptions.
- **Zero DX12 validation errors**; no exceptions.
- **DX12 is the only runtime renderer.** OpenGL and DX11 final parity evidence has been archived; do not add new runtime dependencies on those backends.
- **Renderer regression** is measured with DX12 screenshots plus zero DX12 validation errors, not GL/DX11 parity.
- **Physics must be deterministic**; byte-exact CSV match against baselines.
- **Reusable placeable assets must be registered assets.** New reusable props,
  buildings, terrain dressing, destructible structures, or multi-body placeables
  belong in `SkullbonezData/assets/*.assets.json` and must be registered from
  `Run::RegisterBuiltInAssets()` with an `assetlib.*` logical name. Do not add
  new hardcoded editor-only compound objects that scenes cannot reference by
  asset name. Editor UI affordances may wrap registered assets, but the asset
  recipe itself must live in the asset library data.
- **Asset hulls must be baked.** New or changed `.hull` files must be authored
  with `source_vertex`/`source_face` data and serialized with
  `tools\bake_hulls.py --write` so runtime metadata, faces, edges, mass, and
  inertia stay current.
- **Authored schema changes are versioned migrations.** Any schema change to
  scenes, asset libraries, hulls, or `engine.cfg` must bump that format's owned
  integer version, add its deterministic migration step, upgrade committed
  files, and extend the legacy/current/future/writer tests in the same commit;
  run `tools\migrate_data_formats.py --check` to verify non-scene authored data.
- **Scene use of reusable assets should go through `assetInstances[]`.** Avoid
  baking fresh copies of every generated part into scenes unless the scene is an
  intentional snapshot or regression fixture.

---

## Provenance-Only Golden Reconciliation

Standing owner ruling adopted 2026-07-17: a config-format/version bump
automatically authorizes provenance-hash-only reconciliation in committed
replay or visual golden metadata. A separate owner approval is not required
only when every condition below is satisfied:

- the replacement config hash is mechanically computed from the exact final
  authored config file that will be committed;
- every dependent hash change is mechanically derived solely from that
  provenance-field change;
- the diff changes hash fields only: no behavioral golden value, tick, sample,
  screenshot pixel, scene value, shader, artifact format, or physics baseline;
- the owning plan, report, or commit records the old/new hashes, the causal
  derivation, and the command or comparison evidence for that instance; and
- the mapped gate is rerun against the reconciled metadata and passes.

If any condition is unproved, if the config edit changes runtime behavior, or
if any non-hash golden field moves, revert the reconciliation and obtain
explicit owner approval under the normal baseline/golden rules. This standing
ruling does not authorize golden refreshes or weaken replay's one-process,
one-generation limits.

---

## Reviews

When asked for a review, prioritize findings over summary. List bugs, behavior
regressions, missing tests, validation gaps, baseline mistakes, physics
determinism risks, DX12 validation risks, and hot-path allocation concerns first,
ordered by severity with file and line references. Keep summaries secondary.
If no issues are found, say so clearly and name any residual validation or test
risk.

For bug fixes in subsystems that already have unit coverage, add or update a
regression test in the same commit unless the user explicitly scopes the work to
investigation or documentation only. If a regression test is not practical,
record the reason in the commit body or handoff.

---

## Commit Notes

When committing, write commit notes that are useful future handoff material, not a terse log line.

Every commit produced by a plan runner must begin with the exact progress
header defined by the authoritative ledger in
`Agentic/Plans/MASTER-PLAN.md`:

```text
<PLAN_NAME>, TASK <DONE> / <TASK_COUNT>, <OVERALL_PERCENT>% OVERALL COMPLETE — <ACTION SUMMARY>
```

For plan-runner commits, resolve all three values from the post-commit ledger
state before staging. The overall percentage is the rounded portfolio-done
total divided by the current portfolio task total; never estimate it. Every
plan-implementation prompt must include the fully resolved required subject
line. One plan owns each plan-runner commit; split unrelated work, and use the
MASTER-PLAN governance rule only for an unavoidable aggregate documentation or
governance commit. Commits made outside a plan runner use the normal subject
rules below and do not claim plan progress.

- Keep the action summary after the required progress header short and action-
  oriented. Conventional prefixes like `docs:`, `fix:`, or `feat:` may begin
  that summary when they fit.
- Add a body for anything beyond a trivial single-file cleanup. Explain what changed, why it changed, and the important implementation details by area.
- Mention validation explicitly, including the command run and the meaningful result. Do not reduce this to "tests passed."
- Call out baseline, artifact, or session-state updates when they are part of the change.
- Avoid vague messages such as "Update files", "Fix stuff", "Delete old files", or "misc changes."

On feature branches, commit and push without asking when the work is ready. On
`main`, show the proposed commit notes and changed-file summary first, then
commit or push only after explicit confirmation.

---

## Danger Zones

Changes to these areas require extra care. Before committing PR-bound changes,
run the specified targeted validation:

| Area | Risk | Required Validation |
|------|------|---------------------|
| DX12 resource barriers | GPU hang, corruption, CPU/GPU race | `validate_dx12_renderer` + verify `dx12_validation.txt` = 0 |
| Renderer backend regression | Visual divergence from committed DX12 references | `validate_dx12_renderer` screenshot diff |
| DX12 renderer gate | Future visual regressions after parity removal | `validate_dx12_renderer` + verify `dx12_validation.txt` = 0 |
| Per-frame heap allocations | Performance cliff, stall spikes | `validate_perf` + manual hot path review |
| Visual regression baselines | False passes hide real bugs | `validate_dx12_renderer` + intentional baseline update |
| Physics regression baselines | Stale baselines hide real behavior changes | Update only from final Debug artifacts, then rerun `validate_physics` or `validate_physics_deep` to match the baseline set |
| Matrix conventions | Entire scene renders incorrectly | `validate_dx12_renderer` |
| Physics determinism | Compiler, flags, inlining, SIMD, or gated-content drift can flip knife-edge branches; byte-exactness is certified per binary and pinned toolchain/content envelope | `validate_physics` byte-exact CSV diff; keep fixtures away from selection boundaries and record any motivating flip |
| Screenshot timing | Flaky non-deterministic captures | `validate_dx12_renderer` |
| Fixed-step simulation behavior | Physics replay not reproducible | `validate_physics` |
| Coordinate conventions | Upside-down textures, clip-space bugs | `validate_dx12_renderer` |
| Upload buffer / frame allocator | DX12 CPU overwrites in-flight GPU data | `validate_dx12_renderer` + run 3 consecutive times |
| DX12 graphics stress memory growth | Resource or cache leak, descriptor pressure, GPU memory growth | bounded `tools\run_graphics_stress.bat` with memory artifacts; overnight only when requested |
| Broadphase spatial grid | Missed collisions, perf regression | `validate_physics` + `validate_perf` |

---

## Build

```bat
REM Quick build (Profile, for validation):
tools\validate_build.bat Profile

REM Debug build (for physics logging / CDB debugging):
tools\validate_build.bat Debug
```

- **Platform:** x64 only; do not change.
- **Configurations:** Debug, Profile, Release.
- **Toolset:** v143 (VS2022).
- **Warning level:** `/W4`, zero warnings required.

---

## Project Structure

| What | Path |
|------|------|
| Solution file | `SKULLBONEZ_CORE.sln` |
| Source code | `SkullbonezSource/` |
| Shaders | `SkullbonezData/shaders/` |
| Test scenes | `SkullbonezData/scenes/` |
| Suite files | `SkullbonezData/scenes/*.suite.json` |
| Visual baselines | `TestOutput/baselines/*.png` |
| Physics baselines | `TestOutput/baselines/*.csv` |
| Perf baselines | `TestOutput/baselines/*_perf.json` |
| Validation scripts | `tools/` |
| Agent handoff docs | `Agentic/` |

---

## Agentic Handoff

The Agent Startup Contract names the required first-read files. Use
`Agentic/README.md` as the index for handoff docs, skills, plans, reports, and
reference material. Load skill files only when the current task needs them. Do
not load every skill at session start.
