# Invariant Enforcement And Assertion Hardening

Date: 2026-08-20
Status: Active; 4/8 phases complete
Impact area: repository-wide invariant enforcement, assertion policy, failure
lanes, tests, comment taxonomy, and substantial diagnostic tools
Owner: subsystem owners named by each affected path
Priority: Active final queue item; `REST_STABILITY` is complete
Commit name: `INVARIANT_HARDENING`

## Goal

Close every high-, medium-, and low-severity finding from the 2026-08-19
repository invariant audit. The result must make each audited invariant honest
about how it is enforced in Debug, Profile, and Release builds. It must add
missing runtime enforcement where violating the contract could otherwise cause
undefined behavior, memory corruption, or silently invalid state, while
avoiding ceremonial assertions that merely duplicate a checked failure path.

This is not a requirement to pair every `Invariant:` comment with an `assert`.
An invariant may instead be proved by construction, the type system, a bounded
container, a checked Lane R return, an owned `SB_FATAL`, or a focused test. The
plan is complete only when every finding has an explicit enforcement lane and
negative proof that fails when the contract is broken.

## Registration And Ordering

- The master ledger activates this plan after `REST_STABILITY` closure; IH0 is
  the binding next phase.
- The eight phases are portfolio tasks IH0 through IH7. Their progress is
  reported under commit token `INVARIANT_HARDENING`.
- Orbital forecast and rest stability are complete. Rebase the audit inventory
  on their final source before IH0 is checked.

## Enforcement Policy

Every reviewed invariant and assertion site receives exactly one primary
classification:

| Lane | Use | Required behavior |
|---|---|---|
| Construction/type proof | Invalid state cannot be represented or reached | Record the constructor, type, fixed-capacity owner, or call order that proves it and add a focused test when the proof is non-obvious. |
| Debug tripwire | Internal programmer error whose Release path is already defined and safe | Keep or add `assert`, prove the non-Debug path cannot perform an unchecked access or continue with corrupt state, and exercise the safe path. |
| Lane R | Recoverable authored, file, user, device, or external-input failure | Validate before mutation or allocation and return the repository-owned diagnostic/result value. |
| Owned fatal | Unrecoverable internal contract breach | Check in all configurations with the owning fatal mechanism and prove it in a subprocess/death test so the main test process survives. |
| Lane P | Diagnostic or test-only probe | Keep production behavior unchanged; label the probe and never present it as runtime enforcement. |

Additional rules:

- Profile and Release define `NDEBUG`; a plain `assert` is never the sole guard
  before pointer dereference, array indexing, division, state mutation, or
  capacity overflow.
- Do not add `assert(condition)` immediately beside an equivalent `SB_FATAL`
  branch. The always-on owned failure is the enforcement; duplicate checks add
  noise and can drift.
- Validate external values before construction. A constructor that divides by
  or sizes storage from an unchecked value is already too late.
- Preserve fixed-capacity and no-steady-runtime-growth policies. An assertion
  must not disguise an allocation path or grant a new replay growth privilege.
- Assertion and invariant counts are measurements, never budgets or ratchets.
  Do not add a frozen count, percentage, or spelling gate.
- Apply `Agentic/Reference/comment-style-guide.md` to every touched source file.
  A comment must name the owner and proof, not promise safety that only exists
  in Debug.

## Audit Baseline

The completed read-only audit recorded the following current measurements:

- 835 tracked source-bearing files were inventoried.
- 819 learning headers contained an `Invariants` section, with 2,686 header
  bullets; 792 nearby `Invariant:` blocks existed, 696 in production source.
- Production source contained 132 plain `assert` calls, 66 `static_assert`
  calls, and 468 `SB_FATAL` calls.
- 63 plain-assert sites had no nearby fatal, checked return, abort, or explicit
  conditional fallback and therefore require owner triage. This number is a
  worklist measurement, not a permitted maximum.
- The authority-free aggregate and glossary inventories passed. Transient
  related-path failures belonged to the active, untracked orbital-forecast
  work and are not defects assigned to this plan.

IH0 regenerated these inventories after both earlier plans closed. The exact
path-and-line disposition tables below are the closure source of truth; the
measurements are current evidence, never a policy threshold.

### IH0 Rebased Inventory — 2026-08-20

- `git ls-files` reports 847 tracked source-bearing files: 341 `.cpp`, 350
  `.h`, one `.hpp`, one `.inl`, 23 `.hlsl`, 64 `.py`, 63 `.bat`, and four
  `.ps1`. The exact ordered inventory is preserved in the IH0 artifact folder.
- 777 files contain a learning-header `Invariants:` section with 2,650 bullets.
  The 70 without that section comprise 54 trivial batch wrappers (header-exempt
  under the Comment Quality Gate), the six named IH6 tools, two githooks, one
  orchestrator helper, four generated/third-party files, and three other
  already-recorded trivial/generated helpers.
- There are 806 local `Invariant:` blocks, 702 in production source.
- Production source contains 132 actual plain `assert` calls, 66
  `static_assert` calls, and 469 `SB_FATAL` calls. Comment-aware parsing excludes
  `Common.h`'s textual `// assert()` include note.
- The prior 63-row heuristic rebases to 62 assert-only candidates. The heuristic
  selects a plain assertion with no `SB_FATAL`, abort, or checked return in its
  six-line neighborhood; the table below records false positives explicitly.
- The allocation/capacity taxonomy worklist contains 45 current `Invariant:`
  blocks: 40 line-local keyword matches plus five semantically confirmed
  continuation sites. This preserves the prior audit's bounded worklist without
  treating substring matches such as “preserve” as allocation policy.
