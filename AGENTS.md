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
2. If CodeGraph MCP tools are available in the current Codex or Antigravity session,
   prefer them over shelling out. Use `codegraph_explore`/`codegraph_node` style
   tools for focused symbol, file, caller/callee, and impact lookups.
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

## Recorded Interaction Repro Workflow

When the user supplies an interaction manifest, the following workflow is
mandatory:

1. Preserve the manifest and every adjacent sidecar as read-only evidence. Run
   the exact recording before editing with:
   `Automation\SKULLBONEZ_CORE.exe --interaction-script "<manifest>" --interaction-report "<report>" --interaction-trace "<trace.jsonl>"`.
2. Confirm that the observed behavior matches the problem the user described.
   If it does not reproduce, report the mismatch; do not guess at the cause or
   alter the recording.
3. Treat the user's written expected behavior as the acceptance oracle. The
   recording captures actions and starting state, not what the correct outcome
   should be.
4. Inspect the JSONL trace when the final report is insufficient. Each flushed
   turn records the synthetic device state, ordered routed actions, and the
   post-frame scene/camera/input/UI observation.
5. After implementation, replay the exact unchanged manifest. Record the
   command, process exit result, report and trace paths, and observed outcome in
   the handoff.
6. Add a focused automated assertion whenever the expected result can be
   expressed mechanically. A recording supplements subsystem validation; it
   never replaces it.
7. Never refresh a physics, visual, replay, or other golden baseline merely to
   make a recorded interaction pass.

Recordings created under `TestOutput/recordings/` are local artifacts. Track a
recording as a regression fixture only when the user explicitly requests it,
and place that deliberate fixture with the owning test data rather than
weakening the `TestOutput` ignore policy.

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
3. State whether validation is required now. For intermediate task implementation work, do not run heavy validation suites (anything taking more than 1-2 minutes, such as full test suites, graphics stress, or deep regression gates); `validate_fast` or focused checks are sufficient while iterating. The primary objective is to complete the bulk of the implementation work as quickly and cleanly as possible. All heavy validation (`validate_tests`, full test runs, stress gates, rubber duck review, adversarial checks, refactors, and final fixups) is concentrated at the very end of the working plan in the terminal closure pass. For documentation-only changes, state that no validation is required.
4. If unrelated dirty files are present, leave them alone. Do not overwrite,
   revert, stage, or format user-owned changes unless explicitly requested.
5. For any source-bearing file you edit (`.cpp`, `.h`, `.hpp`, `.inl`,
   `.hlsl`, or substantial tool scripts), apply the comment standard in
   `Agentic/Reference/comment-style-guide.md`. A learning header alone is not
   enough: dense or risky code also needs nearby `Concept:`, `Why:`,
   `Invariant:`, `Lifetime:`, or `Hazard:` comments where the guide calls for
   them.
6. Apply `Agentic/Reference/code-style-guide.md`. Formatting tools own
   mechanical layout; reviewers own assertion placement and semantic parameter
   ordering.

## Dependency Direction Rule

Dependency direction must remain visible from physical include paths. `Core/`
is the infrastructure floor and must not include Assets, Gameplay, Physics,
Rendering, Scene, World, Runtime, or UI. Physics may include Core and Maths, but
must not include Assets, Gameplay, Scene, World, Runtime, or UI. Rendering may
include Core and Maths, but must not include Gameplay, Runtime, or UI. Gameplay
may include Core, Maths, and stable Physics/Rendering value or registration
seams, but must not include Assets, Scene, World, Runtime, or UI. Move a
misplaced value or implementation to its owning layer instead of adding a
forwarding header, compatibility alias, callback pack, service bag, or upward
include.

Authoritative dependency enforcement lives in
`tools/check_dependency_graph.py` with data in
`tools/dependency_graph_rules.json`. `tools/validate_dependency_graph.bat`
runs its positive/negative fixtures and repository scan through the mandatory
fast, CPU, full, and hosted gates. Add or change a direction only by updating
rule data, fixtures, and the generated proof block below in the same change; do
not hardcode a new checker branch or count budget. Regenerate the block with
the checker's write mode after an owner-approved rule-data edit.

Rendering contracts must remain feature-neutral. No type, constant, or function
under `SkullbonezSource/Rendering` may name Runtime feature domains such as
trajectory, replay, prediction, planning, cause trees, porkchops, or operator
panels. Feature owners supply layouts, capacities, and presentation data through
generic Rendering value contracts. The validator mechanically rejects the
retired `RetainedTrajectory` and `RETAINED_TRAJECTORY` names through the
generated content-rule projection below.

The following search is an explicitly qualitative Rendering vocabulary review
aid. It is not a mechanical policy mirror or a closure result by itself:

```powershell
rg -n --ignore-case 'trajectory|porkchop|replay|prediction' SkullbonezSource/Rendering
```

`PhysicsBodyRecord` and hot physics store arrays gain a per-body field only
with an owner ruling in the owning plan or commit body. The ruling must name the
consuming stage and explain why a stage-owned fixed-capacity parallel store is
insufficient. This is a review invariant rather than a frozen field-count or
spelling-budget check.

If an edge cannot be inverted in the owning task, record it in that task's
exception table with the owner, reason, and deletion condition. An unrecorded
edge or a compatibility spelling that hides it is a closure failure.

UI is a presentation library below Runtime and separate from Rendering. Runtime
may include UI to compose, route, and draw the operator surface; UI must include
neither Runtime nor Rendering. UI consumes detached value snapshots and emits
typed command values that Runtime owners apply. Move misplaced values into UI
or build them at the Runtime call site instead of hiding an upward edge behind
a forwarding header, alias, callback pack, service bag, or broad context object.

## Runtime Package Direction Rule

Physical sub-packages inside `SkullbonezSource/Runtime/` expose ownership and
must not collapse back into a flat Runtime god package. `App` is the composition
root; `RuntimeFrameViews.h` is the only allowed top-level source-bearing file
and contains values/forward declarations only. Runtime allow rows are
closed-world over the current rule data; a future package is rejected until an
owner-approved rule edit explicitly admits it.

<!-- DEPENDENCY_PROOF_START -->
<!-- Generated by tools/check_dependency_graph.py --write-proof AGENTS.md. Do not edit this block. -->

### Generated Dependency Proof

This is a deterministic projection of `tools/dependency_graph_rules.json`.
Broad prefixes match the named normalized path and every descendant.
Runtime package rows are different: rank constrains direction, but a
cross-package include is legal only when its exact target file (or mixed
exact source/target pair) is listed. Self-package includes remain legal.

