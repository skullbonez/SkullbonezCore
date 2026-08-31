# Engine Signature And Context Cohesion

Date: 2026-08-31
Status: Active by explicit owner direction on 2026-08-31; 4/7 phases complete.
Impact area: all first-party C++ engine packages, tests, compiler-backed source
design inspection, dependency direction, and focused subsystem validation.
Owner: Engine architecture owner
Priority: Current owner-selected plan
Commit name: `SIGNATURE_COHESION`

## Owner Direction

Readable calls are worth additional source lines. Wide positional argument
lists and broad `*Context`, `*Input`, `*Args`, `*Services`, or similarly shaped
records must not be shortened by hiding the same unrelated values inside a
struct. Operations should instead expose a small number of meaningful concepts:
concrete owners, validated domain values, invariant-owning views, and genuinely
independent phases.

The owner activated this plan on `nightrunner-31st-AUG-26` and directed that
related changes be implemented in substantial batches rather than building
after each small signature edit. No phase authorizes a golden or baseline
refresh.

## Problem

The repository already blocks an edited operation with 12 or more parameters
and detects one obvious form of parameter-struct unpacking. Those checks protect
new changes, but they do not give the engine a complete readability pass:

- Existing wide operations outside the current diff are not reviewed.
- A function with 8–11 parameters can still be difficult to understand,
  especially when several adjacent values share a primitive type.
- A behavior-free record can reduce the visible parameter count while making
  every call site longer and less clear.
- Several smaller reference-bearing records can collectively expose the same
  broad owner surface as one large unrelated-value record.
- Moving a function onto a large class can hide the parameter list through
  unrestricted member reach without improving ownership.
- A single operation can contain independently meaningful configuration,
  selection, simulation, mutation, and publication phases that should be split
  instead of bundled.

The motivating example is `PhysicsBroadphaseStepInput`. The former call exposed
a long positional list. The replacement groups settings, joint rows, sleep
rows, awake rows, eligibility, angular expansion, diagnostics, timestep, skin,
and epsilon into one record. The record states a common synchronous lifetime,
but it does not enforce one rule across those unrelated values. Both forms are
hard to read.

## Goal

Review every first-party operation and parameter aggregate that matches the
candidate rules below, then repair each confirmed design problem so that:

1. A call reads in domain terms rather than as a sequence of plumbing values.
2. Each aggregate enforces a named rule that a caller could otherwise violate.
3. Stable configuration belongs to the concrete owner that applies it.
4. Frame-local views expose only aligned data needed by one algorithm.
5. Independent work is split into independently testable operations or phases.
6. Diagnostics and output dependencies are narrowed to the capability actually
   consumed.
7. No repair introduces a callback pack, service locator, forwarding facade,
   composition-root reach-back, or a differently named unrelated-value record.

Success is judged by cohesion and call-site readability, not fewer parameters
or fewer lines. A longer implementation is acceptable when it makes ownership,
units, invariants, and sequencing obvious.

## Non-Goals

- Do not replace every group of parameters with a struct. A short direct list
  is preferable when the values do not form a real concept.
- Do not require every function to have fewer than eight parameters. The
  inventory thresholds find review candidates; they are not a new build rule.
- Do not create a JSON permission registry, source-coordinate allowlist, frozen
  finding count, or permanent debt budget.
- Do not rename a bag and claim it is repaired. The test is the rule the type
  enforces and the responsibility the operation owns.
- Do not split one broad bag into several nominal slices if every consumer still
  receives all slices.
- Do not move parameters into class members merely to shorten a call.
- Do not store borrowed subsystem owners in long-lived transactions or values.
- Do not add builders whose only purpose is to assemble the old positional
  list somewhere less visible.
- Do not change simulation order, floating-point order, allocation timing,
  serialized layouts, public file formats, or renderer command order as part of
  a readability repair.
- Do not refresh Physics, Replay, visual, shader, or other golden baselines to
  make a refactor pass.

## Candidate Inventory Contract

The inventory is exhaustive for the structural signals below and is followed
by a qualitative package review for cases that numeric signals cannot identify.
Thresholds select questions for review; a match is not automatically a defect.

### Compiler-backed scan

Use the compile commands for every first-party project and configuration needed
to parse the complete engine. Headers must be checked under every distinct
first-party consumer project that changes macros, language flags, or visible
types. Deduplicate identical AST contexts, not physical header paths.

Extend `tools/check_source_design.py` with an advisory whole-engine inventory
mode if that can reuse its current Clang-Tidy and Clang Query setup cleanly.
Otherwise use a temporary local Clang AST query and do not add a second
repository checker. The inventory mode prints deterministic Markdown or TSV to
stdout, never becomes a required validation gate, and fails only when parsing
coverage is incomplete.

Collect these candidates:

- functions, methods, constructors, lambdas with explicit call surfaces, and
  free operators with eight or more parameters;
- operations receiving a record with four or more data fields;
- operations receiving one or more records that collectively carry three or
  more pointers, references, spans, callbacks, or owner-like handles;
- consumers that immediately unpack, alias, or forward several fields from a
  parameter record;
- multiple record types with identical or substantially overlapping member
  types and usage;
- operations that receive several sibling input/participant/output records;
- calls containing adjacent same-type scalar or Boolean arguments whose meaning
  is not visible at the call site;
- methods whose short signature is offset by reaching into several unrelated
  retained owners;
- record parameters used only to relay every field unchanged to another call;
- operations where only one field or capability is consumed from a much larger
  settings, diagnostics, renderer, scene, or runtime owner.

For each candidate, use CodeGraph plus source confirmation to capture all
callers, callees, package edges, tests, and dynamic-dispatch paths. Do not make
an API decision from the declaration alone.

### Qualitative package pass

Review every first-party package even if its AST inventory is empty. Inspect
public APIs and dense internal algorithms for:

- positional literals whose units or roles are unclear;
- groups of parameters that change together but lack validation;
- values passed together only because one large function performs several jobs;
- settings repeatedly passed through owners that already have an explicit
  configuration/update boundary;
- output parameters that should be typed results;
- optional Boolean combinations that admit invalid states;
- test helpers that conceal an awkward production API rather than exercise it
  clearly.

Record package coverage in the plan. “No candidate” is a valid result only
after both the compiler scan and qualitative pass cover that package.

## Candidate Review Worksheet

AFE0-style line counts and naming scans are insufficient. Each inventory row
must answer the design questions below:

| Field | Required answer |
|---|---|
| Symbol and owner | Exact operation/type and the concrete subsystem that owns it |
| Callers | Production, test, callback, ABI, and generated callers |
| Current surface | Direct parameters plus every field reachable through received bags/slices |
| Lifetimes | Which values share one synchronous lifetime and which do not |
| Rule | What rule a proposed type will enforce that separate arguments would let a caller break |
| Responsibilities | One cohesive operation, or independently meaningful phases |
| Stable configuration | Values that should be retained by an existing concrete owner |
| Narrow capability | Larger settings/diagnostic/owner arguments that can become a direct value or capability |
| Proposed call | Representative production and test call sites after repair |
| Evidence | Focused tests, determinism checks, allocation checks, and dependency checks |
| Decision | Repair, keep with concrete reason, or blocked pending an owner decision |

Every row requires a decision. “Keep” is appropriate for an externally fixed
ABI, compiler callback, or genuinely cohesive algorithm only when the exact
constraint is recorded. “Blocked” must name the missing owner decision; it is
not a silent deferral.

## Preferred Repair Patterns

Use the smallest pattern that makes the call truthful.

### 1. Retain stable configuration in its owner

If a stage already has `ApplyRuntimeSettings`, do not pass the complete settings
record to every frame merely to read one stable field. Store the validated
field in the stage and make configuration changes explicit at the existing
update boundary.

### 2. Introduce an invariant-owning value

Group scalars when they form a tested domain value: a finite non-negative time
interval, a bounded contact envelope, a normalized range, or a validated render
extent. Prefer named fields or factories when same-type positional values would
remain ambiguous. The type should validate or provide behavior for its rule,
not only store fields.

### 3. Introduce an aligned algorithm view

Several spans can form one view when the algorithm requires row alignment,
common capacity, shared indexing, and one synchronous lifetime. The view must
enforce or validate those properties and expose operations in domain terms.
It must not provide unrelated owner access.

### 4. Split independent operations