- Ignored artifacts live under
  `TestOutput/validation/INVARIANT_HARDENING_IH0/`: `inventory.json` SHA-256
  `ED86CF17EE8B9659DD6CEC69FC11F28DC178995C32BBD53357610FE7C36F57EF`,
  `assertion-sites.tsv` `BF43F144B397798FE4FF0D68B4367DACC4E703023F693B3BCA7DB47C42FEC5C5`,
  `policy-invariant-sites.tsv`
  `54DA72ACE6AEF3CB682070AA35604C02140CDC232658E7BCE84E92347210EF5F`, and
  `tracked-source-files.txt`
  `BFD1474E850E7607DF1513DEFA607422C5E6034BA048EB5B0EFF84FDC0517C8C`.

## Findings To Close

### High Severity

1. `SkullbonezSource/Physics/ColliderStore.cpp` uses assert-only shape-index
   bounds in `RebindShapeReferences` before unchecked vector indexing. Replace
   the Release-unsafe path with owner-appropriate always-on enforcement and
   cover every sphere, box, and hull branch plus corrupt-index death cases.
2. `SkullbonezSource/Rendering/PrimitiveBatchRenderer.cpp` relies on retained
   renderer/active-scope assertions in `PrimitiveBatchScope` and related draw
   paths. Establish whether scope misuse is an unrecoverable renderer contract
   or a safe no-op result, enforce it outside Debug, and test visible, shadow,
   inactive, moved, and nested/mismatched-resource cases.
3. `SkullbonezSource/Runtime/Render/RenderResourceLifecycle.cpp` asserts preview
   snapshot capacity immediately before indexing. Make capacity enforcement
   always-on or make the bounded append structurally incapable of overflow;
   prove exact-capacity and one-over-capacity behavior.
4. `SkullbonezSource/Runtime/App/Run.cpp` and its sibling runtime owners expose
   assert-only renderer, terrain, sky, world-resource, input-bridge, and
   interaction-state dereferences. Triage the complete `Run*` logical surface,
   move checks to the concrete lifecycle owner, and give Release either a safe
   Lane R result or an owned fatal before dereference.
5. `SkullbonezSource/World/Terrain.cpp` constructs terrain before validating
   `mapSize` and `stepSize`. The constructor divides by `stepSize`, and floor-
   sized post storage disagrees with the ceiling-shaped translation loops for
   non-divisible dimensions. Validate positive, divisible dimensions before
   construction through Lane R, use one checked post-count computation, and
   prove zero, negative, undersized, non-divisible, exact-minimum, and normal
   height maps without out-of-bounds writes.

### Medium Severity

1. Re-triage all 63 assert-only candidates after active work lands. Each row
   must record path, symbol, claimed invariant, Debug behavior, Profile/Release
   behavior, primary enforcement lane, owner, focused proof, and disposition.
   No row may close as “assert added” without defining non-Debug behavior.
2. Review the 45 heuristic `Invariant:` candidates that describe allocation,
   reserve, growth, or capacity policy. Split mechanical safety from runtime
   allocation policy where both are present. Confirmed starting points include
   `SkullbonezSource/Assets/TextureCollection.cpp`,
   `SkullbonezSource/Gameplay/TornadoField.h`,
   `SkullbonezSource/Runtime/Prediction/ReplayPredictionPresentation.cpp`, and
   `SkullbonezSource/Runtime/Replay/ReplayAuthoringPackets.h`.
3. For every touched fixed-capacity owner, test exact capacity and overflow.
   For every lifecycle pointer, test access before initialization, during the
   valid phase, and after teardown. For every identity/index contract, test a
   stale or corrupt value through a bounded death-test harness.

### Low Severity

1. Reclassify the worker-dispatch threshold comment near
   `SkullbonezSource/Runtime/Prediction/ReplayPrediction.cpp:187` as `Why:`;
   it explains a performance choice rather than a correctness invariant.
2. Add compliant learning headers and local ownership/hazard notes where
   warranted to these substantial tools:
   `Agentic/Skills/collapse_params.py`, `Agentic/Skills/loc_count.py`,
   `Agentic/Skills/skore-cpu-profiler/analyze_markers.py`,
   `Agentic/Skills/skore-cpu-profiler/cleanup_markers.py`,
   `Agentic/Skills/skore-render-test/analyze_perf.py`, and
   `Agentic/Skills/skore-render-test/perf_compare.py`.
3. Correct stale or vague invariant prose encountered in the exact IH0
   worklist, but do not expand this into an unrelated repository-wide prose
   rewrite. New findings are appended to the disposition table before repair.

## Required Work Products

IH0 creates and maintains, inside this plan, an exact checkbox table for every
source-bearing file selected by the regenerated assertion and invariant
inventories. Each checkbox names the file and applicable site IDs. Because this
is a targeted enforcement campaign based on a repository-wide inventory—not a
full comment-pass rewrite—files with no finding are recorded by the inventory
artifact and are not silently added to the edit checklist. Any expansion to a
subsystem/full-repository comment pass must first list every tracked source file
in that expanded scope as required by the Comment Quality Gate.

The plan also retains these reviewable artifacts under the normal validation
output location rather than committing generated reports:

- regenerated invariant-comment and assertion-site inventories;
- the exact 63-candidate successor disposition table;
- negative-control output for each new always-on guard;
- subprocess/death-test results for owned fatal paths;
- before/after assertion classification summary with no count target;
- independent ownership and false-pass review notes.

## IH0 Assertion Disposition Worklist

`A###` identities resolve to `assertion-sites.tsv`; that artifact retains all
132 plain assertions and their exact source text. The table below covers every
one of the 62 successor candidates. “Existing” means IH0 confirmed the
non-Debug lane in current source; the owning phase still reviews the whole file
and removes a duplicate assertion where the table says so.

