# Governance review — 18 September 2026

Reviewed C:/SkullbonezCore at HEAD c686f653b on nightrunner-17th-SEP-26, including the substantial pre-existing working-tree changes. This is a source-level architectural review, not runtime certification. Physics Window and Interactive Grass are explicitly unfinished work; findings against them are integration issues, not claims that their plans were falsely closed. No production source, governance policy, baseline, or plan was changed.

The main pattern is ownership erosion despite clean mechanical gates. Concrete domain decisions have accumulated in composition code; some named owners retain storage while callers still implement their state machines.

“Blocking” below means the repository's existing contract requires correction before the affected work can be considered closed. It does not mean every item is a crash or has equal urgency.

## Governance patch status and application

The ten findings below each include proposed policy wording, implementation targets and a recurrence test. These patches are specified for implementation; they have not been applied to AGENTS.md, review skills, production code, tests or CI by this report update. The audit remains a dated observation of the reviewed tree.

For every rule delegated to review, update both `Agentic/Skills/rubber-duck/SKILL.md` and `Agentic/Skills/carmack-test/SKILL.md` in the same implementation change as the authoritative AGENTS.md clause. Extend existing rules rather than maintaining a competing policy in this audit. Compiler checks establish syntax and type facts; behavioral tests establish outcomes; reviews establish semantic ownership. None alone guarantees that future drift is impossible.

A finding closes only when its source repair, applicable policy/gate changes and recurrence proof are complete. Record the evidence in the existing owning plan and commit sections. Do not close a finding on prose alone, a renamed wrapper, a green unrelated gate, or a waived failure. Preserve fast incremental development and run heavier gates at the existing mapped boundaries. This report does not authorize golden changes or introduce count budgets, source-site exceptions or a second permission ledger.

## Top 10, ordered by practical priority

### 1. [Blocking] Replay snapshot compatibility has multiple authorities, already disagreeing

`SkullbonezSource/Physics/PhysicsSolverSnapshot.h:60` introduces settings snapshot version 10. `PhysicsEngine.cpp:1401` emits it after interactive settings changes. `Runtime/App/ReplayRestoreOperations.h:73` accepts it for continuation, but `:140` rejects any snapshot newer than the hull version, 9. `PhysicsWorld.cpp:645` and the artifact reader accept through version 10.

Consequently, settings-bearing snapshots can be recorded/read and pass continuation preflight but cannot pass the actual App restore operation. This is a current working-tree defect established from the predicates; I did not reproduce it in a running engine. The broader problem is a version-policy edit requiring coordination across several owners. Put supported-version and continuation decisions behind Physics-owned predicates, with an integration regression that edits settings, records, restores, and resumes. Codec round-trip coverage alone does not exercise the failing boundary.

**Governance patch — compatibility must have one semantic owner.**

Add under `AGENTS.md / Replay Boundary Rule`:

> Physics owns the supported snapshot versions and their permitted uses: decode, inspection and authoritative continuation. Artifact readers and App restore paths must consume that policy; they must not maintain independent current-version bounds. A snapshot-version change is incomplete until a test follows each newly supported version through the production capture, artifact and live-restore boundaries, and proves unsupported versions reject before live mutation.

Enforcement: put the predicates in a Physics-owned header; use them from Physics, Replay codecs and App. Extend `SkullbonezTests/TestReplayArtifact.cpp` with the version-10 edit/record/save/load/restore/resume case and future-version no-mutation rejection. The two review skills must trace every version decision, including fallback restore, when snapshot policy changes. The existing dependency checker cannot prove agreement between integer comparisons.

Recurrence proof: temporarily restore the obsolete version-9 ceiling in a test workspace; the integrated restore regression must fail. Run the mapped unit, Physics and Replay-fidelity gates after implementation. Do not refresh a golden to accept a failed restore.

### 2. [Blocking] Run has resumed owning product state