#### Broad And Boundary Include Rules

| Rule | Source | Source kind | Mode | Target scope | Denied prefixes | Allowed prefixes | Allowed exact files |
|---|---|---|---|---|---|---|---|
| core_floor | Core | prefix | deny | (none) | Assets, Gameplay, Physics, Rendering, Runtime, Scene, UI, World | (none) | (none) |
| gameplay_direction | Gameplay | prefix | deny | (none) | Assets, Runtime, Scene, UI, World | (none) | (none) |
| physics_direction | Physics | prefix | deny | (none) | Assets, Gameplay, Runtime, Scene, UI, World | (none) | (none) |
| rendering_direction | Rendering | prefix | deny | (none) | Gameplay, Runtime, UI | (none) | (none) |
| replay_downward_boundary | Core, Physics, Rendering, Scene, World | prefix | deny | (none) | Runtime/Planning, Runtime/Prediction, Runtime/Replay | (none) | (none) |
| ui_direction | UI | prefix | deny | (none) | Rendering, Runtime | (none) | (none) |

#### Runtime Package Rules

| Rank | Source package | Allowed exact cross-package target files |
|---|---|---|
| 0 | Runtime/Debug | (none) |
| 1 | Runtime/RuntimeFrameViews.h | (none) |
| 2 | Runtime/Startup | (none) |
| 3 | Runtime/Camera | (none) |
| 4 | Runtime/Interaction | Runtime/Camera/RuntimeCameraMode.h |
| 5 | Runtime/Input | Runtime/Camera/CameraCollection.h, Runtime/Camera/CameraControlState.h, Runtime/Interaction/RuntimeInteractionController.h |
| 6 | Runtime/Simulation | (none) |
| 7 | Runtime/Scene | Runtime/Camera/AttachedCameraController.h, Runtime/Camera/CameraCollection.h, Runtime/Camera/CameraControlState.h, Runtime/Input/Input.h, Runtime/Input/InputRouter.h, Runtime/Simulation/SimulationSystem.h |
| 8 | Runtime/Replay | Runtime/Camera/RuntimeCameraMode.h |
| 9 | Runtime/Prediction | Runtime/Replay/ReplayIdentity.h, Runtime/Replay/ReplayPathPackets.h, Runtime/Replay/ReplayRetainedMemory.h, Runtime/Replay/ReplayTrajectoryPackets.h, Runtime/Replay/ReplayVisualPacket.h |
| 10 | Runtime/Planning | Runtime/Interaction/RuntimePickService.h, Runtime/Prediction/ContinuousPredictionProducer.h, Runtime/Prediction/ReplayPredictionView.h, Runtime/Replay/ReplayAuthoringPackets.h, Runtime/Replay/ReplayCapturePackets.h, Runtime/Replay/ReplayPathPackets.h, Runtime/Replay/ReplayPresentationPackets.h, Runtime/Replay/ReplayTimelinePackets.h |
| 11 | Runtime/Tools | Runtime/Camera/CameraCollection.h, Runtime/Camera/RuntimeCameraMode.h, Runtime/Input/InputRouter.h, Runtime/Interaction/RuntimeInteractionCommands.h, Runtime/Interaction/RuntimeInteractionController.h, Runtime/Replay/ReplayEventCommand.h, Runtime/Replay/ReplayToolPackets.h, Runtime/Replay/ReplayVisualPacket.h, Runtime/Scene/SceneLifecycle.h, Runtime/Scene/SceneSessionState.h, Runtime/Scene/SceneWorld.h |
| 12 | Runtime/Editor | Runtime/Camera/CameraCollection.h, Runtime/Camera/RuntimeCameraMode.h, Runtime/Input/InputController.h, Runtime/Input/InputRouter.h, Runtime/Interaction/RuntimeInteractionCommands.h, Runtime/Interaction/RuntimeInteractionController.h, Runtime/Interaction/RuntimePickService.h, Runtime/Replay/ReplayAuthoringPackets.h, Runtime/Scene/SceneAuthoredSetup.h, Runtime/Scene/SceneController.h, Runtime/Scene/SceneControllerState.h, Runtime/Scene/SceneEntityStore.h, Runtime/Scene/SceneGeneratedSetup.h, Runtime/Scene/SceneSaveOperations.h, Runtime/Scene/SceneSessionState.h, Runtime/Scene/SceneWorld.h, Runtime/Tools/RuntimeFileWriter.h |
| 13 | Runtime/Render | Runtime/Planning/ReplayOverlayPackets.h, Runtime/Replay/ReplayVisualPacket.h, Runtime/RuntimeFrameViews.h |
| 14 | Runtime/Diagnostics | Runtime/Scene/SceneLifecycle.h |
| 15 | Runtime/DevelopmentTools | Runtime/Planning/ReplayOverlayPackets.h |
| 16 | Runtime/Capture | Runtime/Scene/SceneLoadRequest.h |
| 17 | Runtime/Direction | Runtime/Tools/RuntimeFileWriter.h |
| 18 | Runtime/Automation | Runtime/Camera/RuntimeCameraMode.h, Runtime/DevelopmentTools/ImGuiEditorOwner.h, Runtime/Direction/DemoDirector.h, Runtime/Direction/DemoDirectorPlayback.h, Runtime/Input/Input.h, Runtime/Interaction/RuntimePickService.h, Runtime/Prediction/ReplayPrediction.h, Runtime/Prediction/ReplayPredictionArchive.h, Runtime/Prediction/ReplayPredictionDrawing.h, Runtime/Prediction/ReplayPredictionPackets.h, Runtime/Replay/ReplayPresentation.h, Runtime/Replay/ReplayTimelinePackets.h, Runtime/Replay/ReplayV2Artifact.h, Runtime/Replay/ReplayVisualPacket.h, Runtime/Replay/ReplayVisualPacketFingerprint.h, Runtime/RuntimeFrameViews.h, Runtime/Scene/SceneAutomationGateConfiguration.h, Runtime/Scene/SceneLifecycle.h, Runtime/Scene/SceneSleepingDynamicBodyGatePolicy.h, Runtime/Tools/RuntimeFileWriter.h |
| 19 | Runtime/UI | Runtime/Planning/ReplayOverlayPackets.h, Runtime/RuntimeFrameViews.h |
| 20 | Runtime/App | Runtime/Automation/InteractionAutomationController.h, Runtime/Automation/InteractionAutomationRecorder.h, Runtime/Automation/InteractionRecordingBrowser.h, Runtime/Automation/RuntimeValidationHarness.h, Runtime/Camera/AttachedCameraController.h, Runtime/Camera/CameraCollection.h, Runtime/Camera/CameraControlState.h, Runtime/Camera/RuntimeCameraMode.h, Runtime/Capture/CaptureSystem.h, Runtime/Capture/RuntimeStressController.h, Runtime/DevelopmentTools/ImGuiEditorLayoutPolicy.h, Runtime/DevelopmentTools/ImGuiEditorOwner.h, Runtime/Diagnostics/DiagnosticsPhysicsUI.h, Runtime/Diagnostics/DiagnosticsRuntime.h, Runtime/Diagnostics/OverlayDebugState.h, Runtime/Diagnostics/RuntimeDiagnostics.h, Runtime/Diagnostics/RuntimeOverlayDiagnostics.h, Runtime/Diagnostics/SceneMemoryDiagnostics.h, Runtime/Direction/DemoDirectorPlayback.h, Runtime/Direction/LookLabController.h, Runtime/Editor/EditorTools.h, Runtime/Input/Input.h, Runtime/Input/InputController.Bindings.h, Runtime/Input/InputController.h, Runtime/Input/InputRouter.h, Runtime/Interaction/OperatorCommandTransaction.h, Runtime/Interaction/RuntimeInteractionCommands.h, Runtime/Interaction/RuntimeInteractionController.h, Runtime/Interaction/RuntimePickService.h, Runtime/Planning/ContinuousOrbitalForecast.h, Runtime/Planning/ReplayOverlayPackets.h, Runtime/Planning/ReplayOverlayRenderer.h, Runtime/Planning/ReplayPlanningRuntime.h, Runtime/Prediction/ReplayPrediction.h, Runtime/Prediction/ReplayPredictionRetainedGeometry.h, Runtime/Prediction/ReplayPredictionRetainedMemory.h, Runtime/Prediction/ReplayPredictionScheduling.h, Runtime/Prediction/TrajectoryStore.h, Runtime/Render/RenderDefaultsStore.h, Runtime/Render/RenderModelFramePublisher.h, Runtime/Render/RuntimeRenderHost.h, Runtime/Render/RuntimeRenderer.h, Runtime/Replay/ReplayAuthoring.h, Runtime/Replay/ReplayCoordination.h, Runtime/Replay/ReplayIdentity.h, Runtime/Replay/ReplayOverlayLayout.h, Runtime/Replay/ReplayPresentation.h, Runtime/Replay/ReplayPresentationPackets.h, Runtime/Replay/ReplayProbeState.h, Runtime/Replay/ReplayRecorder.h, Runtime/Replay/ReplayRestoreService.h, Runtime/Replay/ReplayRestoreTransactions.h, Runtime/Replay/ReplayRetainedMemory.h, Runtime/Replay/ReplayScrubber.h, Runtime/Replay/ReplayTimeline.h, Runtime/Replay/ReplayV2Artifact.h, Runtime/Replay/ReplayVisualPacket.h, Runtime/Replay/ReplayVisualPacketFingerprint.h, Runtime/RuntimeFrameViews.h, Runtime/Scene/SceneCinematicPolicy.h, Runtime/Scene/SceneController.h, Runtime/Scene/SceneGeneratedControlTransaction.h, Runtime/Scene/SceneGeneratedSetup.h, Runtime/Scene/SceneLifecycle.h, Runtime/Scene/SceneLoadTransaction.h, Runtime/Scene/SceneSaveOperations.h, Runtime/Scene/SceneWorld.h, Runtime/Simulation/SimulationSystem.h, Runtime/Startup/StartupCommandLine.h, Runtime/Startup/StartupCrashLogging.h, Runtime/Startup/StartupLaunchResolution.h, Runtime/Startup/StartupProbeHarnesses.h, Runtime/Tools/RuntimeFileWriter.h, Runtime/Tools/RuntimeTools.h, Runtime/UI/RuntimeViewModel.h |