Exact successor identities: A003, A004, A005, A010, A011, A017, A018, A019,
A020, A023, A024, A025, A026, A028, A030, A033, A048, A049, A050, A051,
A052, A058, A063, A065, A066, A070, A072, A073, A074, A075, A076, A077,
A078, A080, A087, A095, A096, A097, A098, A099, A100, A101, A102, A103,
A104, A105, A107, A109, A110, A111, A114, A115, A116, A117, A118, A119,
A126, A128, A129, A130, A131, and A132.

| Sites | Concrete owner / current non-Debug behavior | Primary lane | Owning phase and intended proof |
|---|---|---|---|
| A003-A004 | `LockOrderValidator::RecordAcquisition`; code is `_DEBUG`-only and Release performs no probe mutation | Lane P / Debug tripwire | IH2 retained the Debug-only graph/stack owner and added one pure classifier used by the production branches; focused proof covers invalid ids, cycles, held-stack exhaustion, and the valid control without claiming Release enforcement |
| A005 | `TornadoVisualPass::EnsureTransientCapacity`; Release dereferences a required frame snapshot | Lane F | IH2 now checks both Gameplay-owned frame borrows before capacity calculation and drawing; prepared, unprepared, and release-cleared lifecycle proofs cover the boundary |
| A010 | `Quaternion::RotateAboutAxis`; Release math is defined but a non-unit axis changes the requested rotation | Debug tripwire | IH2 retained the tripwire after proving every production caller supplies a unit basis or explicitly normalized axis; focused Profile proof pins the distinct non-unit legacy result and normalized output |
| A011 | `Vector3::Normalise`; Release intentionally propagates IEEE invalid values while `TryNormalise` owns caller-recoverable failure | Debug tripwire | IH2 retained the tripwire; focused proof distinguishes plain Profile IEEE propagation from `TryNormalise` failure that leaves the vector unchanged |
| A017 | `ColliderStore::RefreshBodyBindings`; current `bool` path returns false before dereference | Lane R | IH3 removed the duplicate assertion, preflighted every body row, and pinned mismatch returning false without mutation; a missing row is structurally excluded by the unchanged synchronous `PhysicsBodyStore::Count`/`RecordForModelIndex` contract |
| A018-A020 | `ColliderStore::RebindShapeReferences`; Release indexes per-kind backing unchecked | Lane F | IH3 fatal before sphere/box/hull indexing; subprocess corrupt-index negatives plus legal clone/rebind positives |
| A023-A026 | `ColliderStore` shape compaction; Release indexes or pops per-kind backing and assumes hull-row parity | Lane F | IH3 owner/capacity diagnostics before mutation; death tests for stale indices/parity and positive first/middle/last compaction |
| A028 | `DisjointSet::Reset`; Release writes `rowCount` borrowed rows | Lane F | IH3 fatal before `fill_n`; exact-capacity and one-over subprocess cases |
| A030, A033 | `PhysicsBodyStore` hot-state read/copy; Release indexes body arrays or writes the caller span | Lane F | IH3 validate before access; valid boundary and corrupt index/short-span subprocess proofs |
| A048-A052 | `PhysicsContactSolverStage::PrepareSideEffects`; one combined `SB_FATAL` already guards all five capacities | Lane F (existing) | IH3 remove the ceremonial assertions; exact-capacity and each-short-lane negative controls exercise the existing fatal |
| A058 | `PhysicsSleepController` awake-list audit; explicitly `_DEBUG`-only and not used for indexing authority | Lane P / Debug tripwire | IH3 retain with awake-list consistency tests |
| A063, A065-A066 | `Dx12GeometryOwner` draw entry points; Release dereferences mandatory device/frame/submission facets | Lane F | IH4 enforce lifecycle before use; before-init, valid-frame, and after-teardown subprocess/isolated backend cases |
| A070 | `Dx12TextureOwner::CreateTexture2D`; Release dereferences mandatory resource facets | Lane F | IH4 same lifecycle matrix plus device-result positives |
| A072-A075 | `PrimitiveBatchScope` draw methods; moved/inactive or wrong-kind use can dereference or silently skip | Lane F | IH4 owned fatal for scope misuse; visible, shadow, moved, inactive, and wrong-kind negative proofs |
| A076-A078 | `PrimitiveBatchRenderer::BindRenderResourceOwners`; Release can replace a live backend borrow with a mismatched owner | Lane F | IH4 fatal on mismatched resource/texture/geometry owners; same-owner rebinding remains legal |
| A080 | `RenderInstanceStore::Refresh`; current mismatch branch clears and returns before indexing | Debug tripwire with safe fallback | IH4 retain or remove duplicate after a mismatched-count test proves the Release clear/no-draw result |
| A087 | `Input::UnbindWindow`; wrong-window Release path leaves the binding unchanged | Debug tripwire with safe fallback | IH5 retain only with bind/valid-unbind/wrong-window/after-unbind policy tests |
| A095-A104 | `RuntimeInteractionController::ValidateState`; complete method is `_DEBUG`-only post-transition validation | Lane P / Debug tripwire | IH5 retain; transition-policy tests cover every gesture/capture/owner combination |
| A105 | `RenderResourceLifecycle::BuildRenderTargetPreviewSnapshot`; Release indexes a fixed target array | Lane F | IH4 bounded append or fatal before indexing; empty, exact ten-target, and synthetic eleventh-target proof |
| A107 | `SkyPass::Render`; Release dereferences the world-view sky owner | Lane F | IH5 fatal before draw; before-init/valid/after-release render-pass cases |
| A109, A111 | `UiTextPass` Profile lanes; `SKULLBONEZ_PROFILE_ENABLED` Release/Profile code dereferences `m_profiler` | Lane F | IH5 enforce startup binding outside `assert`; profile-enabled before/valid/after lifecycle proof |
| A110 | `UiTextPass` memory tab; invalid stats are presentation-unavailable, not process-fatal | Debug tripwire with safe fallback | IH5 skip sampling and publish unavailable state when invalid; valid/invalid memory-tab tests |
| A114-A116 | `SceneWorld` handle-map consistency loop; complete block is `_DEBUG`-only after Release already clears mismatched refreshes | Lane P / Debug tripwire | IH5 retain with reorder/refresh identity tests |
| A117-A119 | `SkyBox` load/reset; Release dereferences mandatory texture/config/asset/resource borrows | Lane F | IH4 fatal before internal lifecycle misuse while texture/device failures continue through existing Lane R results |
| A126, A128-A130 | `Terrain` render-resource methods; Release dereferences resource borrows or the required clip plane | Lane F | IH4 enforce before draw/build; missing-resource/clip-plane negatives and valid rebuild positives |
| A131-A132 | `WorldEnvironment::BuildFluidMesh`; Release dereferences mandatory asset/resource owners | Lane F | IH4 before-init/valid/after-release lifecycle proof |