`Runtime/App/Run.h:208` retains startup physics settings, settings notices, and a UI step request; `:213` onward retains comparison model, panel, load job, request id, foreground/camera/activation flags and saved camera. `RunComparison.cpp:49` implements workspace transitions; `:163` implements comparison publication, scene loading, camera changes and automation completion. `InputFrame.cpp:1972` implements physics-settings editing and persistence policy.

These are domain decisions, not just construction or frame sequencing. They contradict Run.h's claim at line 8 and the God-Object Closure Rule. Extract a comparison-session owner with its loading/camera/foreground transitions, and a physics-controls owner with edit/reset/save behavior. Keep App responsible for applying typed cross-owner actions. Splitting more Run translation units would not address this.

**Governance patch — review additions to the composition root on every feature.**

Extend `AGENTS.md / God-Object Closure Rule`:

> The composition-root restriction applies to feature additions as well as cleanup closure. For each new retained field or business operation in Run or its sibling implementation files, the review must identify its domain owner. Run may retain concrete owners and process lifecycle facts; feature loading state, selected targets, saved feature cameras, settings-edit policy and product feedback belong to the responsible domain owner. Moving a body to another Run file or hiding it behind a forwarding facade does not satisfy this rule.

Enforcement: add this trigger to both review skills, with review of the logical Run surface across headers and implementations. Require a concrete owner/transition explanation in the existing commit `Ownership:` section; do not create a field-count budget or a second ledger.

Recurrence proof: test comparison load/cancel/workspace return and physics edit/reset/save on their domain owners without constructing Run. Review must establish that App only applies their typed outcomes. These tests prove behavior; the review must separately verify that retained state and decision authority actually moved.

### 3. [Blocking] Planning stores the velocity workflow but App owns its transitions

`Runtime/Planning/ReplayPlanningRuntime.h:74` and `:78` return mutable references to pending edits and velocity divergence. `Runtime/App/ReplayVelocityDivergence.cpp:97`, `:163` and `:178` replace or mutate that state directly. `ReplayScrubberTools.cpp:1457` onward independently changes resume, readiness and playback flags; `ReplayRuntime.cpp:2566` publishes readiness.

The invariant connecting an active comparison, the frozen original prediction owner, modified prediction readiness, pending input and playback exists across several App methods. The Planning owner cannot enforce it because callers can change individual flags. Replace mutable access with a velocity-comparison state machine: begin/edit/release/ready/accept/cancel, with illegal transitions rejected and typed effects returned to App. This is authority transfer, not a renamed state struct.

**Governance patch — state owners must own legal transitions.**

Extend `AGENTS.md / Invariant Ownership Rule`:

> An owner responsible for workflow order must not expose mutable records that let callers change its phase, readiness, acceptance or cancellation state independently. Its public commands enforce legal transitions and return typed effects; observations are read-only values. New transition paths must join the same owner rather than add flags interpreted by App. Mutable storage access is not an ownership boundary.

Enforcement: replace Planning's mutable edit/divergence accessors with commands. Both review skills must follow all writes to the transition state, including writes through returned references. Add focused velocity-comparison tests to the owning subsystem's test file and compile-time API checks that public observations cannot mutate state.

Recurrence proof: exercise release before original prediction is ready, duplicate release, accept before modified readiness, cancel during a build, and scene reset while active. Assert owner identity and legal terminal state, not just visible output. A caller attempting the former mutable access must fail compilation; transition tests must reject invalid ordering.

### 4. [Blocking] Operator features remain inside Replay infrastructure

`Runtime/Replay/ReplayAuthoring.h:209` owns cause filtering, `:321` owns Alt-key history, `:336` owns velocity drag setup, and `:428` retains cause-tree and velocity-editor state. `ReplayAuthoringPackets.h:9` explicitly describes filter text, chip state and key edges. These are operator workflows and presentation interaction, while the Replay-Family Placement Rule assigns them to Planning or a higher product package.