Composition root: `Runtime/App`.
Every cross-package target must have a lower rank; rank alone never grants permission.

#### Runtime Mixed Exact Edges

| Exact source file | Exact target file |
|---|---|
| Runtime/Automation/InteractionAutomationController.cpp | Runtime/Capture/CaptureController.h |

#### Runtime Repair-Plan Debt

Current pre-separation debt is sealed in rule data by source, line, resolved
target, include spelling, and policy fingerprint. The ordinary repository
gate accepts only an exact current seal and reports it as repair-plan debt;
a new, changed, shifted, or deleted site fails. `--check-runtime-graph`
ignores every repair row and fails every forbidden site and multi-package SCC.
Canonical repair-policy SHA-256: `396a362feb8d7e8a8d9cba94773f843e1c93fce2cc6a438f9d3c19bed4a28815`.

#### Content Rules

| Rule | Source | Source kind | Forbidden exact literals |
|---|---|---|---|
| rendering_retired_trajectory_vocabulary | Rendering | prefix | RETAINED_TRAJECTORY, RetainedTrajectory |

#### Project Ownership Rules

| Rule | Path prefix | Suffixes | Required project | Forbidden projects |
|---|---|---|---|---|
| ui_single_project_ownership | SkullbonezSource/UI | .cpp, .h, .hpp, .inl | SKULLBONEZ_UI.vcxproj | SKULLBONEZ_CORE.vcxproj, SKULLBONEZ_TESTS.vcxproj |

#### Executable Proof

The checker, not a second regular-expression parser, evaluates resolved
repository edges and verifies this block before repository validation:

```powershell
python tools/check_dependency_graph.py --check-proof AGENTS.md
python tools/check_dependency_graph.py --repo .
```

Bounded residual-parser fixtures prove that macro-expanded include operands
and backslash-continued directives are not parsed. Quoted and angle-bracket
operands are both recognized, but both use one local-first textual search
order rather than the compiler's different quoted-versus-angle semantics.
<!-- DEPENDENCY_PROOF_END -->

`InputRouter` is the only retained input routing/context/pointer owner.
`Input` samples devices, `InputController.Bindings` owns the immutable binding
table, `InputFrame` assembles frame-local values, `InputFrameExecution`
sequences the turn, `InputRouter.Interactions` remains part of the same router
owner, and stateless `InputController` applies mode/camera policy. Do not add a
second retained input state owner or move frame orchestration down into Input.

## Replay Boundary Rule

Replay, Prediction, and Planning are upper Runtime consumers, not type
providers to engine layers. `Physics/`, `Rendering/`, `Scene/`, `World/`, and
`Core/` must not include `Runtime/Replay/*`, `Runtime/Prediction/*`, or
`Runtime/Planning/*`. These packages read Physics through `PhysicsApi.h`,
`PhysicsEngine`, and Physics-owned value snapshots; render presentation crosses
as value packets or bounded queues. Do not move one of their types downward or
hide an upward edge behind a forwarding header, alias, callback pack, or broad
context object.