## IH0 Allocation-Comment Disposition Worklist

`P###` identities resolve to `policy-invariant-sites.tsv`, which retains exact
path, line, owner hint, and complete comment text for all 45 rows. “Split” keeps
the mechanical correctness statement under `Invariant:` and moves only the
no-growth/phase/cap rule under `Runtime allocation policy:`.

| Sites | Current responsibility | IH6 disposition |
|---|---|---|
| P001 | `TextureCollection` fixed legacy table plus runtime no-growth rule | IH2 split first-free-slot/full-table correctness from `Runtime allocation policy:` and added exact-last-slot plus exhausted-table proof |
| P002-P004 | Slot-index safety and allocation-attribution thread/lifecycle correctness | IH2 retained `Invariant:` after full-file review; these remain mechanical ownership/safety claims |
| P005-P008 | Reserve hard caps, camera reserve, and tornado fixed gameplay budget | IH2 split identity/count correctness from `Runtime allocation policy:`; the 64-field exact/one-over parser proof pins the tornado boundary |
| P009-P011 | Collider atomic rows/dense handles and `PhysicsFixedList` relocation safety | Retain `Invariant:`; allocation wording explains atomicity/exception safety |
| P012 | Solver side-effect capacity is both deterministic completeness and no-growth policy | Split deterministic fit from `Runtime allocation policy:` |
| P013-P015 | Narrowphase disjoint offsets, DX12 frame-upload lifetime, and backend ownership publication | Retain `Invariant:` |
| P016 | Gameplay visual preallocation and steady-render exemption ban | Reclassify as `Runtime allocation policy:` |
| P017-P018 | Editor history overflow order and placement preflight authority | Retain `Invariant:`; “without allocating” is supporting behavior |
| P019-P020 | EditorTracer reserve/copy no-growth rules | Reclassify P019; split P020 cache-coherence invariant from allocation policy |
| P021-P022 | Priority overflow semantics and zero-sentinel request identity | Retain `Invariant:` |
| P023 | Prediction snapshot failure preserves publication atomicity and forbids opportunistic growth | Split publication invariant from `Runtime allocation policy:` |
| P024-P026 | Archive transaction state, marker completeness, and publication-density determinism | Retain `Invariant:` |
| P027 | Ghost draw bounded append/no steady growth | Split capacity proof from `Runtime allocation policy:` |
| P028-P030 | Steady-window measurement, resumable publication atomicity, and completed-frame authority | Retain `Invariant:` |
| P031-P032 | Prediction reserve-scope order and compact-arena construction | Split mechanical scope/arena safety from `Runtime allocation policy:` |
| P033-P035 | Trajectory erase identity, texture-slot binding, and complete overlay-packet work | Retain `Invariant:` |
| P036-P039 | Replay authoring/path/picking reserves and recorder growth approval | Reclassify no-growth/cap portions as `Runtime allocation policy:`; keep fail-closed/publication facts as invariants |
| P040-P041 | Contiguous ragdoll identity and authored-capacity activation order | Retain `Invariant:` |
| P042 | `SceneEntityStore` pre-reserved append | Reclassify the allocation-free claim as `Runtime allocation policy:`; keep transaction precondition local |
| P043-P045 | Handle identity and test-oracle anti-elision/cancellation semantics | Retain `Invariant:`; these are correctness/test-proof claims |

## Named Finding Dispositions

| Finding | Exact scope | Primary lane / owner | Phase and proof |
|---|---|---|---|
| H1 | `Terrain.cpp:98-170` constructed before validating `mapSize`/`stepSize` and floor-counted posts | Lane R, Terrain factory | IH1 validates positive/divisible/overflow-safe dimensions before construction; the former 5/2 mismatch is retained as a non-mutating negative-control fixture and every rejected shape preserves the caller's published Terrain |
| H2 | Complete tracked `Runtime/App/Run*.cpp`/`Run*.h` logical surface, including raw assertion sites A083-A085 | Lane F for mandatory internal owners; existing Lane R remains for device/file startup | IH5 records each dereference owner, moves lifecycle checks to that owner, and runs before/valid/after lifecycle negatives without creating a context bag |
| L1 | `ReplayPrediction.cpp:187` worker threshold comment | Taxonomy: `Why:` | IH6 reclassifies the performance rationale and proves no behavior diff |
| L2 | Six named substantial `Agentic/Skills/*.py` tools | Lane P documentation | IH6 adds truthful learning headers/local hazards and runs their bounded help/smoke paths |