When parameters divide into independent parse, select, mutate, publish, or
render phases, split the behavior at that boundary. Preserve required order in
an owner or a phase-tracked transaction when callers could otherwise invoke
steps illegally.

### 5. Narrow a borrowed capability

Pass the trace recorder instead of the complete diagnostics owner, a settings
value instead of the complete config, or a typed result sink instead of a
renderer/runtime owner when that is all the callee uses.

### 6. Return a typed result

Replace unrelated output references and success flags with a result whose
states are valid by construction. Do not use a result record to collect values
that belong to separate operations.

### 7. Keep direct arguments when direct is clearer

Two or three independent operands often need no wrapper. A repaired call can
have more parameters than an artificially shortened one if every operand is
clear and the complete operation remains cohesive.

## Motivating Broadphase Target

The first implementation slice must use `PhysicsBroadphaseStepInput` to prove
the method. Confirm the live source at activation, then test this target shape:

- `PhysicsBroadphaseStage` retains validated broadphase configuration through
  its existing `ApplyRuntimeSettings` boundary instead of receiving the whole
  settings record per step.
- A `BroadphaseBodyActivityView` owns and checks the alignment/index rules for
  sleep state, awake indices, motion eligibility, and optional angular
  expansion. It offers behavior needed by broadphase instead of exposing an
  arbitrary collection of spans.
- A named broadphase sweep/contact-envelope value owns units, finiteness, and
  non-negative timestep/skin/epsilon rules.
- Point-joint constraints remain an explicit algorithm input unless the source
  proves they share an enforced rule with another value.
- `Run` receives `PhysicsPipelineTraceRecorder&` directly if that remains the
  only diagnostics capability it consumes.
- Production and focused-test calls use named values or designated fields so
  no sequence such as `1.0f, 0.0f, 0.0f` remains unexplained.

This is a design hypothesis, not a prescribed spelling. If source analysis
shows a better phase split or owner, record it in the candidate row and use the
same ownership tests.

## Current-tree Backlog

This is the initial concrete backlog requested by the owner. It was measured at
`f9bbedfcbc30` on 2026-08-29 with Clang Query under the effective Profile
compile context for all 279 first-party production translation units.

The implementation scan found 297 distinct operations with at least eight
parameters across 93 `.cpp` files. Nine operations currently have 12 or more
parameters. A separate header-as-main-file Clang Query scan found 217 matching
declaration or inline-definition sites across 76 headers/inlines. Many are the
declaration half of the same operation, but both physical edit sites are listed.
Approximate lines below identify this revision; refresh them during SC0 because
line movement is expected. Repeated template instantiations were deduplicated.
Four AST matches that resolve to one-parameter lambdas at the physical source
site are excluded as compiler-query noise:
`Runtime/DevelopmentTools/ImGuiEditorOwner.cpp:1215`,
`Runtime/Planning/ReplayOverlayRenderer.cpp:756`,
`Runtime/Replay/ReplayOverlayLayout.cpp:365`, and
`Runtime/App/ReplayValidation.cpp:1622`.

The suggestions are starting decisions, not permission to manufacture the
named types. Each proposed value/view must pass the rule-and-test worksheet
before implementation.

### Maths, Gameplay, and Physics operations

| File | Approx. definitions | Initial break-apart suggestion |
|---|---|---|
| `Maths/RotationMatrix.cpp` | 33 `RotationMatrix` constructor (9) | Replace nine positional floats with three named row/axis vectors or a validated `FromRows`/`FromColumns` factory; keep layout explicit. |
| `Gameplay/TornadoVisualPass.cpp` | 89 `EmitFxVertex` (10) | Append one typed FX vertex containing position, color, UV, effect kind, and terrain height; make the vertex layout own serialization order. |
| `Physics/ObjectContactManifold.cpp` | 722, 983, 1410, 1730, 1833, 2130, 2283, 2291 | Introduce tested SAT-axis identity and face-clip descriptors; represent one body/shape/index operand coherently; keep the output manifold explicit. Do not combine the whole contact algorithm into one bag. |
| `Physics/PersistentContactSolver.cpp` | 350, 784, 907, 1579; 2170 `Solve` (13) | Give the existing solve transaction phase methods, aligned solver-row views, and typed candidate/terrain inputs. Narrow diagnostics to the trace/stat sinks used by each phase. |
| `Physics/PhysicsBodyStore.cpp` | 148, 195, 664, 877 | Return a terrain-vertex query result instead of four output references; group body hot/cold/collider operands through validated row access and keep world-force policy separate. |
| `Physics/PhysicsWorld.cpp` | 803, 958 | Retain runtime settings in `PhysicsWorld`; split external-force preparation from solver execution; pass a validated step clock and direct store/worker owners rather than a frame service bag. |
| `Physics/Ragdoll.cpp` | 170 | Represent each constrained body operand as a checked record/hot-state/lever-arm tuple, then apply one impulse to the pair. |
| `Physics/SpatialGrid.cpp` | 1248, 1493, 1711, 1723 | Use one aligned body-activity view plus a validated contact/sweep envelope; collapse debug/non-debug overload plumbing without hiding output ownership. |
| `Physics/Stages/PhysicsBroadphaseStage.cpp` | 88, 182, plus `PhysicsBroadphaseStepInput` consumer at 438 | Apply the motivating target: retained cell configuration, aligned activity view, sweep/contact value, explicit joints, and direct trace recorder. Reuse those concepts in the two helpers. |
| `Physics/Stages/PhysicsForceStage.cpp` | 100, 125, 164; 416 `ApplyForces` (12), 447 | Separate force environment, aligned active-body rows, execution policy, and step clock. Per-body kernels should take a row index plus narrow accessors, not every store/span. |
| `Physics/Stages/PhysicsNarrowphaseStage.cpp` | 117; 280 `ProcessObjectNarrowphasePair` (12) | Resolve pair identity and available-time policy before the pair kernel; have the kernel consume checked pair bodies, wake behavior, and a typed event result. |
| `Physics/Stages/PhysicsNarrowphaseStage.Execution.cpp` | 57, 254 | Make serial/parallel execution share one narrow pair-work operation and keep worker scheduling outside its physics operands. |
| `Physics/Stages/PhysicsSleepController.cpp` | 410, 496, 740, 869 | Separate joint-island construction, sleep-decision evaluation, and transition application. Reuse checked body activity/wake behavior rather than threading all stores through every phase. |
| `Physics/Stages/PhysicsSleepController.Wake.cpp` | 169, 457, 499, 570 | Reduce duplicated wake-access constructor state derived from stores; expose wake operations on a synchronous capability whose retained fields are all required by those operations. |
| `Physics/Stages/PhysicsTerrainStage.cpp` | 107; 164 `Detect` (12), 203 | Split terrain sampling from contact candidate commit; use checked body-row access, a terrain query value, and a typed detection result instead of output/counter plumbing. |

### Rendering, Scene, World, and UI-library operations