The generated mechanical proof above owns this Replay edge. Before closing
replay-facing work, run the dependency gate and review its resolved findings.

### Replay-Family Placement Rule

A new operator-facing feature built on predicted or recorded data belongs in
`Runtime/Planning/`, or in a future explicitly named product package above it.
It must not be placed in `Runtime/Replay/` or `Runtime/Prediction/`. Replay owns
recorded-data infrastructure; Prediction owns future-simulation production and
publication; neither is a miscellaneous home for trip analysis, launch-window
tools, product panels, or later operator workflows.

Review placement by responsibility and dependency direction. New retained
product state, commands, panels, or orchestration in Replay or Prediction are a
closure failure even if their names avoid planning vocabulary. Shared immutable
values stay in the lowest honest owner, while App may compose siblings. Do not
add frozen type counts, line budgets, or spelling ratchets to enforce this
rule.

Replay retains the only post-gameplay growth privilege, and only through a
`RuntimeReserveAllocator`-registered owner with a replay-phase check, hard cap,
logged growth counter, and owner-specific policy comment. The authoritative
replay-boundary plan carries the registration inventory: owner, phase gate,
cap, high-water/growth counter, policy comment, and status.
Adding a registration, increasing a cap, weakening a phase gate, or changing
counter coverage must update that inventory in the same commit. Reviews of
replay-touching work must answer both questions: did a downward Replay include
appear, and did a growth privilege appear or expand outside the inventory?
Either finding blocks the touching change.

## Comment Quality Gate

Comment quality is part of completion, not a follow-up nicety.

- If a task touches source for meaningful work, the source-writing agent applies
  `Agentic/Reference/comment-style-guide.md` once while implementing the file.
  Do not schedule a separate comment-audit pass or worker for ordinary
  implementation. The terminal rubber-duck reviews implementation-level truth
  and may block materially false ownership, sequencing, lifetime, unit, or
  hazard claims; it does not perform cosmetic wording review.
- Do not treat "file has a learning header" as full compliance. The body of the
  file must also teach local vocabulary, non-obvious ownership/lifetime rules,
  invariants, hazards, units, and validation-sensitive behavior.
- Every learning-header `Summary:` must add ownership, decision, or flow
  information beyond the filename. A filename restatement is not a summary.
- A glossary term defined in exactly one tracked source file remains local. A
  term defined in more than one tracked `.cpp`, `.h`, `.hpp`, `.inl`, or
  `.hlsl` file belongs in `Agentic/Reference/engine-glossary.md`; source
  learning headers cite that reference from `Related:` instead of copying the
  definition. `tools/inventory_glossary_terms.py` reports current duplicates and
  wording drift. Exact current rulings record migration ownership; they are not
  permission to retain copies or a count budget.
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
- Comments that assert ownership, sequencing, or subsystem behavior must name
  owners and paths that exist in the post-change source. When a change moves a
  responsibility, correct every touched comment describing the previous owner
  in the same commit.
- Repository-relative `Related:` entries must resolve, and must point at
  durable targets: source files, `tools/` scripts, `Agentic/Reference/`
  material, or a root document. Do not cite deletion-bound
  `Agentic/Plans/TODO/` plans, and do not reintroduce a per-commit report
  archive to link against; git history is the archive. `validate_fast`
  enforces the mechanical path-resolution portion.
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
the focused behavioral tests in `SkullbonezTests/` and `Agentic/Tests/`, and
the targeted validation gates below.

**Repeatable inventories are the instrument, not budgets.** Banning frozen counts
removed the wrong instrument but left nothing in its place, so shape rules were
enforced only when a human happened to notice. Seven tools now report current
structure without ratcheting anything:

| Tool | Reports | Owning rule |
|---|---|---|
| `tools/inventory_wide_signatures.py` | parameter counts plus current owner rulings per operation | 12-or-more qualitative owner-review trigger |
| `tools/inventory_authority_free_aggregates.py` | suffix-free data-bearing type discovery, members, behavior, stated invariants, sites | Invariant Ownership Rule |
| `tools/inventory_extraction_scars.py` | function-block member-prefixed locals, pure parameter aliases | Extraction Scar Rule |
| `tools/inventory_function_complexity.py` | function body lines, maximum brace depth, closure count, current-body owner rulings | Function Complexity Ownership Review Rule |
| `tools/check_build_config_consistency.py` | effective C++ project metadata, shared-source divergence, dropped list inheritance | Build Configuration Consistency Rule |
| `tools/inventory_unreachable_symbols.py` | Debug/Profile decorated-symbol reachability, test-only and unrooted same-TU definitions, exact current rulings | Symbol Reachability Ownership Review Rule |
| `tools/inventory_glossary_terms.py` | multi-file learning-header definitions, wording drift, exact current site/wording rulings | Comment Quality Gate glossary split rule |

All seven outputs are **current measurements requiring review**, never
allowances. The aggregate and extraction-scar inventories use the shared
unruled-fails/ruled-passes gate backed by
`tools/aggregate_ownership_rulings.json`. The wide-signature inventory uses
`tools/wide_signature_ownership_rulings.json`; a signature at or above the
review trigger must match a current ruling by file and normalized signature.
The function-complexity inventory uses
`tools/function_complexity_rulings.json`; a triggered body must match by file,
normalized signature, and full-body digest. The build-configuration inventory
uses `tools/build_config_rulings.json`; a shared file/setting pair must match
the digest of every current cross-project configuration variant, while dropped
list inheritance is always a defect and cannot be ruled away. The reachability
inventory uses `tools/reachability_rulings.json`; its file/signature identity
must match the current definition, and a repair ruling must name a live plan.
The glossary inventory uses `tools/glossary_term_rulings.json`; each exact term
must match the current definition-site file, line, and wording fingerprint, and
every repair ruling names the live consolidation plan. Historical dispositions
never satisfy any gate. Never convert any inventory into a count threshold, ratio,
or "no more than N" budget, and never add a ruling merely to make a number look
better — a row records a judgement and names the plan that owns the repair.

Any review that `AGENTS.md` delegates a rule to must state that rule in the skill
file the reviewer actually reads. A rule that exists only here, while
`Agentic/Skills/rubber-duck/SKILL.md` and
`Agentic/Skills/carmack-test/SKILL.md` say nothing about it, is unenforced in
practice.