The practical result is that developing a cause inspector or velocity gizmo changes the recorded-data package. Move those operator owners and their interaction state to Planning; retain recording, artifact/provenance and necessary immutable recording values in Replay. Keep the dependency graph honest during the move rather than introducing forwarding headers.

**Governance patch — feature placement is judged by responsibility.**

Extend `AGENTS.md / Replay-Family Placement Rule`:

> Replay and Prediction changes must classify new retained state and operations by responsibility. Operator selection, filters, widget geometry, key edges, gestures and product workflow policy belong in Planning or an explicitly admitted higher product package. A clean include graph, immutable packet name or Replay-prefixed type does not authorize product behavior in infrastructure. Shared recording identity and serialized provenance may remain in Replay; operator controls over them may not.

Enforcement: add this classification to both review skills for any Replay/Prediction feature change. Move the identified owners and adjust `tools/dependency_graph_rules.json`, its fixtures and generated proof together where the move requires different edges. Do not add a vocabulary blacklist or forbidden-type-count gate.

Recurrence proof: operator filtering and velocity gestures must be tested through the Planning owner, while existing artifact/recording tests continue to compile and pass independently of the moved operator implementation. The dependency gate proves direction after the move; review proves semantic placement.

### 5. High: object style 14 has become a hidden feature switch

`Runtime/Render/RenderPresentationSettings.h:46` applies the Split Future preset. `Rendering/PrimitiveBatchRenderer.cpp:1036` interprets object style 14 as rounded geometry. `Runtime/Render/RuntimeRenderer.cpp:1513` interprets it as SMAA; `:2380` uses it for overlay presentation, and `Runtime/App/InputFrameExecution.cpp:734` uses it to detect the toggle's authored state.

A material-style value now selects mesh shape, post-processing and feature behavior across layers. Renumbering/reusing the style or wanting rounded boxes without the complete look becomes a coordinated change. Resolve the authored preset at a Runtime presentation boundary into explicit geometry, antialiasing and overlay values. Rendering should consume those generic choices.

**Governance patch — presets resolve into independent rendering choices.**

Extend `AGENTS.md / Dependency Direction Rule`, beside Rendering neutrality:

> Runtime presentation resolves authored feature presets into explicit Rendering-owned value choices before submission. A material-style identifier must not implicitly choose geometry, antialiasing, overlay policy or resource lifecycle. A new render choice must identify the contract that carries it and the owner that resolves it. Numeric encodings are permitted for serialization and shader ABI, but must not conceal unrelated product decisions.

Enforcement: introduce explicit geometry and post-process selections using the existing Rendering value seams; resolve the Split Future preset once in Runtime. Both review skills must trace preset-derived values into mesh selection, pass scheduling and overlays. Avoid a regex that merely bans the number 14.

Recurrence proof: test rounded/plain geometry independently of SMAA on/off, then verify the authored preset still resolves to the intended combination. Run the DX12 screenshot and bounded stress gates, preserving existing golden-approval rules. The independence test must fail if the unrelated choices are coupled again.

### 6. High: useful feature regressions are disconnected from the regular gates

`tools/validate_split_future_lines.py:1` verifies target/path identity and corner accents. `tools/validate_split_future_smaa.py:1` checks the post graph, edge quality and resize behavior. The unified UI family includes `validate_unified_scene_ui.py` and `validate_unified_tools_ui.py`. Searching tools and .github found no caller of these four scripts. They are absent from the explicit `validate_skarness.bat` suite; that suite itself is correctly reached through `validate_automation.bat:77`.

This does not prove they were never run, nor that all their behavior lacks other tests. It means their specific assertions do not automatically protect later changes. Classify retained scripts as maintained regressions or one-off evidence; connect maintained assertions to bounded feature lanes and the file-to-gate map.

**Governance patch — retained regressions must be reachable and fail the gate.**

Extend `AGENTS.md / File To Validation Mapping` and the surrounding validation rules:

> A native regression retained under tools/validate_* must have an executable route from its owning validation lane and a file-to-gate mapping before the feature closes. One-off investigation code must be labelled and stored as evidence rather than advertised as a maintained gate. Acceptance must prove both that the lane invokes the regression and that its failure propagates to the lane result; a script's existence or past manual run is insufficient.

Enforcement: wire the retained Split Future and unified UI checks into their appropriate renderer, Automation or UI lanes. Consolidate native case selection into one executable manifest consumed by the runners; do not create an independently maintained second inventory. Extend the runner's self-tests to reject missing selected scripts and propagate failed child processes. Document any graphics runner requirement honestly; hosted CPU checks do not execute native pixel assertions.

Recurrence proof: use a small failing fixture to prove lane failure propagation and a removed-script fixture to prove reachability validation. Produce an execution report naming selected cases. Do not launch a deliberately broken real engine for the runner's negative control.

### 7. Medium: Skarness loses semantic typing after JSON parsing

`Runtime/Automation/SkarnessProtocol.h:143` uses one command struct with number through sixthNumber and unsignedInteger through sixthUnsignedInteger. `SkarnessHost.cpp:620` interprets six numbers as linear/angular velocity, while `:628` interprets six integers as a causal evidence identity. Camera pose reuses the six numbers differently. `SkarnessProtocol.h:183` similarly combines comparison, object, scalar and grass results behind independent presence flags.

The compiler cannot prevent swapping evidence-stamp fields, reading the wrong slot, or constructing contradictory result shapes. Use command-specific value payloads in a tagged union/variant and typed result alternatives. Keep the external protocol stable; the improvement belongs at the parser-to-App boundary. The capability catalog and parser also separately restate argument contracts, which should share structured descriptors where practical.

**Governance patch — transport commands preserve domain types.**

Add under `AGENTS.md / Skarness Runtime Interaction Workflow`:

> Parsing external input must produce a command-specific payload before crossing into App or a domain owner. Unrelated operations must not communicate through ordinal scalar slots or independently selectable result-presence flags. Evidence identities use named typed fields, and command dispatch handles the payload's actual alternative. Protocol compatibility is owned at the parser/serializer boundary, not by weakening the internal command contract.

Enforcement: replace generic command/result bags with typed alternatives and exhaustive dispatch. Reuse existing domain value identities where appropriate. Keep the external protocol compatible; update the capability descriptions, parser and tests together. Add the rule to both review skills for Automation boundary changes.

Recurrence proof: compile-time controls reject a camera payload passed to the velocity handler; malformed/missing identity inputs are rejected without mutation; each typed result serializes only its applicable fields. Existing transport, command-coverage and state-stream regressions remain required. Strong identity types, not named uint64 fields alone, should prevent swapping semantically different stamp components where practical.

### 8. Medium: the documented exhaustive source-design safety net is actually incremental

`AGENTS.md:924` says hosted CI owns the exhaustive repository scan. Its retained-policy invocation in `tools/run_retained_policy_group.ps1:17` supplies neither --all-first-party nor an explicit complete file list. `tools/check_source_design.py:845` therefore selects branch/working-tree diff paths under SkullbonezSource. Newly untracked .cpp files are also absent from those git diff results, even when added to a project; the dependency checker, in contrast, explicitly includes untracked sources.

The incremental lane is reasonable, but it cannot justify a whole-engine clean claim or reliably inspect every new file before staging. Reconcile the documented promise with an intentional periodic/closure inventory, and include new project-owned source in local selection. Preserve a fast incremental path rather than making every edit trigger a full scan.

**Governance patch — scan scope must be executable and accurately reported.**

Replace the unqualified exhaustive-hosted claim in `AGENTS.md / File To Validation Mapping` with:

> Ordinary source-design checks are incremental and report the exact selected files and compile contexts. Selection includes branch changes, staged and unstaged edits, and new first-party source/header files, including untracked project additions; a selected file with no valid compile context is an error. Whole-engine ownership closure requires the explicit all-first-party compiler scan. A workflow may claim exhaustive coverage only when its command selects that scope and completes without infrastructure errors.

Enforcement: update `tools/check_source_design.py` selection and its self-tests; retain the incremental call in `tools/run_retained_policy_group.ps1`. For a whole-engine closure, explicitly run `python tools/check_source_design.py --repo . --all-first-party` in the chosen closure lane. An advisory `--inventory` run is not a substitute for this policy check. Update workflow/docs wording together; do not silently impose a full scan on every small edit.

Recurrence proof: fixtures cover a tracked modification, staged addition, untracked project-owned .cpp, new header and missing compile context. A planted violation in an unchanged file must be found by the all-first-party lane. Report incremental versus exhaustive scope in validation evidence and both review skills.

### 9. [Blocking] Renderer transactions violate the stated borrowing rule

`Runtime/Render/RuntimeRenderer.h:122` defines WorldOverlayTransaction with a useful tested phase cursor, but `:164` retains a RuntimeRenderer pointer plus terrain and replay packet borrows across method returns. `RuntimeRenderer.cpp:2629` reaches back through that pointer to execute the overlay phase. The Invariant Ownership Rule instead requires transactions to retain values/cursor and borrow owners in phase methods.

This is a contract/design conflict, not a demonstrated dangling-pointer failure. Existing tests in `SkullbonezTests/TestRuntimeContracts.cpp:807` cover abandonment and duplicate submission, and the renderer outlives ordinary frame transactions. Nevertheless, the implementation relies on broader caller lifetime discipline than the declared policy permits. Make the continuation value/phase-owned with synchronous participant borrows, or explicitly settle and document a narrower legitimate frame-borrow policy before treating it as compliant. Also review the public ResourceLifecycle and raw DX12-owner getters at RuntimeRenderer.h:270 onward: they allow callers to bypass the supposedly focused render operation boundary.

**Governance patch — phase correctness and borrow lifetime are separate obligations.**

Clarify `AGENTS.md / Invariant Ownership Rule` without relaxing it:

> A transaction's tested phase cursor does not authorize retaining participant owners or source-backed payload borrows between phase calls. Transactions retain owned values and phase state; each phase borrows its participants synchronously. Review must separately prove transition correctness, participant lifetime and authority restriction. A noncopyable or nonmovable type alone proves none of the latter two.

Enforcement: change the world/overlay continuation to consume explicit participant borrows in the applying operation, with detached retained values, and preserve its abandonment/double-submit guards. Audit public raw renderer-owner getters against actual callers, replacing broad access where a renderer operation expresses the required capability. Both review skills must inspect stored pointer/reference members and hidden aliases in transaction types.

Recurrence proof: retain the existing phase-failure tests and add coverage proving the continuation uses its captured values rather than a later-mutated source packet. Compiler-backed inventory can identify borrowing candidates; manual review must classify them, including references nested in spans or helper types. Do not add a blanket no-pointer rule for all engine records. This patch chooses compliance with the existing transaction rule; it does not silently grandfather the current implementation.

### 10. [Blocking] An explicitly banned parameter-wrapper shape remains

`Runtime/Render/RuntimeRenderFrameValues.h:155` defines RuntimeRenderBroadphaseDebugView with only `PhysicsEngine& physicsEngine` and no behavior. `RuntimeRenderer.cpp:1879` immediately uses that member to query the engine. The wrapper narrows no authority and enforces no invariant; it grants mutable engine authority to a diagnostic read path.

This is smaller than the other findings, but it is an exact match for the governance rule banning behavior-free single borrowed-owner aggregates. Pass a const engine borrow directly if that is the intended boundary, or publish bounded broadphase diagnostic values that actually narrow the available data.

**Governance patch — expose nominal wrappers to review mechanically.**