| File | Approx. definitions | Initial break-apart suggestion |
|---|---|---|
| `Rendering/DX12/Dx12Diagnostics.cpp` | 46 `BindSources` | Bind independently owned diagnostic sources through explicit owner methods or publish detached snapshots; do not retain an eight-source service bag. |
| `Rendering/DX12/MeshDX12.cpp` | 54 `Create` | Use a validated mesh-create description for vertex/index bytes, strides, counts, and topology; keep resource builder/diagnostics direct. |
| `Rendering/DX12/RenderBackendDX12.DynamicGeometry.cpp` | 390, 485, 510, 587, 605, 617, 658, 752 | Separate upload/range validation from draw submission. Use real vertex-stream/range/topology/raster values; do not restore a behavior-free `*DrawRequest` parameter wrapper. |
| `Rendering/DX12/RenderBackendDX12.Pipeline.cpp` | 327, 545 | Make shader/layout/raster/depth identity a hashable validated pipeline key; make prepared draw state the result of PSO resolution rather than parallel parameters. |
| `Rendering/DX12/RenderBackendDX12.Textures.cpp` | 537 | Use a validated texture description for dimensions, format, mip/data layout, and flags; pass upload/resource owners directly. |
| `Rendering/PrimitiveBatchRenderer.cpp` | 368, 566, 575, 584, 840, 1028, 1134, 1242 | Separate stable shader/resource binding from per-view lighting/raster state; use one tested instance-layout value shared by sphere/box/pine/hull batches. |
| `Rendering/RenderInstanceRenderer.cpp` | 312, 546 | Publish a render-view value containing matrices/clip/light policy and split model selection from draw submission. |
| `Rendering/Text.cpp` | 593, 781, 915, 933, 954; 1012 `BatchTriangle` (12) | Introduce glyph/text style and typed colored vertex values; make quad/triangle emission consume vertices instead of positional coordinate/color runs. |
| `Scene/AuthoredSceneParserAssets.cpp` | 541 | Record one validated asset-part identity/transform value, with scene object id and source identity strongly typed. |
| `World/Terrain.cpp` | 78, 224, 695 | Separate validated height-map geometry from resource creation; return a creation result; publish one render-view value for matrices, clip, raster, cinematic, and shadow inputs. |
| `World/WorldEnvironment.cpp` | 252 | Split water camera/reflection values from render resources and fluid style policy; retain stable cinematic settings in the environment owner. |
| `UI/UIComboBox.cpp` | 100, 114 | Use combo options/selection and pointer values; eliminate duplicate overload plumbing and same-type coordinate/count arguments. |
| `UI/UIDraw.cpp` | 68, 74, 81, 90 | Replace coordinate/color runs with `UIRect`/triangle points, `UIColor`, and corner-radius values. These are low-level cohesive drawing values, not contexts. |
| `UI/UIDrawList.cpp` | 47, 68, 90, 190 | Mirror the `UIDrawContext` geometry/color values in retained commands; use a preview fallback style value for image fallback fields. |
| `UI/UIDrawWidgets.cpp` | 385, 588, 656, 700, 968, 995, 1009, 1018 | Use existing bounds/layout types plus named slider range, combo selection, label/value style, and footer-stat values. Keep draw owner direct. |

### Runtime App and feature orchestration operations

| File | Approx. definitions | Initial break-apart suggestion |
|---|---|---|
| `Runtime/App/CameraFrameApplication.cpp` | 113 | Separate camera movement input from world support/clamp policy; retain movement settings in `CameraControlState`. |
| `Runtime/App/GraphicsStressApplication.cpp` | 83, 197 | Split presentation action application from runtime/capture mutation; use typed action variants instead of parallel flags and owners. |
| `Runtime/App/Init.cpp` | 169 `RunApp` | Keep process composition direct, but move startup option resolution and subsystem construction into their concrete owners; do not create an app-services bag. |
| `Runtime/App/InputFrame.cpp` | 1318 | Finish pointer routing through a typed pointer result and narrow UI/interaction operations rather than ten state/output parameters. |
| `Runtime/App/InputRouter.Interactions.cpp` | 96, 168, 209, 442, 606, 658, 773 | Turn mode/owner transitions into typed transition commands with valid states; split focus-loss cleanup, camera mode, and post-UI dispatch by owner. |
| `Runtime/App/InteractionAutomationApplication.cpp` | 1250, 1304, 1417, 1659, 5116, 5319, 5488, 5603, 5615, 5694, 5774 | Preserve one authored action traversal while each action variant calls a narrow direct handler. Move report/script/after-render work to Automation owners; no multi-owner automation context. |
| `Runtime/App/InteractionAutomationReportApplication.cpp` | 1281 `Write` | Separate immutable report content from filesystem publication; make optional sections typed and let the writer own path/file policy. |
| `Runtime/App/OperatorEditorFramePhase.cpp` | 238 | Project secondary diagnostics into detached values per diagnostic owner, then compose those values; do not pass ten live owners into sampling. |
| `Runtime/App/ReplayAuthoringCauseTree.cpp` | 125, 323, 1066 | Move cause-tree construction/activation to Planning; separate body-position lookup, row building, and activation commands. |
| `Runtime/App/ReplayCauseFocusSubmission.cpp` | 186 | Give overlay rendering a cause-focus presentation value and render target; keep prediction/replay owners out of the draw call. |
| `Runtime/App/ReplayPredictionDrawing.cpp` | 164, 390, 618, 819, 853, 946, 1036, 1179, 1507, 1560, 2348 | Split path color/style, source trajectory view, geometry emission, and capacity accounting. Move feature drawing to its Planning presentation owner. |
| `Runtime/App/ReplayRuntime.cpp` | 453, 1041, 1228, 1297, 1353, 1591, 2018 | Split replay transport, pointer routing, capture, prediction scheduling, and scene lifecycle into existing owners; App composes typed commands/results only. |
| `Runtime/App/ReplayScrubberTools.cpp` | 269, 359, 493, 554, 782, 847, 1205, 1243, 1339, 1414, 1435, 1490, 1645, 1679, 1717, 1929; 2149 `TickScrubberInput` (12) | Separate camera transition, timeline mutation, transport commands, cause selection, and drag state machines. Each state machine owns its operands and returns typed commands. |
| `Runtime/App/ReplayValidation.cpp` | 332, 415, 620, 814, 902, 964, 1042, 1107, 1208, 1234, 1270, 1332, 1453, 1566, 1595 | Split event application, topology preparation, target stepping, hash comparison, and divergence formatting. Move validation algorithms below App where dependency direction permits. |
| `Runtime/App/ReplayValidation.Probes.cpp` | 673 `TickProbes` (13), 1134, 1752 `AdvanceStartupProbeWorkflows` (12), 2077 | Give each probe a phase-tracked state machine and narrow scene/replay operations. App selects/advances probes without receiving every probe participant. |
| `Runtime/App/Run.cpp` | 291 constructor | Keep direct construction wiring or introduce real subsystem-owned constructors; never replace it with `RunServices`/`RunContext`. Review whether any constructor operands can be created by existing owners. |
| `Runtime/App/SceneLoadApplication.cpp` | 564 | Split scene-load result application into presentation publication, renderer epoch change, and UI navigation update commands. |

### Runtime owner operations below App