## Exact Selected-File Checklist

The first 67 rows below are the complete IH0 selected-file union: 62 assert-only
candidates across 22 files, 45 policy-comment candidates across 35 files, the
complete eight-file `Run*` logical surface, Terrain construction, the worker
taxonomy site, and the six named tools. IH1 appended two supporting files and
IH2 appended eight more and IH3 appended six more, so the live checklist now
contains 83 rows. A row
remains unchecked until its owning phase inspects the entire file, applies the
comment audit, and records the focused proof. IH7 reruns `git ls-files` and
reconciles this list.

- [ ] `Agentic/Skills/collapse_params.py` — L2
- [ ] `Agentic/Skills/loc_count.py` — L2
- [ ] `Agentic/Skills/skore-cpu-profiler/analyze_markers.py` — L2
- [ ] `Agentic/Skills/skore-cpu-profiler/cleanup_markers.py` — L2
- [ ] `Agentic/Skills/skore-render-test/analyze_perf.py` — L2
- [ ] `Agentic/Skills/skore-render-test/perf_compare.py` — L2
- [x] `SkullbonezSource/Assets/TextureCollection.cpp` — P001, P002; IH2 full-file audit and fixed-table boundary proof
- [x] `SkullbonezSource/Core/Allocation/RuntimeAllocationTracker.cpp` — P003, P004; IH2 full-file audit retained mechanical invariants
- [x] `SkullbonezSource/Core/Allocation/RuntimeReserveAllocator.cpp` — P005, P006; IH2 full-file audit split hard-cap correctness from exploratory Replay policy
- [x] `SkullbonezSource/Core/LockOrderValidator.cpp` — A003, A004; IH2 full-file audit and Debug-policy classifier proof
- [x] `SkullbonezSource/Core/SceneCapacity.h` — P007; IH2 full-file audit and camera identity/policy split
- [x] `SkullbonezSource/Gameplay/TornadoField.h` — P008; IH2 full-file audit and exact/one-over authored-capacity proof
- [x] `SkullbonezSource/Gameplay/TornadoVisualPass.cpp` — A005; IH2 full-file audit and lifecycle Lane F proof
- [x] `SkullbonezSource/Maths/Quaternion.cpp` — A010; IH2 full-file audit, all-caller normalization proof, and Profile negative control
- [x] `SkullbonezSource/Maths/Vector3.h` — A011; IH2 full-file audit and plain/Try normalization proof
- [x] `SkullbonezSource/Physics/ColliderStore.cpp` — A017-A020, A023-A026, P009-P010; IH3 full-file audit, transactional refresh, per-kind Lane F, and compaction proof
- [x] `SkullbonezSource/Physics/DisjointSet.h` — A028; IH3 full-file audit and exact/one-over borrowed-scratch proof
- [x] `SkullbonezSource/Physics/PhysicsBodyStore.cpp` — A030, A033; IH3 full-file audit and hot-index/sleep-destination boundary proof
- [x] `SkullbonezSource/Physics/PhysicsFixedList.h` — P011; IH3 full-file audit retained relocation correctness separately from runtime allocation policy
- [x] `SkullbonezSource/Physics/Stages/PhysicsContactSolverStage.cpp` — A048-A052, P012; IH3 full-file audit, five-lane diagnostics, and allocation-policy split
- [x] `SkullbonezSource/Physics/Stages/PhysicsNarrowphaseStage.Execution.cpp` — P013; IH3 full-file audit retained mechanical island write-offset correctness
- [x] `SkullbonezSource/Physics/Stages/PhysicsSleepController.cpp` — A058; IH3 full-file audit retained Debug-only awake-membership tripwire with pure-classifier proof
- [ ] `SkullbonezSource/Rendering/DX12/MeshDX12.cpp` — P014
- [ ] `SkullbonezSource/Rendering/DX12/RenderBackendDX12.DynamicGeometry.cpp` — A063, A065-A066
- [ ] `SkullbonezSource/Rendering/DX12/RenderBackendDX12.Textures.cpp` — A070
- [ ] `SkullbonezSource/Rendering/PrimitiveBatchRenderer.cpp` — A072-A078
- [ ] `SkullbonezSource/Rendering/RenderInstanceStore.cpp` — A080
- [ ] `SkullbonezSource/Runtime/App/Init.cpp` — P015
- [ ] `SkullbonezSource/Runtime/App/Run.cpp` — H2
- [ ] `SkullbonezSource/Runtime/App/Run.h` — H2
- [ ] `SkullbonezSource/Runtime/App/RunFrame.cpp` — H2
- [ ] `SkullbonezSource/Runtime/App/RunLaunchOptions.h` — H2
- [ ] `SkullbonezSource/Runtime/App/RunLaunchOptions.Renderer.h` — H2
- [ ] `SkullbonezSource/Runtime/App/RunRender.cpp` — H2, P016
- [ ] `SkullbonezSource/Runtime/App/RunStartupState.h` — H2
- [ ] `SkullbonezSource/Runtime/App/RunTimerState.h` — H2
- [ ] `SkullbonezSource/Runtime/Editor/EditorCommandHistory.cpp` — P017
- [ ] `SkullbonezSource/Runtime/Editor/EditorObjectPlacement.cpp` — P018
- [ ] `SkullbonezSource/Runtime/Editor/EditorTracer.cpp` — P019-P021
- [ ] `SkullbonezSource/Runtime/Input/Input.cpp` — A087
- [ ] `SkullbonezSource/Runtime/Interaction/RuntimeInteractionController.cpp` — A095-A104
- [ ] `SkullbonezSource/Runtime/Planning/ReplayCauseInspection.cpp` — P022
- [ ] `SkullbonezSource/Runtime/Prediction/ReplayPrediction.cpp` — L1, P023
- [ ] `SkullbonezSource/Runtime/Prediction/ReplayPredictionArchive.Automation.cpp` — P024
- [ ] `SkullbonezSource/Runtime/Prediction/ReplayPredictionDrawing.cpp` — P025-P026
- [ ] `SkullbonezSource/Runtime/Prediction/ReplayPredictionPresentation.cpp` — P027-P028
- [ ] `SkullbonezSource/Runtime/Prediction/ReplayPredictionPublication.cpp` — P029-P030
- [ ] `SkullbonezSource/Runtime/Prediction/ReplayPredictionReserve.h` — P031
- [ ] `SkullbonezSource/Runtime/Prediction/ReplayPredictionRetainedGeometry.h` — P032
- [ ] `SkullbonezSource/Runtime/Prediction/TrajectoryStore.cpp` — P033
- [ ] `SkullbonezSource/Runtime/Render/RenderResourceLifecycle.cpp` — A105
- [ ] `SkullbonezSource/Runtime/Render/RuntimeRenderPasses.cpp` — A107, P034-P035
- [ ] `SkullbonezSource/Runtime/Render/UiTextPass.cpp` — A109-A111
- [ ] `SkullbonezSource/Runtime/Replay/ReplayAuthoringPackets.h` — P036
- [ ] `SkullbonezSource/Runtime/Replay/ReplayPathPackets.h` — P037
- [ ] `SkullbonezSource/Runtime/Replay/ReplayPresentation.cpp` — P038
- [ ] `SkullbonezSource/Runtime/Replay/ReplayRecorder.cpp` — P039
- [ ] `SkullbonezSource/Runtime/Scene/SceneAuthoredSetup.cpp` — P040
- [ ] `SkullbonezSource/Runtime/Scene/SceneController.Load.cpp` — P041
- [ ] `SkullbonezSource/Runtime/Scene/SceneEntityStore.cpp` — P042
- [ ] `SkullbonezSource/Runtime/Scene/SceneWorld.cpp` — A114-A116
- [ ] `SkullbonezSource/Runtime/Startup/StartupProbeHarnesses.cpp` — P043
- [ ] `SkullbonezSource/World/SkyBox.cpp` — A117-A119
- [ ] `SkullbonezSource/World/Terrain.cpp` — H1 complete with IH1 full-file comment audit and focused topology proof; A126, A128-A130 remain assigned to IH4
- [x] `SkullbonezSource/World/Terrain.h` — IH1 supporting type boundary; full-file comment audit and Profile compile proof
- [ ] `SkullbonezSource/World/WorldEnvironment.cpp` — A131-A132
- [ ] `SkullbonezTests/TestReplayRecorder.cpp` — P044
- [ ] `SkullbonezTests/TestReserveAllocator.cpp` — P045
- [x] `SkullbonezTests/TestTerrain.cpp` — IH1 supporting positive/negative proof; full-file comment audit
- [x] `SkullbonezSource/Assets/TextureCollection.h` — IH2 supporting fixed-table test seam; full-file comment audit
- [x] `SkullbonezSource/Core/LockOrderValidator.h` — IH2 supporting Debug-policy test seam; full-file comment audit
- [x] `SkullbonezSource/Gameplay/TornadoGameplay.cpp` — IH2 supporting visual lifecycle owner; full-file comment audit
- [x] `SkullbonezSource/Gameplay/TornadoVisualPass.h` — IH2 supporting lifecycle contract; full-file comment audit
- [x] `SkullbonezTests/TestQuaternion.cpp` — IH2 supporting unit/non-unit proof; full-file comment audit
- [x] `SkullbonezTests/TestRuntimeContracts.cpp` — IH2/IH3 supporting fatal, exact-capacity, and Debug-policy proof; full-file comment audit refreshed in IH3
- [x] `SkullbonezTests/TestSceneParserUnit.cpp` — IH2 supporting exact/one-over tornado proof; full-file comment audit
- [x] `SkullbonezTests/TestVector3.cpp` — IH2 supporting plain/Try normalization proof; full-file comment audit
- [x] `SkullbonezSource/Physics/ColliderStore.h` — IH3 supporting shape-topology test seam and owner contract; full-file comment audit
- [x] `SkullbonezSource/Physics/PhysicsBodyStore.h` — IH3 supporting hot-row test seam and owner contract; full-file comment audit
- [x] `SkullbonezSource/Physics/Stages/PhysicsContactSolverStage.h` — IH3 supporting five-lane consequence test seam and owner contract; full-file comment audit
- [x] `SkullbonezSource/Physics/Stages/PhysicsSleepController.h` — IH3 supporting Debug-classifier test seam and owner contract; full-file comment audit
- [x] `SkullbonezTests/TestPhysicsHandles.cpp` — IH3 supporting refresh, compaction, and sleep-export proof; full-file comment audit
- [x] `SkullbonezTests/TestSleepController.cpp` — IH3 supporting awake-membership classifier and consistency proof; full-file comment audit