## Symbol Reachability Ownership Review Rule

An ordinary out-of-line first-party function definition in a `.cpp` file, with
a matching first-party header declaration and no Debug or Profile production
reference outside its own translation unit, triggers qualitative owner review.
Constructors, destructors, operators, inline/header definitions, and
internal-linkage helpers remain review-owned outside this inventory. This is a
review trigger, not proof that the symbol is dead: virtual dispatch, callbacks,
exports, compiler-generated uses, and a source-to-symbol join uncertainty must
be resolved before deletion.

`tools/inventory_unreachable_symbols.py` runs after current Debug and Profile
builds. Decorated COFF names preserve overload type identity after each
configuration's preprocessor has run. The source pass supplies same-TU helper
edges; compiler objects own production cross-TU and `SKULLBONEZ_TESTS`
references. Standalone `Agentic/Tests` projects contribute masked lexical test
edges because their objects live outside the Debug/Profile roots. Every row is
classified as no-reference, test-only, own-TU-only, or own-TU-and-test-only.

For every row, the reviewer must answer:

1. Which concrete module or invariant owns the symbol?
2. What exact production invocation mechanism reaches it, if any?
3. Is a test proving a deliberately exposed invariant seam, or manufacturing
   reachability for retired surface?
4. Does an unrooted same-TU component have a live entry, or should it be
   internalized/deleted?
5. Is the exact current ruling `retain-owner`, or does `repair-plan` name the
   active plan that owns adjudication and deletion?

An unruled row, stale signature ruling, or missing repair plan fails
`validate_fast`. A ruled row is current judgement, not an allowance. Never
convert the row count into a ceiling, target ratio, or ratchet.

## Wide Signature Ownership Review Rule

An operation with 12 or more parameters triggers mandatory qualitative owner
review. Twelve is the point at which review becomes compulsory; it is not a
maximum, a safe allowance, or an automatic defect. A signature above the
trigger may pass when the ruling proves one cohesive operation, while an
exact-12 signature remains a blocking design defect when its responsibilities
or authority are unowned.

For every triggered signature, the reviewer must answer:

1. Which concrete owner or invariant-owning phase owns the operation?
2. Do all participant borrows and outputs have one synchronous lifetime, or
   does the operation span unrelated responsibilities?
3. Would shortening the signature introduce a courier, capability-slice set,
   callback pack, service/context bag, pure forwarder, or owner reach-back?
4. Does the callee immediately destructure an aggregate or preserve an
   extraction scar instead of moving design?
5. Is the current ruling `retain-owner`, or does `repair-plan` name the active
   plan that owns deletion/decomposition?

`tools/wide_signature_ownership_rulings.json` is exact current-source evidence.
Changing a file/signature pair invalidates its ruling; deleting or narrowing a
signature makes its old ruling stale. An unruled trigger row, a stale ruling,
or a repair ruling without an owning plan fails `validate_fast`. Passing the
mechanical gate only proves that somebody made a current, reviewable judgement;
an independent reviewer may disagree with the reason and reopen the work.

## Function Complexity Ownership Review Rule

A function with 400 or more inclusive body lines, or a maximum brace depth of
6 or more, triggers mandatory qualitative owner review. Either signal is
sufficient. These are points where review becomes compulsory, not maxima,
allowances, targets, or automatic defects; a shorter or flatter function may
still be badly owned. Brace depth includes the function body's outer brace, as
reported by `tools/inventory_function_complexity.py`.

For every triggered body, the reviewer must answer:

1. Which concrete owner or invariant-owning phase owns the complete body?
2. Does the body implement one cohesive synchronous operation, or does it span
   independently meaningful parsing, arbitration, mutation, publication, or
   lifecycle phases?
3. Would extraction create a real owner or independently testable algorithm,
   or merely move lines into a helper called once immediately by the original
   body?
4. Do nested branches encode one state machine or algorithm, or accumulate
   unrelated policy whose ordering is enforced only by caller discipline?
5. Is the current ruling `retain-owner`, with a concrete cohesion reason, or
   does `repair-plan` name the active plan that owns decomposition?

`tools/function_complexity_rulings.json` is exact current-source evidence.
Changing any body text invalidates its digest; renaming, moving, deleting, or
shrinking a triggered function makes its old ruling stale. An unruled body, an
edited body, a stale ruling, or a repair ruling without a canonical existing
Markdown plan under `Agentic/Plans/TODO/` fails `validate_fast`.

Passing the mechanical gate proves current judgement, not sound design.
Splitting a function into a helper that is called once immediately, moving the
same authority across sibling translation units, or adding a ruling merely to
clear the trigger is a review failure. The reviewer must follow the operation
across such helpers and reopen the ruling or owning plan when responsibility did
not move.

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
- an extracted function that preserves its pre-extraction body by rebinding
  parameters to member-prefixed locals, which moves code without moving design
  and is invisible to include and member checks (see the Extraction Scar Rule);
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
Distinguish banned bags from invariant owners per the Invariant Ownership Rule.

## Invariant Ownership Rule

Judge aggregate types by the invariant they own and enforce, not by their
shape or their effect on parameter count.

- A struct or class that groups unrelated-owner data or orchestrates
  multi-owner work is legitimate only when its header names the owned rule in
  an `Invariant:` block and a focused test exercises that rule.
- An aggregate that only carries data to shorten a signature is an
  authority-free bag and remains banned.
- An invariant-owning transaction stores values and a phase cursor, never
  long-lived owner pointers. Borrowed owners enter phase methods and expire
  when those calls return.
- Multi-step work whose correctness depends on call order must enforce that
  order in a type, using a phase cursor and fatal invariant on an illegal
  transition, rather than relying on comments or caller discipline.
- Three or more sibling input/participant/output structs combined with a wide
  apply free function and ordering/arbitration comments are an extrusion
  signal: the operation needs an invariant owner. Rearranging parameters is
  not a remediation.

### The Test Is Ownership, Not Spelling

Ask one question of every aggregate: **what rule does this type enforce that its
absence would let a caller break?** A type that has no answer is authority-free
and is banned no matter what it is named. Renaming a banned shape has never made
it legitimate, and the following cases are explicitly not exempt because their
names avoid the banned nouns.

- **A behavior-free aggregate whose only member borrows another owner is
  authority-free.** It cannot narrow representation or enforce identity, so it
  exists only to add a name. Take the pointer/reference directly. A one-field
  class that provides real behavior, or a strong value type that narrows a
  scalar into a tested domain identity, is not this shape.