| File | Approx. definitions | Initial break-apart suggestion |
|---|---|---|
| `Runtime/Capture/CaptureController.cpp` | 101 | Make auto-cycle schedule a validated clock/range value and return a capture command; keep render/camera mutation outside. |
| `Runtime/Capture/CaptureSystem.cpp` | 421, 498 | Separate trigger schedule, filename/publication policy, and capture backend; return typed capture outcomes. |
| `Runtime/Diagnostics/RuntimeDiagnostics.cpp` | 46 constructor (10) | Build diagnostic facts from detached subsystem snapshots or smaller domain projections; do not retain ten live diagnostic sources. |
| `Runtime/Editor/EditorInteractionTools.cpp` | 1240, 1455 | Separate pointer ray/hit testing, gizmo gesture state, and command emission; use typed pointer/hit/gesture values. |
| `Runtime/Editor/EditorObjectPlacement.cpp` | 101, 164 | Introduce a validated editor body description and placement pose; return the placement command/result instead of parallel flags. |
| `Runtime/Planning/ReplayCauseInspection.cpp` | 1503 | Put pointer/layout values in an input event and keep solver-detail selection state in the inspection owner. |
| `Runtime/Prediction/ReplayPrediction.cpp` | 743, 885, 1037, 1312 | Split source snapshot, simulation seed, worker step, and publication. Use phase-owned prediction job values without storing live scene/physics owners. |
| `Runtime/Prediction/ReplayPredictionArchive.Automation.cpp` | 171 | Use one archive rejection fixture/result value; keep this test-only helper out of production API decisions. |
| `Runtime/Prediction/ReplayPredictionArchive.cpp` | 1664 | Separate archive bytes/schema validation, retained-state transaction, and publication result. |
| `Runtime/Prediction/ReplayPredictionPublication.cpp` | 233, 478, 549, 1041 | Use typed trajectory builders that own source identity/range/capacity rules; split record creation from frame append/publication. |
| `Runtime/Prediction/ReplayPredictionTopologyPublication.cpp` | 156, 219, 400, 464, 512, 628, 791 | Replace the local future-context pointer bag with a topology builder owner; separate node allocation/linking, markers, cache update, and overlay projection. |
| `Runtime/Render/RuntimeRenderer.cpp` | 458, 1213 | Split ghost-presentation data from render resources; make scene-target begin/graph execution explicit phases on the renderer owner. |
| `Runtime/Render/RuntimeRenderPasses.cpp` | 855, 1620, 1631, 1779, 1875 | Give each pass a pass-specific immutable input and direct resource owner; share camera/light values without passing the complete render context to every pass. |
| `Runtime/Render/UiDrawSubmission.cpp` | 71, 92, 98, 158, 207, 216, 228 | Use UI geometry/color/style values and separate preview-resource resolution from command submission. |
| `Runtime/Render/UIProfilerOverlayPresenter.cpp` | 61, 91 | Replace quad coordinate/color runs with a typed projected quad; project profiler samples before drawing. |
| `Runtime/Render/UiTextPass.cpp` | 93, 526, 554 | Split operator draw-list text, overlay text, and test pattern; consume typed text commands and a narrow target view. |
| `Runtime/Replay/ReplayAuthoringVelocity.cpp` | 522 | Make velocity editing a pointer event plus retained edit state that emits one typed velocity command. |
| `Runtime/Replay/ReplayOverlayLayout.cpp` | 476 | Return a scrubber availability/layout value instead of parallel output/label fields. |
| `Runtime/Replay/ReplayPresentation.cpp` | 459 | Separate past-trajectory sample selection from retained-geometry publication; use a bounded trajectory update value. |
| `Runtime/Replay/ReplayRecorder.cpp` | 1668, 1813, 2023, 2745 | Use typed event/editor-transform builders; split world presentation capture from solver evidence capture and keep reserve owners explicit. |
| `Runtime/Replay/ReplayTimeline.cpp` | 335 | Capture one timeline-frame input with validated frame identity and narrow presentation/solver samples; keep branch/event publication separate. |
| `Runtime/Replay/ReplayV2Artifact.cpp` | 2378 | Build manifest sections independently, then assemble a validated manifest value before serialization. |
| `Runtime/Scene/AttachedCameraController.cpp` | 803 | Use a follow-target pose and camera constraint policy; return a camera pose rather than several output components. |
| `Runtime/Scene/SceneAuthoredSetup.cpp` | 131 | Build one validated authored body description from material/shape/motion values. |
| `Runtime/Scene/SceneController.Load.cpp` | 449, 790, 1117, 1177, 1271 | Keep `SceneLoadTransaction` as the phase owner; give terrain/authored/generated preparation typed results and reduce `Load` to enforced phase transitions. |
| `Runtime/Scene/SceneGeneratedSetup.cpp` | 460 | Separate generated population specification from scene/storage owners; return a setup result. |
| `Runtime/Scene/SceneLoadTransaction.Preparation.cpp` | 117 | Replace Boolean combinations with a typed load mode and preservation policy; keep render frame optionality explicit. |
| `Runtime/Tools/EditorTracer.cpp` | 548, 619, 668, 685, 1228, 1302 | Use typed colored line/ribbon vertices, ribbon style, shape pose, and gizmo interaction state; eliminate RGB and axis-state runs. |
| `Runtime/Tools/MousePickupTools.cpp` | 56 | Split pointer-ray resolution from pickup mutation; use a resolved pickup ray/camera value and emit an interaction command. |

### Runtime UI operations

| File | Approx. definitions | Initial break-apart suggestion |
|---|---|---|
| `Runtime/UI/GameUI/UI.cpp` | 471 | Replace mode Booleans with one typed UI mode state; pass screen extent, clock, input snapshot, and camera choices as named values. |
| `Runtime/UI/GameUI/UIEditorMiniPaletteDraw.cpp` | 55, 395, 525 | Use palette entry/style/interaction values and screen layout; split tree icon drawing from palette selection logic. |
| `Runtime/UI/GameUI/UIFrameComposition.cpp` | 534 `BuildUIInteractionSignature` (12) | Hash a canonical `UIInteractionState` assembled from typed open-panel and selection values; do not expose 12 primitive hash arguments. |
| `Runtime/UI/GameUI/UITabCinematic.cpp` | 520, 607 | Use tab layout and pointer event values; keep retained cinematic tab state explicit. |
| `Runtime/UI/GameUI/UITabControls.cpp` | 73, 227 | Separate input hit testing from drawing; use controls-tab layout, pointer event, and solver summary values. |
| `Runtime/UI/GameUI/UITabEditor.cpp` | 130 | Use editor-tab layout and pointer values rather than six floats/two coordinates. |
| `Runtime/UI/GameUI/UITabMemory.cpp` | 258, 540, 582, 633, 1444, 1458 | Use typed line/stack/row presentation values; split memory data projection, policy interaction, and drawing. |
| `Runtime/UI/GameUI/UITabOptions.cpp` | 35, 82, 179 | Use a two-column tab layout and pointer event; replace row/column/coordinate runs with `UIRect` results. |
| `Runtime/UI/GameUI/UITabPhysics.cpp` | 65, 150, 589 | Share the options-tab layout/pointer concepts; keep Physics values in a tab-specific detached view. |
| `Runtime/UI/GameUI/UITabProfiler.cpp` | 584, 739 | Use profiler-tab layout, pointer event, and worker-count range values; split interaction from render. |
| `Runtime/UI/GameUI/UITabProfilerHistogram.cpp` | 460, 601, 660 | Use typed colored line/checkbox values and one pointer-wheel event; retain histogram state in its owner. |
| `Runtime/UI/GameUI/UITabScene.cpp` | 536, 564, 691, 713, 736, 753, 854 | Consolidate repeated combo hit/wheel operations around combo model/layout/event values; split recording, scene, and time-scale controls. |
| `Runtime/UI/GameUI/UITabSky.cpp` | 280, 362 | Use sky-tab layout/pointer values and a detached cinematic style view. |
| `Runtime/UI/GameUI/UIWindowInteractionOwner.cpp` | 599 | Replace ten duplicated mode arguments with one typed UI mode state and keep scene navigation as a direct value/owner boundary. |
| `Runtime/UI/OperatorUiProjection.cpp` | 284 | Project one replay-memory policy value with named durations/budgets/clamp status rather than seven primitive values. |

### Paired declarations and inline/header-only operations

These are the 76 physical header/inline files reported by the header scan.
“Same repair” refers to the owning implementation row above; the declaration
must change in the same slice. Header-only additions have their own suggestion.