## Phases

- [x] **IH0 — Rebase the exact inventory and freeze the worklist.** After
  `REST_STABILITY` closes, use `git ls-files` to regenerate tracked source
  scope, enumerate plain assertions and invariant claims, remove false
  positives only with written reasons, and append the exact per-file checkbox
  and per-site disposition tables to this plan. Record overlapping active-plan
  edits as reconciliations, not omissions. No production edit begins until
  every high/medium/low finding above has a site ID, owner, enforcement lane,
  and intended proof.

- [x] **IH1 — Repair terrain construction and sizing first.** Move validation
  ahead of every terrain factory construction; centralize checked post/cell
  count arithmetic; reject invalid/non-divisible dimensions through Lane R
  without partial mutation; add boundary, malformed-input, overflow, and
  normal-map tests. Run sanitizers or the repository's equivalent memory-safety
  proof against the formerly mismatched `mapSize = 5`, `stepSize = 2` shape.
  `TryValidateHeightMapDimensions` now owns one checked pixel/post/quad shape,
  and the private construction tag prevents height-map construction without
  that proof. The focused Profile witness passes 5 cases / 231 assertions,
  including zero, negative, undersized, non-divisible, exact-minimum, normal,
  and arithmetic-overflow shapes. Its Lane P 5/2 fixture proves the retired
  floor allocation would reserve four posts while traversal would write nine;
  the production path now rejects that shape before file access, allocation,
  construction, or output publication. This bounded false-pass control plus
  the Profile build is the repository-equivalent memory-safety proof for IH1.
  The touched-source comment audit is 3/3 with zero deferred files; related
  paths and the authority-free aggregate report are clean. The cumulative
  `tools\validate_tests.bat` gate passes 640 cases / 2,521,642 assertions, and
  `tools\validate_fast.bat` passes formatting, metadata, dependencies,
  ownership inventories, Profile/Automation/Debug builds, tests, and compiled-
  symbol reachability.