- **An aggregate whose sole consumer destructures every member at entry owns
  nothing.** If the first thing the consumer does is copy members into locals or
  forward them onward unchanged, the type is a courier. This is the case the
  `RenderModelPassInput` example below records.
- **Two aggregates with identical member lists are one aggregate or none.** If
  they differ only by name and comment, neither is expressing an invariant.
- **An aggregate that states no invariant in its own doc comment is a review
  question, not automatically a defect.** Many types legitimately state their
  rules in the file header. But a type that both states no invariant *and*
  carries one of the shapes above is a defect, and the reviewer must say which.

The same question applies to reference-carrying view structs, judged as one
surface rather than one at a time. See Capability Slice Ownership below.

`tools/inventory_authority_free_aggregates.py` reports the mechanically decidable
part of this — member counts, stated invariants, and lexical construction and
consumer sites — with owner verdicts in
`tools/aggregate_ownership_rulings.json`. An **unruled** member of the bounded
gate defined below fails `validate_fast`; a **ruled** one passes, because an
owner has answered for it. Structural signals remain visible even outside that
bounded set. The inventory deliberately does not gate on the destructuring test, because
distinguishing a construction from a same-named local is not decidable without a
compiler database; that half stays a review question and a bad mechanical proxy
for it would recreate the frozen-metric failure this rule replaced. No count in
the inventory or the ruling file is a threshold, and adding a ruling row is never
a way to raise an allowance.

The permanent mechanical gate is deliberately bounded: every discovered
aggregate whose name uses a candidate suffix family and whose own documentation
states no `Invariant:` block requires a row in
`tools/aggregate_ownership_rulings.json` before it can land. The row must state
an ownership reason a reviewer can disagree with; “carries data for the frame
packet” or a restatement of the type name is not a ruling. Suffix-free discovery
and structural signals remain wider review context. A name-scoped gate is
evadable by calling a new bag `FooFrameData`, so this gate shrinks the evasion
surface rather than proving ownership; the review questions above remain
responsible for deliberately renamed bags.

## Capability Slice Ownership Rule

A set of reference-carrying view or slice structs is judged as **one surface**,
not one struct at a time.

- If any single operation receives every slice, the split is nominal and confers
  the authority of the whole surface. Decompose the operation or delete the
  slices; do not add a slice to make the count look smaller.
- A slice whose members are the borrowed concrete owners of one named subsystem
  is legitimate. A slice set that is a partition of a composition root's member
  list is not, whatever the individual slices are called.
- A slice convention must be the *only* convention on its path. If some
  operations take slices and others reach the same owners as members, the
  convention is decorative and the reviewer must report it: there is no rule a
  reader can apply to predict which form a given operation uses.
- A view struct may not state an invariant it does not hold. A header claiming
  "no slice spans the complete surface" while an operation receives every slice
  is a false claim and is repaired under the Comment Quality Gate, not left as
  aspiration.

## Extraction Scar Rule

A decomposition is judged by whether the extracted code reads as code written for
its new owner. Two shapes prove it does not, and both are closure failures:

- **A member-prefixed local.** Rebinding a parameter to an `m_`-named local
  preserves a pre-extraction spelling so a lifted body needed no internal edits.
  It also lies to every future reader: `m_candidatePairs` reads as owner state
  when it is a span whose lifetime ends at the next `return`.
- **A pure reference alias of a parameter.** A reference declaration that is only
  a second name for a parameter adds a name and nothing else. A *value* copy the
  body then mutates is different and legitimate; so is a binding genuinely
  required by the language, such as materialising a forwarding reference for a
  lambda capture. Both need a stated reason.

`tools/inventory_extraction_scars.py` reports both shapes with owner verdicts in
`tools/aggregate_ownership_rulings.json`, on the same unruled-fails/ruled-passes
contract as the aggregate inventory. It is not a count budget.

**Banned example — authority-free bag:** the rejected
`RenderModelPassInput` shape from the 2026-07-23 parameter-bag remediation
was immediately unpacked by its consumer. Deleting it only widened the
signature; it enforced no lifecycle, ordering, arbitration, or authority
rule.

**Legitimate example — invariant owner:** a transaction type may own a phase
cursor whose legal walk is tested, make an out-of-order phase call a fatal
invariant, and expose arbitration methods that replace free which-value-wins
helpers. Its header must name the exact phase-order and arbitration invariant
it enforces.

This rule does not relax the context-bag, callback-pack, owner reach-back, or
forwarding bans and does not bypass the wide-signature qualitative review
trigger. Passing a legitimate invariant owner as one parameter is not review
evasion because the header invariant and focused test independently establish
why the type exists.

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

Distinguish banned bags from invariant owners per the Invariant Ownership Rule.

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

`PhysicsFixedList` may commit or monotonically raise its runtime backing only
during `SceneLoad`, through `Reserve(count)` and a registered
`RuntimeReserveAllocator` physics owner. The requested runtime capacity must
remain beneath the template's compile-time ceiling. Appends never allocate:
crossing either the committed runtime capacity or the compile-time ceiling is a
fatal owner/capacity/high-water diagnostic.

Replay is the only runtime subsystem allowed to grow after steady gameplay
starts, and that growth must be approved by `RuntimeReserveAllocator` through a
registered owner, replay phase check, hard cap, logged growth counter, and
policy comment. Unregistered replay allocations are allocation-guard failures.
`PhysicsFixedList` backing copied into the isolated prediction engine follows
this existing Replay exception: an outer registered replay owner and granted
growth scope must already be active, and the list cannot create either scope.

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

## Determinism Math Policy

Physics-visible arithmetic must not depend silently on implementation-defined
`<cmath>` transcendental results. Static enforcement lives in
`tools/check_determinism_math_policy.py` with exact current-source judgements in
`tools/determinism_math_rulings.json`. The checker scans both
`SkullbonezSource/Physics` and `SkullbonezSource/Maths`; scanning Physics alone
misses shared Maths code that Physics invokes.

Basic arithmetic and comparison plus the certified exact or correctly-rounded
families (`sqrt`, `fabs`, `floor`, `ceil`, `trunc`, `round`, `copysign`,
`ldexp`, `frexp`, and `fmod`, including their standard float/long-double
spellings) pass directly. Every other `<cmath>` entry point requires an exact
current ruling. Explicit fused multiply-add also requires a ruling: the
operation is correctly rounded, but this repository deliberately disables
implicit contraction, so an explicit fused operation must record its owner and
intent.