| Header / inline and approximate lines | Initial break-apart suggestion |
|---|---|
| `Maths/RotationMatrix.h:40` | Same three-row/axis constructor or named factory repair as `RotationMatrix.cpp`. |
| `Physics/ObjectContactManifold.h:128,132,137,145` | Same checked body/shape operand and SAT/contact descriptor repair as the manifold implementation. |
| `Physics/PhysicsApi.h:90` | Make `MakePhysicsBodyCreateDesc` consume a validated authored-body specification split into shape, mass/motion, and identity values. |
| `Physics/PhysicsWorld.h:169,194` | Same retained settings, step clock, and phase split as `PhysicsWorld.cpp`. |
| `Physics/SpatialGrid.h:251,256,331,335,339` | Same aligned activity and contact/sweep query values as `SpatialGrid.cpp`. |
| `Physics/Stages/PhysicsContactSolverStage.h:188,194,200,210,309` | Same transaction phase and aligned solver-row repair as `PersistentContactSolver.cpp`. |
| `Physics/Stages/PhysicsForceStage.h:96,114,119` | Same force environment, active-row view, execution policy, and step clock repair. |
| `Physics/Stages/PhysicsNarrowphaseStage.h:175,193,200` | Same checked pair-work and scheduling split as the narrowphase implementation files. |
| `Physics/Stages/PhysicsSleepController.h:87,210,220,227,253,257,272,277` | Same wake-capability reduction and joint/decision/transition phase split as the sleep implementation. |
| `Physics/Stages/PhysicsTerrainStage.h:102,117,129` | Same terrain query/detection/commit result split as `PhysicsTerrainStage.cpp`. |
| `Physics/TerrainSupportClassifier.h:316,391` | Introduce a checked terrain-support query containing one body pose/shape and support policy; return the classification rather than thread output facts. |
| `Rendering/DX12/Dx12Diagnostics.h:62` | Same independent diagnostic-source binding/projection repair as the implementation. |
| `Rendering/DX12/MeshDX12.h:76` | Same validated mesh-create description. |
| `Rendering/DX12/RenderBackendDX12.h:371,536,577,625,639,643,778,783,788,795,800` | Same texture/pipeline/stream/range/topology repairs; do not add behavior-free draw-request structs. |
| `Rendering/PrimitiveBatchRenderer.h:252,295,301,307,321,344,351,359` | Same stable binding, render-view, and instance-layout values. |
| `Rendering/RenderInstanceRenderer.h:60,73,95` | Give the constructor only retained renderer dependencies; use the same render-view/model-selection split for draw operations. |
| `Rendering/Text.h:177,183,186,189,193,215` | Same text style and typed colored vertex/quad repair. |
| `Runtime/App/GraphicsStressApplication.h:91,97` | Same presentation/runtime action split and typed action variants. |
| `Runtime/App/InputFrame.h:140` | Same pointer result and narrow UI/interaction completion operations. |
| `Runtime/App/ReplayAuthoringCauseTree.h:47,53` | Same Planning-owned row-building and activation command split. |
| `Runtime/App/ReplayPredictionDrawing.h:212` | Same Planning-owned path visualizer values. |
| `Runtime/App/ReplayPredictionPresentation.h:113` | Same detached cause-focus overlay presentation value. |
| `Runtime/App/ReplayPredictionRetainedGeometry.h:242,267` | Same source trajectory/style/capacity-accounting geometry split. |
| `Runtime/App/ReplayRestoreOperations.h:296` | Capture a typed solver-hash input from replay/physics snapshots; keep archive/diagnostic formatting outside the hash operation. |
| `Runtime/App/ReplayRuntime.h:303,307,478,529,535,548,557,585,599,607,622,628,649,663,679,698,707,733,741,748` | Same transport/camera/capture/prediction/scene lifecycle decomposition as `ReplayRuntime.cpp` and `ReplayScrubberTools.cpp`. |
| `Runtime/App/Run.h:275,341` | Same direct composition constructor and detached diagnostics projection decisions as the implementations. |
| `Runtime/App/SceneLoadApplication.h:217` | Same typed scene-load presentation/application commands. |
| `Runtime/Automation/InteractionAutomationController.h:428` | Return validated output paths derived from script/report roots; separate path resolution from file publication. |
| `Runtime/Automation/InteractionAutomationReportWriter.h:312` | Same immutable report-content and filesystem publication split. |
| `Runtime/Camera/CameraControlState.h:139` | Same movement input versus world support/clamp policy split as `CameraFrameApplication.cpp`. |
| `Runtime/Capture/CaptureController.h:105` | Same validated auto-cycle clock/range and emitted capture command. |
| `Runtime/Capture/CaptureSystem.h:113,117` | Same trigger schedule, path/publication policy, and backend split. |
| `Runtime/Diagnostics/DiagnosticsKeyboardShortcuts.h:47` | Consume a typed keyboard edge/action plus the one overlay/diagnostic owner being changed; return a diagnostics command/result. |
| `Runtime/Diagnostics/RuntimeDiagnostics.h:49` | Same detached per-domain diagnostic facts instead of ten live sources. |
| `Runtime/Editor/EditorTools.h:375,420` | Same pointer/hit/gesture and validated placement command values. |
| `Runtime/Input/InputRouter.h:328,333,338,345,350,354,358,372` | Same typed transitions, camera-mode commands, focus-loss phase, and pointer routing results. |
| `Runtime/Planning/ReplayCauseInspection.h:413` | Same pointer/layout event plus owner-retained solver-detail state. |
| `Runtime/Prediction/ReplayPrediction.h:243,463,1024,1030,1039` | Give marker matching a typed scan key and publication begin a validated identity/capacity value; apply the same source/simulation/worker phase split. |
| `Runtime/Prediction/ReplayPredictionArchive.h:56` | Same schema validation, retained-state transaction, and publication result split. |
| `Runtime/Prediction/ReplayPredictionPublication.MarkerScan.inl:165` | Make marker scan state own its criteria/cursor; the advance call receives only the current frame/collection and bounded output. |
| `Runtime/Prediction/ReplayPredictionPublicationOperations.h:191,195,205` | Same marker scan, affected-trail builder, and overlay projection separation. |
| `Runtime/Render/RuntimeRenderer.h:295` | Same explicit scene-target begin and render-graph execution phases. |
| `Runtime/Render/RuntimeRenderPasses.h:713,741,871,873,909,944,998,1003` | Give `ReflectionPass` retained pass dependencies only; apply pass-specific input/resource values to shadow, debug, volumetric, tonemap, and UI operations. |
| `Runtime/Render/UiDrawSubmission.h:71,75,84` | Same typed UI commands and preview-resource resolution split. |
| `Runtime/Render/UIProfilerOverlayPresenter.h:38` | Same pre-projected profiler values and typed quads. |
| `Runtime/Replay/ReplayAuthoring.h:200` | Same pointer event, retained velocity-edit state, and emitted velocity command. |
| `Runtime/Replay/ReplayEventCommand.h:132,147` | Build commands from typed event identity/payload and editor transform values rather than primitive runs. |
| `Runtime/Replay/ReplayOverlayLayout.h:217` | Same returned scrubber availability/layout value. |
| `Runtime/Replay/ReplayPresentation.h:212` | Same bounded past-trajectory update value. |
| `Runtime/Replay/ReplayRecorder.h:442,516` | Same world-presentation versus solver-evidence capture split with explicit reserve ownership. |
| `Runtime/Replay/ReplayTimeline.h:274` | Same validated timeline frame identity plus narrow sample inputs. |
| `Runtime/Scene/AttachedCameraController.h:101` | Same follow-target pose and constraint-policy result. |
| `Runtime/Scene/SceneGeneratedSetup.h:84` | Same population specification and setup result. |
| `Runtime/Scene/SceneLoadTransaction.h:194,283,300,306,314` | Same phase-tracked load transaction, typed mode/preservation policy, and preparation results. |
| `Runtime/Tools/EditorTracer.h:87,96,100,104,175,177` | Same typed line/ribbon/shape pose and gizmo interaction values. |
| `Runtime/Tools/RuntimeTools.h:324` | Same resolved pickup ray/camera value and emitted interaction command. |
| `Runtime/UI/GameUI/UI.h:510` | Same screen/input/mode/camera value split for `UpdateInput`; independently repair the 115-field frame record below. |
| `Runtime/UI/GameUI/UIFrameComposition.h:94,250,262,267` | Same canonical interaction state and palette entry/style/layout values. |
| `Runtime/UI/GameUI/UITabCinematic.h:51,58` | Same tab layout/pointer event split. |
| `Runtime/UI/GameUI/UITabControls.h:55,64` | Same controls layout/event and solver summary values. |
| `Runtime/UI/GameUI/UITabEditor.h:54` | Same editor-tab layout/pointer values. |
| `Runtime/UI/GameUI/UITabMemory.h:106,108` | Same memory projection, policy interaction, and drawing split. |
| `Runtime/UI/GameUI/UITabOptions.h:51,58` | Same two-column layout and pointer event. |
| `Runtime/UI/GameUI/UITabPhysics.h:136,142` | Same shared tab-layout concepts and Physics-specific detached view. |
| `Runtime/UI/GameUI/UITabProfiler.h:211,218,224` | Same profiler layout, pointer-wheel event, and worker range. |
| `Runtime/UI/GameUI/UITabScene.h:96,99,109,111,113,116,124` | Same combo model/layout/event reuse and separate scene/recording/time-scale controls. |
| `Runtime/UI/GameUI/UITabSky.h:55,61` | Same sky-tab layout/pointer and detached cinematic style. |
| `Runtime/UI/GameUI/UIWindowInteractionOwner.h:167` | Same typed UI mode state; independently remove the 53-reference `WidgetView` below. |
| `Runtime/UI/OperatorUiProjection.h:466` | Same named replay-memory policy projection value. |
| `Scene/AuthoredSceneParserSchema.h:1275` | Same validated asset-part identity/transform record. |
| `UI/UIComboBox.h:43,45` | Same combo options/selection/pointer values and overload consolidation. |
| `UI/UIDraw.h:57-60` | Same typed bounds/points/color/radius low-level draw values. |
| `UI/UIDrawList.h:103-105,112` | Same retained typed geometry/color commands and preview fallback style. |
| `UI/UIDrawWidgets.h:84,107,127,130,151,155,158,160` | Same slider/combo/label/footer presentation values. |
| `World/Terrain.h:126,139,150` | Same validated height-map creation and terrain render-view split. |
| `World/WorldEnvironment.h:187` | Same water camera/reflection/resources/style split. |

## Aggregate and Context-family Backlog

The same revision contains 296 first-party records with at least four fields
and a context/input/view/state-style name. Naming alone does not make those
records defective. The structural triage below lists every borrow-heavy record
(three or more pointers, references, or spans) and every major sibling-slice
family that needs a collective review. “Keep” still requires the named check;
it is not an exception.