Retain the existing single-borrow aggregate prohibition and add under `AGENTS.md / Governance Review Model`:

> The compiler-backed source-design inventory must surface behavior-free single borrowed-pointer/reference aggregates for explicit ownership review. A report must name the enforced rule or the replacement boundary for each candidate in scope. A candidate with no invariant is blocking. Strong value identities and classes with actual behavior are classified from their semantics, not admitted through a name or source-site exception.

Enforcement: extend the existing Clang-based inventory and its negative controls; do not add a parallel source-text checker. Both review skills must consume these candidate results when affected aggregates change. Keep discovery advisory where the compiler cannot decide whether a pointee is an owner; semantic review is mandatory, with no frozen count or permission ledger.

Recurrence proof: the current one-reference wrapper is reported; a renamed copy is also reported. Fixtures distinguish a real behavior owner and a scalar strong identity from the banned shape. After replacement, diagnostic code must have only const access or detached bounded values, with a compile-time test preventing mutation through the public diagnostic seam.

## Required ownership-review answers

1. Aggregate ownership: not uniformly sound. Finding 10 is an exact invariant-free wrapper. Finding 3 stores a workflow without controlling mutation. WorldOverlayTransaction does enforce a phase rule, but violates the separate borrowing contract in finding 9.
2. Capability slices: the inspected renderer exposes both focused submissions and broad mutable resource-owner access. I did not establish an exact operation receiving every nominal slice; I am not claiming that specific violation. The broader access and lifetime issues are identified above.
3. Incomplete extractions: the compiler-backed check found no member-prefixed locals, pure parameter aliases or protected immediate-unpack findings in the six inspected translation units. This is not a whole-tree clearance.
4. Rename evasion: no specific delete-and-rename evasion was established. Run's distribution across sibling translation units still preserves the authority in finding 2; correcting filenames alone would not close it.
5. False claims: Run.h:8 says it does not absorb domain business state, contradicted by finding 2. ReplayV2Artifact.cpp:28 still says versions newer than v5 fail closed, while :80 defines v6 as current. The latter is a stale protocol comment, not a separate top-ten issue.

The exact extrusion signal (three sibling participant/input/output structs plus a wide apply free function and ordering comments) was not established in the sampled paths. The velocity workflow is still a concrete invariant-ownership defect, with the proposed state-machine owner described in finding 3.

Replay boundary: the dependency scan found no forbidden downward include. I did not complete an allocator registration/cap accounting audit and make no claim that the growth inventory is clean.

## Checks and limitations

- CodeGraph was used as the initial map. Its status reported 1 added and 26 modified indexed files, so findings were checked against current on-disk source.
- `python tools/check_dependency_graph.py --repo .`: PASS; 0 findings, 0 runtime repair-plan debt; generated proof current.
- `python tools/check_source_design.py --repo . --files SkullbonezSource/Runtime/App/RunComparison.cpp SkullbonezSource/Runtime/App/ReplayRuntime.cpp SkullbonezSource/Runtime/Render/RuntimeRenderer.cpp SkullbonezSource/Runtime/UI/GameUI/UI.cpp SkullbonezSource/Runtime/Automation/SkarnessHost.cpp SkullbonezSource/Runtime/App/OperatorCommandApplication.cpp`: PASS; 6 sources, 20 compiler contexts, 0 findings, 0 infrastructure errors. Exact output is preserved in [governance-review-2026-09-18-source-design.txt](governance-review-2026-09-18-source-design.txt) beside this report.
- Reviewed recent churn, owner headers, actual call paths, protocol/restore policy and validation wiring. No full build, runtime regression, allocator audit, performance benchmark or whole-engine compiler scan was run. No external CI status was inspected.

Recommended sequence: fix the restore mismatch before integration; repair comparison/velocity ownership; move operator behavior out of Replay; connect the retained regressions; then narrow rendering and automation boundaries.