A `retain-owner` ruling must name a concrete current purpose and prove the call
is not Physics-reachable. A Physics-visible call remains a `repair-plan` row
that names an existing canonical plan until the deterministic owner replaces
it. Unruled calls, stale source fingerprints, malformed rows, and missing repair
plans fail `validate_fast`. Rulings are reviewable judgements, not allowances;
never turn the inventory into a count threshold or frozen budget.

## Error Handling Policy

Exceptions are banned for engine code. The strict source throw inventory is
zero as of 2026-07-10. Do not add a new throw-count ratchet or frozen budget;
any new `throw` is a review failure.

| Category | Use For | Mechanism |
|---|---|---|
| Fatal invariant | Should-never-happen engine state in physics, stores, solver, frame loop, replay internals, or other owned runtime logic | `SB_FATAL(owner, ...)`; logs owner/diagnostics, flushes, breaks in Debug/Profile, and never returns |
| Recoverable error | External input or environment failure: scene/asset files, editor commands, automation input, device support, file IO | `SbResult`/future value-carrying result; operation fails and reports an owner/message to the UI or log boundary |
| Test probe | Validation, interaction, replay, scrub, and stress probes that should become machine-readable failures | Existing probe/report channel such as `FailAutomation(...)`, with interaction report `ok=false` and a failure message |

New fatal or recoverable paths must clarify their error category in source comments or the
owning plan when not obvious from the API being used.

## After Editing