| File and approximate lines | Type/family | Initial decision or decomposition |
|---|---|---|
| `Core/Allocation/RuntimeReserveAllocator.h:122-138` | `RuntimeReserveGrowthEventView` | Likely keep as an immutable event record; replace raw string pointers only if their lifetime is not guaranteed by the event owner. |
| `Physics/PhysicsApi.h:336-342` | `PhysicsAuthoredBodyRefreshView` | Replace pointer-plus-count parallel arrays with spans and validate equal body count before refresh. |
| `Physics/PhysicsBodyStore.h:195-241` | `PhysicsBodyHotFieldsConstView`, `PhysicsBodyHotFieldsView` | Keep as the canonical aligned SoA views, but ensure construction validates every span length once and consumers use row behavior rather than copying spans. |
| `Physics/PhysicsDiagnosticsSink.h:67-75` | `PhysicsDiagnosticsFrameInput` | Split diagnostic snapshot projection from CSV publication; pass the writer only the projected data it serializes. |
| `Physics/PhysicsDiagnosticsView.h:192-217` | `PhysicsDiagnosticsView` | Split contact, sleep, broadphase/pipeline, and memory/stat views by actual consumers; no consumer should receive all 23 borrows unless it renders the complete diagnostics product. |
| `Physics/Stages/ExternalForceStage.h:68-82` | `ExternalForceFrameInput` | Keep force fields plus aligned exposure/cooldown rows together with validation; move timestep and parallel-execution policy outside that row view. |
| `Physics/Stages/PhysicsBroadphaseStage.h:63-75` | `PhysicsBroadphaseStepInput` | Definite repair: replace with retained config, checked activity view, validated sweep/contact value, explicit joints, and direct trace recorder. |
| `Physics/Stages/PhysicsSleepController.h:70-101` | `PhysicsNarrowphaseWakeAccess` | Keep only if its methods need the full synchronous capability; remove cached records/hot view/model count that can be derived safely from the store, or split wake-with-forces from sleep queries. |
| `Rendering/PrimitiveBatchRenderer.h:78-120` | `PrimitiveBatchRendererState` | Likely keep as owned renderer state, not a parameter bag; review whether unrelated sphere/box/pine resources should be child owners. |
| `Rendering/RenderGraph.h:157-164` | `RenderGraphPassContext` | Likely keep as a checked graph/pass execution cursor; ensure passes cannot retain it and invalid pass index/graph combinations are rejected. |
| `Rendering/WorldRenderExtension.h:70-81` | `WorldRenderExtensionFrameView` | Split camera values from live DX12 services; extension callbacks should receive a narrow extension draw interface or direct resources actually used. |
| `Runtime/DevelopmentTools/ImGuiEditorCausalityProjection.h:61-75` | `ImGuiEditorCausalityContext` | Replace pointer collection/count fields with a selected-cause value and bounded related-row span; keep projection statistics separate. |
| `Runtime/Direction/LookLabController.h:77-87` | `LookLabSaveRequest` | Likely keep as one synchronous save request after validating/copied strings and timestamp/offset rules; it already names a concrete operation. |
| `Runtime/Interaction/OperatorEditorExchange.h:81-385` | `OperatorEditorInspectorView` and all `OperatorEditor*View` siblings | Review as one surface. Keep detached presentation groups only where different consumers use different subsets; merge duplicates and prevent any operation from receiving the complete slice set. |
| `Runtime/Planning/ReplayCauseInspection.h:166-197` | `ReplayCauseInspectionView` | Split transport/selection, solver-detail availability, and display metrics if consumers use them independently. |
| `Runtime/Planning/ReplayOverlayPackets.h:74-101` | `ReplayOverlayStateView` | Treat as top-level immutable overlay composition only; individual renderers consume their subview, never the full aggregate. |
| `Runtime/Prediction/ReplayPredictionTopologyPublication.cpp:445-452` | `ReplayPredictionFutureContext` | Definite repair: replace three mutable owner pointers with a topology builder method/owner and explicit collection input. |
| `Runtime/Prediction/ReplayPredictionView.h:200-242` | `ReplayPredictionPresentationView` | Split trajectory, topology, marker, baseline-pose, and drag-preview views by renderer/planner consumer; retain one top-level composition only at App/UI projection. |
| `Runtime/Render/CollisionVisualizer.h:69-78` | `CollisionVisualizerFrameView` | Keep only as an aligned visualizer input with model-count validation; prefer a Physics debug snapshot over live store borrows if already available. |
| `Runtime/Render/PhysicsDebugVisualizer.h:55-65` | `PhysicsDebugFrameView` | Split contact, sleep, and pipeline overlays when independently enabled; each overlay receives only its rows. |
| `Runtime/Render/RuntimeRenderer.h:76-88` | `FrameEntryContext` | Definite repair: split frame begin, world draw, replay/tool overlay, and frame end; do not pass the same ten-part bag through all phases. |
| `Runtime/Render/RuntimeRenderFrameValues.h:126-147` | `RuntimeRenderModelFrameView` | Split model presentation, Physics debug rows, and world-extension data. Replace live engine-owner references with detached render values where feasible. |
| `Runtime/Render/RuntimeRenderHost.h:92-99` | `RenderWorldView` | Definite constructor/service bag candidate: transfer stable assets/window/config/environment ownership explicitly to `RuntimeRenderHost` or direct child owners. |
| `Runtime/Render/RuntimeRenderPasses.h:234-246` | `RenderResourceContext` | Split pass-specific resources or expose behavior on the owning renderer; do not give every pass all assets/resource/texture/geometry services. |
| `Runtime/Replay/ReplayCoordination.h:82-283` | workspace, transport, input, `ReplayStartupRequest`, and reset records | Review collectively. Replace flag/path combinations with typed startup/transport variants; keep input/layout values separate from scene reset mutation. |
| `Runtime/Replay/ReplayRuntimePackets.h:40-51` | `ReplayRenderFrameView` | Validate optional sample combinations and split recorded, solver, prediction, focus-mask, and contact presentation by actual render consumers. |
| `Runtime/Scene/SceneController.h:97-105` | `SceneDefaultsSaveView` | Likely keep as one synchronous save snapshot if the writer consumes all fields and a focused test pins lifetime/serialization. |
| `Runtime/Tools/RuntimeTools.h:200-216` | `LauncherReproSnapshotContext` | Definite repair: split scene snapshot, launch options, and captured runtime settings; the exporter consumes immutable values, not live `SceneWorld`. |
| `Runtime/UI/GameUI/UI.h:139-441` | `UIMemoryTabFrameView`, `UISceneTabFrameView`, other tab views, and `InGameUIFrameData` | Definite family repair: remove the 115-field root record. Project per-tab detached views and provide only the active tab plus shared window/input values. |
| `Runtime/UI/GameUI/UIWindowInteractionOwner.h:61-117` | `WidgetView` (53 references) | Definite repair: make the interaction owner own cohesive widget groups or use narrow handlers; never reconstruct the entire UI object through references. |
| `Runtime/UI/OperatorUiProjection.h:48-208` | `OperatorUiSceneFacts`, `OperatorUiInspectorFacts` | Consolidate duplicated fields with the corresponding detached UI views; split selection identity, material, transform/physics, and navigation projections by consumer. |
| `Scene/SceneSnapshotWriter.h:53-70` | `SceneWorldSaveState` | Likely keep as one serialization snapshot if construction validates joint pointer/count and the writer consumes the complete state synchronously. |

Large non-borrowing planning and UI presentation records remain in the 296-row
SC0 input even when not repeated in this table. SC0 must add a row when a
consumer receives several of them together, when two duplicate fields, or when
one is used only as forwarding storage.

## SC0 Inventory Evidence

The refreshed compiler-backed census covers 736 first-party source and header
files under 6,435 distinct project/configuration contexts. It reports 698
deduplicated review candidates: 517 operations with at least eight parameters
and 181 context-family records with at least four fields. The run completed
with zero parse or infrastructure errors. The deterministic output is frozen at
`Artifacts/engine-signature-and-context-cohesion/sc0-source-design-inventory.txt`;
the local execution logs remain under
`TestOutput/validation/signature-context/`.