- [x] **IH2 — Harden Core, Maths, Assets, and Gameplay findings.** Adjudicate
  every IH0 site in these layers, preserve dependency direction, distinguish
  authored-input Lane R failures from internal owned fatals, and prove fixed-
  capacity texture/tornado boundaries. Do not introduce an upward include,
  service bag, callback pack, or new retained state owner. The texture table
  now admits its exact final slot and reaches a TextureCollection-owned Lane F
  before any full-table index can escape. Tornado authoring admits exactly 64
  fields and rejects the 65th through the existing Lane R parser transaction;
  visual capacity/draw work reaches Gameplay-owned Lane F before a missing or
  release-cleared frame borrow is dereferenced. Lock-order graph and stack
  instrumentation remains Debug-only, with its exact conditions exposed by a
  stateless classifier. All production `RotateAboutAxis` callers use unit basis
  axes or explicitly normalize, and its non-unit Profile behavior remains a
  pinned negative control; Vector3 plain/Try normalization lanes remain
  distinct. Focused Profile proof passes lock-order 1/5, texture 1/1, tornado
  visual 1/2, quaternion 8/33, tornado parser 2/10, runtime fatal 1/268, and
  Vector3 12/56. The touched/selected-source audit is 17/17 with zero deferred;
  related paths, glossary terms, dependency direction, and the authority-free
  aggregate report are clean. The cumulative `tools\validate_tests.bat` gate
  passes 645 cases / 2,522,019 assertions. After refreshing the exact
  `TornadoVisualPass::Render` body digest with the unchanged cohesive-frame
  owner judgment, `tools\validate_fast.bat` passes formatting, metadata,
  dependencies, all ownership inventories, Profile/Automation/Debug builds,
  tests, and compiled-symbol reachability.

- [x] **IH3 — Harden Physics findings.** Repair all `ColliderStore` index,
  identity, compaction, and body-binding assert-only paths identified by IH0;
  review the remaining Physics assertions by complete owner rather than line in
  isolation. Add positive branch coverage and subprocess negative tests while
  preserving hot-store layout, deterministic ordering, and the no-new-body-
  field rule. Collider refresh now preflights its complete body topology and
  returns false without mutation on a count mismatch. Shape rebind/removal,
  disjoint-set reset, hot-body access, sleep export, and every solver
  consequence lane now validate before unsafe access in Profile/Release, with
  owner-specific Lane F diagnostics. The five solver assertions were removed
  as ceremonial duplicates; the awake-list assertion remains an explicitly
  Debug-only Lane P tripwire backed by a pure four-case classifier. Focused
  Profile proof passes refresh 1/8, first/final shape compaction 1/12,
  sleep-state export 1/3, awake membership/classifier 2/45, exact scratch and
  consequence capacity 1/7, and the fatal harness 1/347. Existing middle-row
  compaction remains covered by the cumulative suite. The selected/touched
  comment audit is 14/14 with zero deferred files; P009-P011 and P013 remain
  mechanical `Invariant:` claims, while P012 now separates deterministic
  completeness from `Runtime allocation policy:`. The final
  `tools\\validate_tests.bat` gate passes 650 cases / 2,522,407 assertions on
  the current workspace. Formatting, project metadata, dependency direction,
  all non-reachability ownership inventories, staged-size policy, and the
  Profile/Automation/Debug build matrix also pass inside
  `tools\\validate_fast.bat`; its terminal compiled-symbol inventory is deferred
  only because Visual Studio is actively running and locking
  `Profile\\SKULLBONEZ_CORE.exe` while concurrent user-owned source edits are
  present. No IH3 source, test, or governance failure remains.

- [ ] **IH4 — Harden Rendering, DX12, World, and Scene findings.** Repair
  primitive-batch scope/lifetime checks, render-resource preview capacity, and
  all other selected sites in these layers. Exercise construction, valid use,
  move/teardown, exact-capacity, and misuse paths. Preserve feature-neutral
  Rendering vocabulary and dependency boundaries; do not hide an error behind
  a silent renderer state mutation.

- [ ] **IH5 — Harden Runtime, Input, Interaction, Replay, Planning, and UI
  findings.** Review `Run` as a logical surface with its sibling owners, move
  lifecycle enforcement to the concrete owner, and close every selected
  runtime assertion without creating a second retained state owner or a broad
  context/capability bag. Recheck Replay growth privileges and Runtime package
  direction. Coordinate with the post-forecast source layout captured by IH0.