Do not run validation scripts automatically after every edit. Formal repository
validation runs only as a pre-commit/PR gate, or when the user explicitly asks
for it. When preparing PR-bound work, choose the smallest cumulative set of
scripts from `tools\` that matches the fix. Broad or uncertain PR scope means
combining the affected focused gates; it does not authorize full validation.
Use deep, perf, and UI validation only when the change actually needs them.
Every DX12 modification also requires the mandatory bounded graphics-stress
run defined below.

`validate_full.bat --plan-completion` and its
`agent_validate.bat --plan-completion` alias are reserved for terminal
completion of an entire implementation plan, after independent review and
focused task gates. Both
entry points reject calls without the explicit token. The CPU umbrella includes
the ratified product coverage floors. A new standalone CPU test executable must join
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
  `tools\validate_full.bat --plan-completion`,
  `tools\agent_validate.bat --plan-completion`, and the hosted mandatory CPU CI
  lane through the same call chain.

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
| Broad or uncertain scope | `tools\validate_fast.bat` plus every affected focused gate | cumulative focused runtime |
| Entire implementation plan is complete | `tools\agent_validate.bat --plan-completion` | CPU tests + 5 engine processes |
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
| `Common.h` | `validate_fast` + `validate_all_cpu_tests` + every affected focused runtime gate |
| `SkullbonezTests/*`, `SKULLBONEZ_TESTS.vcxproj`, `SKULLBONEZ_TESTS.vcxproj.filters` | `validate_tests`; add `validate_coverage` when the tests are intended to raise subsystem coverage |
| `SkullbonezSource/Runtime/Replay/*`, `RunReplay*`, or replay-facing presentation/submission changes in `EditorTracer*` or `RuntimeRender*` | `validate_replay_visual_fidelity.bat` in addition to the normal mapped gate |
| `SkullbonezTests/TestReplay*` or replay artifact/presentation test changes | `validate_tests`, then `validate_replay_visual_fidelity.bat` |
| `SkullbonezData/scenes/prediction_ragdoll_wall_200.scene.json`, `SkullbonezData/interaction/prediction_ragdoll_wall_200_*.json`, or `TestOutput/baselines/replay_visual_fidelity_200_box*.json` | `validate_replay_visual_fidelity.bat` |
| `tools/check_replay_visual_fidelity.py`, `tools/replay_query.py`, `tools/validate_replay_visual_fidelity.bat`, or `tools/validate_replay_scrub.bat` | `validate_fast`, then `validate_replay_visual_fidelity.bat` |
| `Agentic/Tests/*` or a new standalone CPU test project/script | `validate_all_cpu_tests` |
| `Core/Allocation/*` | `validate_perf` |
| `tools/check_allocation_policy.py`, `tools/allocation_policy_allowlist.json` | `validate_fast`, then `python tools\check_allocation_policy.py --self-test` and `python tools\check_allocation_policy.py --repo .`; add `validate_perf` if runtime guard or reserve semantics change |
| `tools/check_determinism_math_policy.py`, `tools/determinism_math_rulings.json` | `validate_fast`, then `python tools\check_determinism_math_policy.py --self-test` and `python tools\check_determinism_math_policy.py --repo .` |
| `tools/inventory_authority_free_aggregates.py`, `tools/inventory_extraction_scars.py`, `tools/cpp_source_scan.py`, `tools/aggregate_ownership_rulings.json` | `validate_fast`, which runs both `--self-test` invocations, the aggregate repository scan in `--strict` mode, and the extraction-scar repository scan |
| `tools/inventory_function_complexity.py`, `tools/function_complexity_rulings.json` | `validate_fast`, which runs the complexity `--self-test` and current-tree `--strict` scan; then run the changed script directly |
| `tools/inventory_glossary_terms.py`, `tools/glossary_term_rulings.json`, `Agentic/Reference/engine-glossary.md`, `Agentic/Reference/comment-style-guide.md`, `Agentic/Skills/comment-style-audit/skill.md`, or `Agentic/Skills/rubber-duck/SKILL.md` glossary rules | `validate_fast`, which runs the glossary `--self-test` and current-tree `--strict` scan; then run `python tools\inventory_glossary_terms.py --self-test` and `python tools\inventory_glossary_terms.py --repo . --strict` directly |
| `tools/check_coverage.py`, `tools/coverage_floors.json`, `tools/validate_coverage.bat`, or coverage exclusions/instrumentation scope | `validate_fast`, then run `tools\validate_coverage.bat` directly |
| `tools/check_build_config_consistency.py`, `tools/build_config_rulings.json`, or any root first-party `*.vcxproj` | `validate_fast`, then `python tools\check_build_config_consistency.py --self-test` and `python tools\check_build_config_consistency.py --repo .` |
| `CMakeLists.txt`, `Core/PlatformWin32.h`, `Core/PlatformPosix.h`, or `Core/FloatingPointContract.h` portable-build behavior | `validate_fast`, then configure, build, and run the `skullbonez_portable_tests` CMake target |
| `tools/inventory_unreachable_symbols.py`, `tools/reachability_rulings.json`, or externally declared C++ symbol reachability | Build Debug and Profile, then `validate_fast`; run `python tools\inventory_unreachable_symbols.py --self-test` and `python tools\inventory_unreachable_symbols.py --repo . --strict` directly |
| `Run*`, `Runtime/*` | `validate_fast` plus every more-specific matching focused gate |
| `Window*` | `validate_fast` + `validate_automation` |
| `Init*` | `validate_fast` + `validate_automation` |
| `SkullbonezData/assets/*.assets.json` | `validate_fast` + the affected renderer/scene gate |
| `SkullbonezData/hulls/*.hull` | `validate_fast` + the affected physics/scene gate |
| `SkullbonezData/scenes/*.scene.json` | `validate_fast` + every affected physics/renderer/automation gate |
| Multiple areas or unsure | `validate_fast` + every more-specific matching focused gate; never infer plan completion |
| Documentation-only `Agentic/*` (excluding `Agentic/Tests/*`), `*.md`, docs | No validation required when documentation-only |
| `tools/*` | `validate_fast`, then run the changed script; `validate_fast` includes `validate_tests` |

---

## Rules

- **Repository validation scripts are PR/commit gates.** Do not run `tools\validate_*` merely as you go. During iteration, use targeted builds, launches, focused tests, or inspections only when they answer a specific question about the fix.
- **Full validation is a terminal plan gate.** Run `tools\agent_validate.bat --plan-completion` once only when an entire implementation plan is closing. Never run it for an intermediate task, ordinary commit/PR preparation, comment cleanup, documentation, or because scope is merely broad or uncertain.
- **Renderer validation must fail fast.** `tools\validate_dx12_renderer.bat` builds `Profile` first and must stop before launching DX12 if compilation fails. Renderer launches in that script use PID-scoped timeouts, then `tools\check_dx12_baselines.py` handles image comparison artifacts.
- **DX12-only validation is the production safety net.** `tools\validate_dx12_renderer.bat` builds `Profile`, launches only DX12, checks `dx12_validation.txt`, and compares captures against committed DX12 baselines.
- **Every DX12 modification requires a crash-free stress run of at least 10 seconds.** This applies to DX12 runtime source, shaders, resource/binding contracts, and DX12 validation tooling. The standard bounded command is `tools\run_graphics_stress.bat 1` (one minute); record its command, measured runtime, and successful exit evidence. A shorter custom launch is acceptable only when its measured runtime is at least 10 seconds and the process exits without a crash.
- **Never claim validation success without command output.** Paste the validation output when validation is required.
- **Never skip required pre-commit/PR validation** for code, tool, scene, shader, baseline, or runtime behavior changes unless the user explicitly says to.
- **Documentation-only changes require no validation.** Do not run `validate_fast` for prose-only edits.
- **Physics baselines are owner-controlled and immutable without explicit approval.** An agent must never regenerate, replace, or accept changed physics CSV, known-issue, or SkullScope goldens unless the owner explicitly approves that exact baseline transition after reviewing the behavior. A plan's divergence allowance, a passing determinism check, or an agent-authored closure report is not approval. When behavior differs without approval, restore the owner-approved oracle and repair the implementation.
- **An owner-approved physics baseline refresh requires a final physics gate.** Regenerate CSV or SkullScope baselines only from the final Debug executable, scene files, and config that will be committed, then rerun the matching gate after the baseline files are updated: `tools\validate_physics.bat` for the core varied-scene baseline, or `tools\validate_physics_deep.bat` for bullet sweep, shooting, known-issue, or SkullScope baselines. A copied physics artifact is not trustworthy until the gate compares it byte-exactly against the committed baseline.
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

Reviews must report the extrusion signal—three or more sibling structs, a
wide apply free function, and ordering/arbitration comments—as a design
finding with a proposed invariant owner. A parameter reshuffle does not close
that finding.

An ownership review must answer these five questions explicitly, and a review
that does not is incomplete rather than clean:

1. **Aggregate ownership.** Does every aggregate the change touches or adds name
   a rule it enforces? A behavior-free aggregate whose sole member is a borrowed
   owner, or one whose sole consumer destructures it at entry, is authority-free
   — say so and name the replacement.
2. **Capability slices.** Can any single operation reach the whole surface? Do
   some operations take slices while others reach the same owners as members?
3. **Extraction scars.** Does any local use the `m_` member convention, or exist
   only as a second name for a parameter?
4. **Rename evasion.** Did a shape the change deleted reappear under a different
   suffix? Deleting `FooContext` and adding `FooOperands` closes nothing.
5. **False claims.** Does any header state an invariant the post-change source
   does not hold?

Report a finding against these as `[Blocking]`. None of them may be waived as
follow-up debt, and none is closed by a rename or a parameter reshuffle. The three
repeatable inventories named in the Governance Review Model provide the evidence
for questions 1 and 3; cite their output rather than asserting a conclusion.

A test file is named for the subsystem whose behavior it pins, never for a gate,
a metric, or a plan. Coverage is raised by testing a subsystem, not by adding a
file organized around the coverage gate.

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
<PLAN_NAME>, TASK <DONE>/<TASK_COUNT> — <ACTION SUMMARY>
```

For plan-runner commits, resolve both counts from the post-commit ledger state
before staging; never estimate them. `<DONE>` counts the owning plan's
completed tasks after this commit, so the counter is plan-local and rises
monotonically to `<TASK_COUNT>/<TASK_COUNT>` on the closing commit.

Do not reintroduce a cross-plan percentage in the subject. The retired
`<OVERALL_PERCENT>% OVERALL COMPLETE` field divided portfolio-done by the
*current* portfolio total, so completing a plan removed that plan's finished
tasks from the numerator and every closing commit reported `0% OVERALL
COMPLETE`. A denominator that moves whenever a plan is added or deleted cannot
be compared across commits, and the field is redundant with `<DONE>/<TASK_COUNT>`.
Portfolio-level state belongs in `Agentic/Plans/MASTER-PLAN.md`, where it can
be revised, not frozen into commit subjects.

Keep the whole subject under 72 characters so it is not truncated in GitHub's
commit list; the action summary is the part a reader needs, so shorten the
action summary rather than dropping the counts. Every plan-implementation
prompt must include the fully resolved required subject line. One plan owns
each plan-runner commit; split unrelated work, and use the MASTER-PLAN
governance rule only for an unavoidable aggregate documentation or governance
commit. Commits made outside a plan runner use the normal subject rules below
and do not claim plan progress.

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

REM Every configuration the compiled-symbol gates read (Automation, Debug, Profile):
tools\validate_build_all.bat
```

The compiled-symbol reachability scan joins Automation, Debug, and Profile
objects and fails closed when any root predates current source. Build all three
after editing source before running that scan directly; `validate_fast` now does
this for you.

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