The current-tree operation and aggregate tables above provide the qualitative
package pass and initial decisions for Core, Maths, Physics, Gameplay, Assets,
Rendering, Scene, World, UI, every Runtime sub-package, and Tests. Assets has no
structural match in the current compiler census; its public surface remains in
the qualitative review rather than being omitted. The candidate worksheet will
gain final caller/lifetime/evidence decisions in each owning implementation
phase, and SC6 will reconcile every frozen SC0 row against the final tree.

Focused evidence mapped at activation is: source-design plus Physics
determinism/allocation gates for SC1-SC2; source-design plus command/resource
and shader-contract gates for SC3; source-design plus Replay fidelity,
unchanged-manifest, allocation, and dependency gates for SC4-SC5; and the full
validation map plus independent review for SC6. SC0 itself changes advisory
tooling and documentation only, so no product build is required.

## SC1 Broadphase Decision And Evidence

`PhysicsBroadphaseStepInput` is deleted. Its settings, joints, four activity
rows, diagnostics owner, and three scalars no longer travel as one positional
record. `PhysicsBroadphaseStage::Run` now reads as the two stores being queried,
explicit joint exclusions, a checked body-activity view, a validated
sweep/contact envelope, and the exact pipeline-trace capability consumed.

`BroadphaseBodyActivityView` enforces the dense body domain, optional row
alignment, and ascending unique awake indices while preserving conservative
fallback for intentionally absent motion/expansion rows.
`BroadphaseSweepContactEnvelope` enforces finite non-negative timestep, skin,
and epsilon. `BroadphasePairFilter` owns the synchronous store/activity/envelope
relationship and provides the sleeping, geometry-only, and complete admission
operations used by both `SpatialGrid` and fast-sweep augmentation. It retains
nothing beyond the call. The stage retains its validated cell-size cap through
`ApplyRuntimeSettings`; per-step callers no longer resend broadphase settings.

Production and tests now construct the named values explicitly. Focused tests
cover activity misalignment, duplicate indices, motion/expansion behavior,
invalid scalar domains, static/swept boundaries, sleep pruning, angular reach,
and all SpatialGrid paths. Profile builds warning-clean; 39 mapped cases pass
with 9,273 assertions; source-design passes 11 files/70 contexts; dependency,
format, and build-configuration checks pass. The clean-process Physics worker
matrix remains byte-exact across 0/repeat/1/4 workers at 44,401 lines and
SHA-256 `03088b30b8826f88a6193e511b7f4205aff9324d06ad08456610aac0e13a3f6b`.
No baseline or golden changed.

## SC2 Lower-package Decisions And Evidence

The SC0 census contained 100 Maths, Physics, Gameplay, World, and Assets rows:
85 wide operations and 15 context-family records. Assets had no structural row
and its qualitative public-surface review found no positional or unrelated-
borrow repair. The package review made these owner-level decisions:

- `RotationMatrix` now constructs from three explicit basis rows instead of
  nine adjacent floats. Quaternion and arbitrary-axis callers retain the exact
  row-major arithmetic and make the matrix meaning visible.
- Tornado visual vertices now serialize through one `FxVertex` value with named
  color/UV/style fields. The oversized render operation is split into time
  selection, active-vortex construction, ribbon emission, dust emission, and
  draw submission while preserving vertex and command order.
- Authored Physics refresh uses three aligned spans and rejects mismatched row
  counts. Both mutable and const hot-field views expose and check their common
  row domain before leaving `PhysicsBodyStore`.
- Terrain closest-probe output references became an optional gap result;
  external-force strongest-field output references became one typed sample;
  and the ragdoll impulse helper became a two-body operation whose body value
  applies the exact retained update/clamp sequence.
- `PhysicsWorld` retains the validated runtime settings it applies, including
  replay-prediction topology clones, instead of receiving the same settings on
  every fixed step. Active external-force rows now validate their body count,
  timer alignment, and finite non-negative timestep at the world boundary.
- Height-map construction receives one factory-only validated geometry value.
  Callers can no longer pair dimensions with stale derived pixel/post/quad
  counts.

The remaining lower-package wide rows are cohesive private SAT/clip, solver,
force, sleep, terrain-contact, and grid kernels. Their operands are independently
named algorithm inputs, several are intentionally direct to preserve arithmetic
order, and no caller repeatedly rebuilds an unrelated owner surface. Wrapping
those operands would add nominal bags without enforcing a new rule. Physics
diagnostic and terrain snapshots remain complete synchronous values consumed by
their owning emit/query operation. World render-facing rows are reviewed with
their Rendering consumers in SC3 rather than introducing a downward Runtime or
Rendering concept.

Profile builds warning-clean. The 33 focused Maths/Physics/Gameplay/World cases
pass 925 assertions. Compiler-backed source-design passes 24 files under 192
distinct consumer contexts with no findings; dependency, formatting, and
build-configuration checks pass. The clean-process Physics worker matrix remains
byte-exact at 44,401 lines and SHA-256
`03088b30b8826f88a6193e511b7f4205aff9324d06ad08456610aac0e13a3f6b`.
No baseline or golden changed.

## SC3 Rendering, UI, And DX12 Decisions And Evidence

The SC0 census contained 108 rows in this slice: 68 Rendering/DX12 rows and 40
UI-library rows. The package review made these owner-level decisions:

- Mesh creation now receives a validated packed vertex view and an in-bounds
  upload slice that proves the CPU bytes, GPU address, byte count, and backing
  resource belong together before any copy is recorded.
- Instance rendering receives one synchronous camera/light view and a model
  selection value with exactly three valid modes: all, marked, or unmarked.
  Shadow submission reads typed batch views instead of unpacking the complete
  batch record into local aliases.
- Retained UI commands now accept bounds, points, triangles, and colors as named
  values. Combo drawing receives one span-backed presentation view, so option
  storage cannot disagree with a separate count and selection/disabled state
  remains aligned with that storage.
- Because the changed Runtime consumers are compiler-gated as complete
  translation units, the oversized GameUI and replay scrubber composers were
  repaired in this phase rather than left with new SC4 debt. GameUI now has
  separate minimized, overlay, active-tab, render-authoring, target-preview,
  and footer phases. Replay scrubber drawing now has separate surface, label,
  header/edit, track, prediction-control, and status phases.

Externally fixed D3D12 callbacks and ABI entry points remain direct. Private
pipeline-state and command-recording kernels also remain direct where their
operands are the actual algorithm and a wrapper would enforce no invariant.
`UIDrawContext` remains the immediate-mode primitive authoring surface: its
scalar geometry/color overloads immediately create typed retained
`UIDrawList` commands and retain no ambiguous state. Render-graph and pipeline
composition records remain complete synchronous values whose consumers already
use behavioral subviews. These are concrete keep decisions, not exception rows.

The Profile solution builds with zero warnings. The complete test executable
passes 912 cases and 2,693,378 assertions; the renderer-free UI boundary probe
builds and passes its deterministic fingerprints. Compiler-backed source-design
passes 45 changed files under 347 distinct build contexts with no findings;
dependency, formatting, and build-configuration checks pass. Baked shader
freshness passes for all 44 stages. The broader advisory shader inventory still
reports its inherited 22 manifest/resource-contract errors; no shader or shader
manifest changed in SC3.

Two live DX12 runs produce every expected capture and report zero InfoQueue
errors. The committed screenshot oracle still rejects `water_ball_test` and
`solver_smoke`, while `space_three_body` is pixel-exact. A detached build of the
pre-SC3 commit reproduces those same oracle deltas. Current and pre-SC3 water
and space captures are pixel-identical; solver differs in only 828 color
channels with maximum delta 36, compared with roughly 397,000 channels over the
baseline threshold. The screenshot mismatch is inherited and no baseline or
golden was refreshed. Replay visual fidelity remains mapped to the SC6 terminal
closure pass, where the plan concentrates heavy subsystem gates.

## SC4 Runtime Capture And Presentation Slice Evidence

The first SC4 slice repairs three related Runtime boundaries without retaining a
broad context surface:

- Capture receives a typed screenshot frame value and a typed auto-cycle input.
  The schedule value owns the due check, including the non-zero ball-count
  invariant, while Capture returns an explicit state update that App applies.
- Replay scrubber availability receives one source-selection value whose
  behavior identifies the current track instead of passing six independent
  availability booleans through Planning, App, and tests.
- UI interaction caching receives one interaction-signature value. Its methods
  own the window-local pointer rules and its open-control flags preserve the
  previous hash order exactly.