- [ ] **IH6 — Correct invariant taxonomy and tool documentation.** Complete
  the 45-candidate allocation-policy review, reclassify the dispatch-threshold
  `Why:`, add the six tool learning headers, resolve every checked-file comment
  audit finding, run glossary and related-path checks, and reconcile the exact
  checklist. Comment-only edits remain separate from behavioral fixes where
  practical so review can distinguish prose from enforcement.

- [ ] **IH7 — Integrated proof, independent review, and closure.** Run the
  validation map, repeat the assertion/invariant inventories, reconcile every
  disposition and file checkbox, and obtain independent reviews for ownership,
  dependency direction, fatal-test false passes, and Release safety. Reopen the
  owning phase for any unchecked dereference/index/division, duplicate
  enforcement, stale comment, unproved failure lane, or asserted invariant
  whose non-Debug behavior remains undefined. Report checked/deferred counts;
  closure requires zero unexplained and zero silently deferred findings.

## Test And Failure-Proof Matrix

| Contract | Positive proof | Required negative proof |
|---|---|---|
| Terrain dimensions | Valid exact-divisible maps build identical topology | Zero/negative step, undersized/non-divisible map, arithmetic overflow, and former 5/2 mismatch return Lane R before construction |
| Collider shape rebinding | Sphere, box, and hull identities rebind after legal copy/move/compaction | Corrupt or stale index terminates through the owned fatal in a subprocess before indexing |
| Primitive batch scope | Visible and shadow scopes draw through valid retained resources | Inactive, moved-from, wrong-mode, and mismatched-resource use follows the documented always-on lane |
| Preview snapshot capacity | Empty, partial, and exact-capacity snapshots publish correctly | One-over-capacity cannot index storage and produces the selected owned failure |
| Runtime lifecycle pointers | Access during the declared initialized phase succeeds | Before-init and after-teardown access returns Lane R or terminates before dereference |
| Fixed-capacity/runtime allocation policy | Reserve/build phase reaches exact capacity without steady growth | Overflow or forbidden steady growth is rejected by the named owner; no new Replay privilege appears |
| Comment taxonomy | Each `Invariant:`, `Why:`, and runtime-allocation note matches its semantic role | Audit fixture or manual negative control demonstrates that misleading classifications are detected |

Fatal-path tests must launch a dedicated child executable or subprocess and
assert the exit/status/diagnostic contract. A test that invokes `SB_FATAL` in
the main suite process is not acceptable. Each negative control must be run
once with the guard intentionally disabled or the old implementation retained
in a test fixture, proving that the test would have caught the defect.

## Validation Map

Do not run repository validation scripts while iterating. Select focused unit
or subprocess tests per phase and defer the repository gates until the work is
PR-bound. IH7 runs, at minimum:

```powershell
tools\validate_fast.bat
tools\validate_cpu.bat
tools\validate_full.bat
tools\validate_hosted.bat
tools\validate_dependency_graph.bat
python tools/inventory_authority_free_aggregates.py --check
python tools/inventory_glossary_terms.py --check
python tools/inventory_wide_signatures.py --check
python tools/inventory_extraction_scars.py --check
python tools/inventory_function_complexity.py --check
```

Add the owner-specific Physics, Rendering/DX12, Runtime, and tool-test commands
selected during IH0. A pre-existing baseline mismatch is reported with its
artifact and owner; it is not hidden by refreshing a golden. Any behavior
change introduced by this plan requires the applicable validation even if the
associated comment edit is documentation-only.

## Acceptance Criteria

- Every high-, medium-, and low-severity finding has a checked site/file row,
  named owner, enforcement lane, and focused positive/negative proof.
- No plain assertion is the only barrier before an unsafe operation in Profile
  or Release among the regenerated candidate set.
- Terrain rejects invalid dimensions before construction and its allocation and
  traversal counts use one proved formula.
- Collider rebinding, primitive-batch scope, preview capacity, and `Run`-
  surface lifecycle failures are enforced outside Debug.
- Allocation/growth comments use the runtime-allocation taxonomy, correctness
  claims remain `Invariant:`, and the dispatch threshold is `Why:`.
- The six named tool scripts satisfy the comment standard without claiming
  production invariants they do not own.
- Dependency, growth-privilege, hot-store-field, aggregate, signature,
  extraction-scar, complexity, reachability, glossary, and related-path rules
  remain green or have an already-authorized owner disposition.
- The final report names this plan path, checked count, deferred count, and all
  remaining unchecked rows. Completion requires zero unexplained/deferred
  audit findings and an independent review with no blocking ownership finding.

## Non-Goals

- Adding one assert for every invariant comment.
- Replacing recoverable diagnostics with process termination.
- Converting assertions into exceptions or inventing a second result type.
- Creating frozen assertion/comment counts, historical-debt budgets, or a new
  regex governance checker.
- Broad code cleanup, owner extraction, formatting, or comment rewriting not
  required to enforce an audited contract.
- Adding a Replay reserve registration, Physics hot-store field, Runtime
  package, compatibility alias, forwarding header, callback pack, or service
  bag to make enforcement convenient.

## References

- `AGENTS.md`
- `Agentic/Reference/comment-style-guide.md`
- `Agentic/Reference/code-style-guide.md`
- `Agentic/Skills/comment-style-audit/skill.md`
- `Agentic/Plans/MASTER-PLAN.md`
- `tools/inventory_authority_free_aggregates.py`
- `tools/inventory_glossary_terms.py`
- `tools/inventory_wide_signatures.py`
- `tools/inventory_extraction_scars.py`
- `tools/inventory_function_complexity.py`