The combined Profile solution build completed with zero warnings. The complete
test executable passed 914 cases and 2,692,750 assertions. Compiler-backed
source-design passed the 15 changed source-bearing files under 96 consumer
contexts with no findings. Formatting, dependency/project ownership,
build-configuration consistency, and whitespace checks pass. The touched
coordinator functions expose separate pre-existing size and unpacking findings;
their structural repair remains in the next grouped SC4/SC5 slice rather than
being hidden behind another parameter record. No baseline or golden changed.

The second SC4 slice repairs the exposed below-App coordinator structure.
`UIWindowInteractionOwner` now sequences separate histogram, minimized-camera,
editor-status, editor-palette, window-layout, wheel, chrome, open-control,
diagnostic-tab, presentation-tab, render-tab, footer, slider, drag/resize, and
release operations. One derived window layout owns the animated hit regions,
and one synchronous option view binds labels to the selected rows from the same
navigation generation. Editor gizmo routing separately applies the live drag,
records scale release, and records pose/group release. Operator diagnostics
separately project frame metrics, draw attribution, and bounded profiler rows.
The previous action order and capture transitions remain intact without a new
multi-owner context.

The Profile solution builds with zero warnings. The complete normal test run
passes 913 cases and 2,691,927 assertions with its one expected skip. Compiler-
backed source-design passes the five changed source-bearing files under 39
consumer contexts. The renderer-free UI boundary, formatting,
dependency/project ownership, build-configuration consistency, and whitespace
checks pass. No baseline or golden changed.

The third grouped slice repairs the first App orchestration boundary exposed by
that below-App work. `ApplyInputCommandsPhase` now sequences replay transport,
forecast ownership, device/mode acceptance, editor-mode transitions, editor
scene edits, runtime presentation, replay/physics tuning, generated-scene
transactions, and world/cinematic completion as explicit phases. The command
transaction and acceptance ledger retain their original ordering; helper
operations borrow App owners only for the synchronous phase and retain no
context record. The UI input seam now receives detached screen/timing,
editor-mode, and camera-availability values instead of ten adjacent primitive
arguments. The camera value omits the formerly unused selected-mode scalar.

The combined Profile solution build completes warning-clean. The normal test
suite passes 913 cases and 2,694,677 assertions with one expected skip.
Compiler-backed source-design passes all seven changed source-bearing files
under 53 consumer contexts with no findings. No baseline or golden changed.

The fourth grouped slice repairs the two remaining oversized App frame
coordinators exposed by the compiler gate. `RunInputPhase` now sequences
capture/default draining, pre-UI action families, operator input, replay
restore, recording diagnostics, camera control, and deferred owner requests as
named operations. `RenderOperatorUiPhase` now samples one detached projection
shared by GameUI and ImGui, then separately projects hierarchy, inspector,
secondary diagnostics, GameUI data, text/GPU submission, and the development
surface. These remain ordered composition-root phases; no helper retains a
multi-owner context or moves frame orchestration below App.

The Profile solution builds warning-clean. The normal test suite passes 913
cases and 2,692,437 assertions with one expected skip. Compiler-backed
source-design passes the three changed source-bearing files under 16 consumer
contexts with no findings. Formatting, dependency/project ownership,
build-configuration consistency, and whitespace checks pass. No baseline or
golden changed.

## Phases

- [x] **SC0 — Build the whole-engine inventory and lock behavior evidence.** Add
  or run the advisory compiler-backed inventory across all first-party
  translation units and distinct header-consumer contexts. Complete the
  qualitative package pass, populate the candidate worksheet, and record the
  focused validation mapped to every accepted repair. Prove the inventory has
  no parse or project-coverage gaps before editing production source.
- [x] **SC1 — Prove the repair model on Physics broadphase.** Replace the current
  broadphase bag/wide-call tradeoff with concrete configuration ownership,
  aligned activity behavior, named validated sweep values, and a narrow trace
  capability. Preserve candidate order, trace order, allocation behavior, and
  exact Physics output. Update the worksheet with before/after production and
  test calls.
- [x] **SC2 — Repair Maths, Physics, Gameplay, World, and Assets candidates.** Work
  in independently reviewable owner-local slices. Prioritize hot loops, solver
  stages, and APIs with same-type scalar runs. For every slice, preserve
  floating-point evaluation order and run the owning focused tests. Do not move
  Runtime concepts downward to shorten an upper-layer call.
- [x] **SC3 — Repair Rendering, UI library, and DX12 candidates.** Separate
  immutable resource descriptions, per-draw values, and lifecycle operations.
  Preserve C/DX12 callback and ABI signatures when externally fixed, and record
  that constraint rather than wrapping them. Prove command order, descriptor
  ownership, resource lifetime, and shader-contract behavior for changed calls.
- [ ] **SC4 — Repair Runtime package candidates below App.** Review Input,
  Simulation, Scene, Replay, Prediction, Planning, Tools, Editor, Render,
  Diagnostics, DevelopmentTools, Capture, Direction, Automation, and Runtime UI
  in dependency order. Keep Replay/Prediction/Planning placement intact. Reject
  context slices that collectively expose several concrete owners to one
  operation.
- [ ] **SC5 — Repair Runtime/App orchestration and test-call readability.** Treat
  `Run` and its sibling translation units as one surface. Split independently
  meaningful phases or move behavior to existing concrete owners; do not hide
  parameters through `Run` member reach. Update tests to show the real API with
  named values. Test fixtures may supply legitimate defaults, but must not be
  the only way an unreadable production call can be understood.
- [ ] **SC6 — Rescan, remove obsolete bags, and close with independent review.**
  Run the whole-engine inventory again and account for every original and newly
  exposed candidate. Delete superseded behavior-free records and forwarding
  helpers. Obtain independent ownership/readability review of representative
  production and test calls, then run the complete validation map. Any finding
  that the same participant surface was merely renamed, sliced, or moved into
  member reach reopens the owning phase.

## Slice Validation

During implementation, keep each repair small and run focused evidence before
moving to the next owner:

- compiler-backed source-design scan for every changed source/header and its
  distinct first-party consumer contexts;
- pinned formatting and dependency/project ownership checks;
- focused unit tests that exercise the value invariant, invalid input, call
  sequence, and production behavior;
- Profile warning-clean build for the touched project;
- Physics byte-exact 0/repeat/1/4-worker comparison for any Physics execution,
  state, or arithmetic change;
- replay visual/fidelity and unchanged-manifest evidence for Replay-facing
  changes;
- focused graphics/resource validation for Rendering and DX12 changes;
- allocation policy validation when a view, result, or phase changes retained
  or temporary storage.

At terminal closure, run `validate_fast`, the complete test suite, portable
Linux diagnostics, and every mapped subsystem gate. No baseline update command
is part of this plan.

## Acceptance Criteria

- [ ] Every first-party package has complete compiler inventory coverage and a
  recorded qualitative review result.
- [ ] Every candidate row has a final decision and evidence; no row disappears
  because a symbol was renamed or moved.
- [ ] Every repaired aggregate names and enforces a concrete rule exercised by
  a focused test.
- [ ] Representative production and test calls make units, roles, and optional
  behavior clear without consulting a constructor field order.
- [ ] No operation receives a collection of slices that reconstructs the old
  broad context surface.
- [ ] No operation was shortened by moving unrelated borrows into members,
  globals, callbacks, lambdas, friends, service locators, or forwarding owners.
- [ ] Stable configuration is retained only by the concrete subsystem owner and
  updates through an explicit configuration boundary.
- [ ] Externally fixed callbacks/ABIs remain direct and are recorded as such;
  no wrapper exists solely to satisfy a parameter count.
- [ ] Physics and Replay behavior remains byte-exact where required, renderer
  command/lifetime behavior remains unchanged, and no golden was refreshed.
- [ ] The advisory final scan has complete parse coverage and all remaining
  candidates have a concrete keep decision rather than an exception entry.
- [ ] Independent review finds no renamed parameter bag, nominal capability
  slice, composition-root reach-back, or phase split that relies on caller
  discipline.

## Reactivation Condition

Move this file from `WNF/` to `TODO/` only when the owner explicitly activates
the engine signature/context cohesion campaign. At activation, rerun SC0 against
the then-current branch and replace the motivating broadphase hypothesis with
the exact live API if it has changed. Do not reuse candidate counts or decisions
from an older revision.
