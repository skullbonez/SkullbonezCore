# Adversarial Review Round 3 — Execution Progress Checklist

Companion to `Agentic/Plans/TODO/adversarial-review-round-3.md`. This file is
the fine-grained working checklist; it does not add a second MASTER ledger
denominator. Tick a sub-item only when its edit is complete and the named
evidence exists; tick a task header only when the owning plan task closes with
its validation evidence.

Progress header source: the owning ledger row is
`adversarial-review-round-3` (10 tasks). Resolve `DONE` and the overall
percentage from `Agentic/Plans/MASTER-PLAN.md` after each commit.

## R1 — Header `using namespace` removal

- [x] Inventory every name each directive currently resolves in
      `UI/UIFrameComposition.h` (build once with directives commented out and
      collect the error list as the work queue).
- [x] `UI/UIFrameComposition.h:63-70`: delete all eight directives; qualify
      names or add class/function-scoped `using`-declarations only.
- [x] `Scene/TestSceneParserSchema.h:1040`: stop exporting
      `TestSceneParserDetail` into includers; qualify call sites inside the
      header instead.
- [x] `Rendering/ShaderReflectionContracts.h:217`: replace the directive with
      explicit `UnifiedRasterRootSignature::` qualification (or keep it only
      if it is provably function-scoped and bounded — record the decision).
- [x] Prove clean: `rg -n '^\s*using namespace' --glob '*.h' SkullbonezSource`
      returns zero rows.
- [x] Zero-warning Profile build evidence captured for the PR gate.

Evidence (2026-07-13): the deliberate no-directive inventory build exposed the
UI composition/palette and five scene-parser translation units that had relied
on header leakage. Headers now qualify owner types directly; implementation
files use explicit finite helper imports. The header grep returned zero rows,
and `tools\validate_fast.bat` passed in 53.7s with 177/177 doctest cases,
4,059/4,059 assertions, and zero-warning Profile/Debug builds.

## R2 — Scene capacity constant ownership

- [x] Choose and record the namespace (proposal:
      `SkullbonezCore::Scene::Capacity` or the R5 target namespace — decide
      once, consistent with R5 mapping).
- [x] Wrap `TOTAL_CAMERA_COUNT`, `TOTAL_TEXTURE_COUNT`,
      `DEFAULT_GAME_MODEL_CAPACITY`, `MAX_GAME_MODELS`, `DEFAULT_GAME_MODELS`
      in that namespace; keep `constexpr` (they size fixed arrays — an
      `extern const` would break constant expressions; decision recorded).
- [x] Update every consumer qualification (`rg -l 'MAX_GAME_MODELS|TOTAL_CAMERA_COUNT|TOTAL_TEXTURE_COUNT|DEFAULT_GAME_MODEL'`).
- [x] Fix the stale Related block: remove the `Core/Common.h` aliasing claim;
      point at the real `Runtime/Scene/SceneController.h`.
- [x] Decide `GameObjects/` directory fate: relocate `SceneCapacity.h` to
      `Runtime/Scene/` (and update `.vcxproj` + `.filters`) or record why the
      directory remains; no orphan folder left behind.
- [x] `tools\validate_project_filters` clean after any file move.

Decision and evidence (2026-07-13): `SkullbonezCore::Scene::Capacity` owns the
five `constexpr` budgets. The domain namespace stays correct through R5 and
does not make cross-system ceilings composition-root state. The header moved to
`Runtime/Scene/SceneCapacity.h`; the empty `GameObjects/` source/filter roots
were retired, and every source/test consumer is explicitly qualified. Project
filters passed with 663/663 entries. Fast, physics, full, and DX12 renderer
gates passed; physics remained a 44,401-line byte-exact match, DX12 reported
zero InfoQueue errors with matching screenshots, and the 61.8s bounded stress
run completed with exit 0. Allocation-policy self-test and the 321-file repo
scan passed with zero allowlist errors.

## R3 — `Common.h` platform prelude removal

- [x] Inventory which of the 43 `Common.h`-including headers actually use
      Win32 types (HWND, DWORD, LARGE_INTEGER, CRITICAL_SECTION, …) versus
      only C/C++ stdlib.
- [x] Add `Core/PlatformWin32.h` owning `WIN32_LEAN_AND_MEAN` + `<windows.h>`
      (+ `NOMINMAX` decision — record it; current code uses `(std::max)`
      parenthesis-guards, so `NOMINMAX` is the cleaner end state).
- [x] Remove `<windows.h>` and `WIN32_LEAN_AND_MEAN` from `Core/Common.h`;
      keep the CRT debug block and stdlib includes, or shrink further if
      consumers allow.
- [x] Point true platform consumers (Window, Timer, WorkerPool, Input, DX12
      device/backend, Init/Run message pump, capture/file IO) at the new
      prelude or at `<windows.h>` in their `.cpp`.
- [x] Compile-fix the fallout headers with narrow stdlib includes only —
      physics/maths/UI-layout/scene headers must not gain a platform include.
- [x] Prove: `rg -ln 'windows.h' SkullbonezSource` lists only platform-owner
      files; zero warnings in Debug, Profile, Release.

Decision and evidence (2026-07-13): the current tree had 38 direct
`Common.h`-including headers (the plan's 43 count was pre-round-3 evidence).
`PlatformWin32.h` is the single Windows SDK prelude and defines both
`WIN32_LEAN_AND_MEAN` and `NOMINMAX` before `windows.h`. Runtime window/input,
automation, file/diagnostic, profiler, GDI text, and DX12 owners include it
explicitly. `Terrain` replaced `BYTE`/`UINT` with standard fixed-width types
and value initialization, so world/physics-facing headers stayed platform-free.
Case-insensitive grep finds `windows.h` only in `PlatformWin32.h`.
Profile (13.7s), Debug (24.9s), and Release (38.7s) built with zero warnings.
Fast, full, and DX12 gates passed; full retained the 44,401-line byte-exact
physics baseline, DX12 reported zero InfoQueue errors with matching screenshots,
and the bounded 62.2s stress run exited 0. The profiler-marker probe emitted
`requested` and `enabled`; because marker mode intentionally keeps the runtime
alive, PID 46576 was stopped after the 34s probe window.

## R4 — Compile out engine exceptions

- [x] Set `<ExceptionHandling>false</ExceptionHandling>` and add
      `_HAS_EXCEPTIONS=0` to preprocessor definitions for SKULLBONEZ_CORE,
      SKULLBONEZ_MATHS, SKULLBONEZ_PHYSICS — all x64 configurations.
- [x] Keep SKULLBONEZ_TESTS and `Agentic/Tests/*` on `/EHsc` (doctest needs
      exceptions); record the boundary rule: engine TUs compiled under the
      test project inherit test flags, and no STL object with EH-dependent
      layout crosses a binary boundary.
- [x] Define `JSON_NOEXCEPTION` (or `JSON_THROW_USER=SB_FATAL` routing —
      decide and record; `JSON_NOEXCEPTION` + discard-checked parse is the
      default choice) wherever `ThirdPtySource/nlohmann/json.hpp` is compiled
      under an engine project.
- [x] Rework `TestSceneParserSchema.h` `ReadJsonFile`: replace `try/catch`
      with `Json::parse(input, nullptr, false)` and `is_discarded()`;
      malformed JSON stays a Lane R `Fail(path, message)` recoverable result.
- [x] Audit every engine-project nlohmann call site
      (`ShaderBytecodeManifest.cpp`, `InteractionAutomationController.cpp`,
      `ContactAudioService.cpp`, `DemoDirector.cpp`, `SceneSnapshotWriter.cpp`,
      `Init.cpp`, `ReplayV2Artifact.cpp`, `RunScene.cpp`,
      `SceneRuntimeCreate.cpp`, `EditorPlacementAssets.h`,
      `RunEditorTools.cpp`): under `JSON_NOEXCEPTION`, unchecked `.get<T>()`
      on a wrong type aborts — every read must flow through the checked
      `Require*`/`Read*` helpers or an `is_*()` guard first.
- [x] Check stdlib fallout under `_HAS_EXCEPTIONS=0` (e.g. `std::stof/stoi`
      usage, `std::filesystem` throwing overloads → error_code overloads).
- [x] Evidence: `dumpbin /headers /unwindinfo` (or equivalent) spot check on
      `Profile\SKULLBONEZ_CORE.exe` objects shows no EH unwind tables for
      engine TUs; a deliberately malformed scene file still loads-fails
      recoverably with owner/message.
- [x] `tools\validate_full.bat` pass recorded.

Decision and evidence (2026-07-13): engine projects use `/EHs-c-`,
`_HAS_EXCEPTIONS=0`, and Core uses `JSON_NOEXCEPTION`; doctest keeps `/EHsc`.
SKULLBONEZ_TESTS retains `/EHsc` for doctest language exceptions but also defines
`_HAS_EXCEPTIONS=0`, keeping the STL ABI identical to linked engine libraries;
engine sources compiled into that project inherit the same test flags. This was
proved by clean Release and Profile-WPO links after those configurations exposed
and closed an initial `std::bad_variant_access` C4743 mismatch. No STL object
whose layout or lifetime depends on the exception setting crosses a binary ABI
boundary. All JSON conversions were inspected and now have
an `is_*()`/checked-helper guard; filesystem directory probes use
`std::error_code`. `dumpbin /symbols` spot checks on the Profile
`InteractionAutomationController.obj`, `SceneRuntimeCreate.obj`, and
`TestSceneParser.obj` found no `__CxxFrameHandler`, `__CxxThrowException`,
handler-map, try-map, or FuncInfo symbols (ordinary x64 stack-unwind
`.pdata`/`.xdata` remains). The malformed-scene doctest passed with the
`Scene/TestSceneParser` owner and `Invalid JSON` message, alongside the wrong
member-type recoverable test. `tools\validate_full.bat` passed in 127.6s:
177/177 doctest cases and 4,059/4,059 assertions, all standalone CPU tests,
zero-warning Profile/Debug builds, zero DX12 validation errors with matching
screenshots, and the 44,401-line physics baseline byte-exact.

## R5 — Retire `SkullbonezCore::Basics`

- [x] Record the binding target map BEFORE editing (resolved 2026-07-13):
      - `Run`, `RunFrame`/timer/camera/debug state, launch options, frame
        loop shell → `SkullbonezCore::Runtime`
      - `Profiler`, `EngineConfig`, `Log`, result/fatal, and memory-stat
        service types currently in `Basics` → `SkullbonezCore::Core`
      - Types that already have a domain owner namespace stay put.
      Resolve collisions with the existing `Runtime::Audio` nesting and any
      `Core` name clashes; adjust the map here if needed.
      Resolved detail: all types defined under `SkullbonezSource/Runtime/`
      move to the existing top-level `SkullbonezCore::Runtime` namespace;
      its existing `Runtime::Audio` child remains unchanged and has no symbol
      collisions. `EngineConfig` and its value records, `SbError`/`SbResult`,
      `SbFatal`, `EngineLog`/`Log`, profiler types/macros, platform-profiler
      functions, and main-memory statistics move to
      `SkullbonezCore::Core`. `Environment::Timer`,
      `Threading::WorkerPool`, and all existing `Assets`, `Physics`,
      `Rendering`, `GameObjects`, `Scene`, `UI`, and `World` owners remain in
      their current namespaces; their former `Basics` blocks contain only
      forward declarations and are rebound to `Core` or `Runtime` by the
      declared type's owner. No compatibility namespace or alias will remain.
- [x] Mechanical rename across all 270 current files (the plan's 145-file
      evidence was stale after later runtime work): namespace blocks, `Basics::`
      qualifications, `using` declarations, forward declarations, friend
      decls, `.filters` untouched).
- [x] No compatibility alias namespace, no `namespace Basics = ...;` shim —
      atomic cutover only (migration-noun rule).
- [x] Prove: `rg -n 'namespace Basics|Basics::' SkullbonezSource` returns
      zero rows.
- [x] Zero warnings all configurations; no behavior change claimed anywhere.
- [x] `tools\validate_full.bat` pass recorded.

Evidence (2026-07-13): all 270 files carrying the old spelling moved atomically
to `Runtime`, `Core`, or their existing domain owner; no compatibility alias was
added and the acceptance grep returned zero rows. The comment-style touched-file
audit covered 279 source/test files: 276 existing learning headers were complete,
and the three standalone CPU test runners received their missing `Summary`
sections; no files were deferred. Profile (13.7s), Debug (31.6s), Release
(33.5s), and Profile-WPO (33.7s) built with zero warnings. The Release/WPO runs
also closed the R4 test/engine STL ABI flag gap described above.
`tools\validate_full.bat` passed in 162.8s: 177 doctests/4,059 assertions and all
standalone CPU tests passed, DX12 reported zero validation errors with matching
screenshots, and physics matched the 44,401-line baseline byte-exactly.

## R6 — Dissolve `Rendering/Helper.{h,cpp}`

- [x] Produce the routine inventory: every function/struct in `Helper.h` +
      `Helper.cpp` (1,424 lines) with its real state owner named.
      Resolved inventory (current files are 341 + 1,429 lines):
      - `RenderHelperContext` → `Rendering::PrimitiveRenderContext`, the typed
        per-call render/resource/config borrow passed by runtime render owners.
      - `RenderHelperState`, `RenderHelper`, `PrimitiveBatchKind`, and
        `PrimitiveBatchScope` → `Rendering::PrimitiveBatchRenderer`; they own the
        sphere/box/pine meshes, shaders, material table, convex-hull dynamic
        vertex scratch, clip plane, instance arrays, and exactly-once batch
        flush invariant.
      - Context/resource internals `Resources`, `Commands`, `AssetRegistry`,
        `Config`; payload records `PrimitiveBatchShaderConstants`,
        `InstancedShadowDepthConstants`, `PrimitiveBatchShaderParams`; and
        material/geometry helpers `BeginPrimitiveBatchTransparency`,
        `EndPrimitiveBatchTransparency`, `MaterialByte`,
        `EnsureMaterialTableTexture`, `AppendMaterialInstancePayload`,
        `BuildSingleMaterialInstancePayload`, `BuildSingleMatrixPayload`,
        `EnsureConvexHullDynamicVB`, `BuildConvexHullDynamicVertices`,
        `ApplySceneLightConstants`, `ApplySceneLightUniforms`,
        `ApplyBatchLightConstants`, `ObjectStyleForShader`,
        `ObjectStyleForMeshSelection`, `BindPrimitiveBatchShader` → private
        implementation details of `PrimitiveBatchRenderer`. Canonical CPU
        triangle emission remains in the pre-existing
        `Rendering::PrimitiveMeshes` owner (`PrimitiveMeshBuilder.h`).
      - Sphere owner operations: `EnsureSphereShader`, `EnsureSphereMesh`,
        `BuildSphereMesh`, `BuildLowPolySphereMesh`, sphere batch begin/model/end,
        and public `BeginSphereBatch` → `PrimitiveBatchRenderer`.
      - Box owner operations: `BuildBoxMesh`, box batch begin/model/end, public
        `BeginBoxBatch`, and convex-hull visible draw/upload →
        `PrimitiveBatchRenderer` because all use the same instance/material ABI.
      - Pine owner operations: `BuildPineMesh`, pine batch begin/model/end, and
        public `BeginPineBatch` → `PrimitiveBatchRenderer`.
      - Shadow-depth shader/resource prewarm, `FillShadowReceiverConstants`,
        sphere/box/pine shadow batch begin/model/end, convex-hull depth draw,
        and public shadow batch scopes → `PrimitiveBatchRenderer`; these submit
        builder-owned primitive geometry. Generic projection, bias, receiver,
        texel-snap, and frame-data math already remains in `Rendering/Shadow.h`
        and the `ShadowPass` owner rather than being duplicated here.
      - Resource lifetime operations (`BindRenderResourceFactory`, destructor,
        `ReleaseOwnedRenderResources`) → `PrimitiveBatchRenderer`.
      - DXR sphere handle/count accessors → one typed
        `PrimitiveMeshGeometryView` value published by `PrimitiveBatchRenderer`;
        `RuntimeRenderer` passes that value to the existing `IRenderRayTracing`
        owner, so no DXR backend reaches into builder state.
      - `SetClipPlane`/`GetClipPlane` remain builder-owned draw policy.
        Unused `StateSetup` and unused tint-only compatibility overloads are
        deleted rather than migrated.
- [x] Keep primitive/instance batch scope machinery with the cohesive
      `Rendering::PrimitiveBatchRenderer` owner. Canonical CPU mesh emission
      remains in `PrimitiveMeshes`; `RenderInstanceStore` remains the durable
      scene-instance store rather than absorbing transient draw batching.
- [x] Keep primitive shadow submission with `PrimitiveBatchRenderer`, whose
      meshes, instance scratch, and shader resources it consumes. Generic
      shadow projection/bias/frame math remains in `Shadow.h`/`ShadowPass`.
- [x] Replace raw DXR sphere accessors with `PrimitiveMeshGeometryView` and
      pass that value from `RuntimeRenderer` into the existing ray-tracing
      owner; no backend-specific bridge or new DX12 owner was needed.
- [x] Move runtime-facing context and frame lookup composition into
      `Runtime/Render/RuntimeRenderer.*` and `RuntimeRenderPasses.*`.
- [x] Anything left without a clear owner gets a named decision here — no new
      `*Util`/`*Common`/`*Misc` file is allowed to absorb it.
- [x] Delete `Helper.h`/`Helper.cpp`; update `.vcxproj` + `.filters` +
      includes; `tools\validate_project_filters` clean (664 project items,
      664 filter items, zero errors).
- [x] Learning-header/comment audit on all 10 touched destination source files:
      10 checked, zero deferred. Each retains the required File/Purpose/
      Mental-model/Glossary teaching surface and local ownership/lifetime/
      invariant comments where the renamed boundary is non-obvious.
- [x] `tools\validate_dx12_renderer.bat` passed in 48.5s: zero DX12 InfoQueue
      errors and all three screenshots matched committed baselines.
      `tools\run_graphics_stress.bat 1` completed in 61.8s with exit code 0;
      PID 24084 ran for the bounded minute and was stopped by the script's
      PID-scoped timeout without a crash. The final
      `tools\validate_fast.bat` also passed in 46.6s, and both allocation-policy
      self-test/repository scans passed
      after the renamed allowlist paths/patterns were reconciled.

## R7 — C++20 upgrade

- [x] `<LanguageStandard>stdcpp20</LanguageStandard>` in SKULLBONEZ_CORE,
      SKULLBONEZ_MATHS, SKULLBONEZ_PHYSICS, SKULLBONEZ_TESTS and
      `Agentic/Tests/*` projects — all 20 x64 configuration declarations.
- [x] Fix /W4 fallout to zero warnings (typical MSVC C++20 items: implicit
      `this` capture in lambdas, deprecated `u8` conversions, aggregate init
      changes, `std::result_of` remnants in third-party headers — wrap third
      party in the existing `#pragma warning(push,0)` pattern only).
      The only fallout was C++20's aggregate rule for the five non-copyable
      stack-only types in `RuntimeFrameViews.h`; explicit reference-binding
      constructors preserve their lifetime contract. Profile built in 15.6s;
      Debug/Release/Profile-WPO built in a combined 126.5s (Profile-WPO 47.8s),
      all with zero warnings. The standalone RuntimeInteractionPolicyTests
      Release configuration also built `/W4 /WX` clean in 7.8s.
- [x] Confirm doctest + nlohmann + stb compile clean under stdcpp20: doctest
      2.4.12, nlohmann/json 3.12.0, and stb_image 2.30 all compiled in the CPU
      umbrella/full gate. No third-party warning suppression was added.
- [x] Learning-header/comment audit for the sole touched source file,
      `RuntimeFrameViews.h`: 1 checked, zero deferred. Its existing teaching
      header remains complete and the C++20 constructor rationale is local.
- [x] Determinism proof: `tools\validate_physics.bat` byte-exact against the
      existing committed baseline (no refresh allowed in this task).
      The full gate matched `physics_regression_varied.csv` at 44,401 lines.
- [x] `tools\validate_full.bat` passed in 151.1s: all CPU lanes, Profile/Debug
      zero-warning builds, zero DX12 InfoQueue errors with all three screenshots
      inside baseline thresholds, and standalone plus regression physics clean.

## R8 — `std::span` at dense-store boundaries

- [x] Inventory pointer+count and `const std::vector<T>&` view parameters on:
      `PhysicsBodyStore`, `ColliderStore`, `PersistentContactSolverContext`,
      solver side-effect queues, sleep/island passes, broadphase candidate
      consumers, `RenderInstanceStore` submission, replay sample views.
- [x] Convert read seams to `std::span<const T>`, write seams to
      `std::span<T>`; storage ownership and capacities unchanged.
- [x] Guard rail: spans are frame-scoped borrows; no span stored as a member
      that outlives the store contents it views (comment the lifetime rule at
      each stored-view site if any must exist).
- [x] Prove no copies were introduced: the pre-change
      `tools\validate_perf.bat` passed in 61.6s and the post-change run passed
      in 62.2s. DX12 frame average/p99 improved 6.2%/23.0%; physics-bench frame
      average/p99 improved 9.7%/22.1%; all structural and budget comparisons
      passed. The sole upward comparison was the quantized physics-bench
      shadow-batch average, 0.0005ms to 0.0006ms (+0.0001ms), while its p99
      improved 25%.
- [x] `tools\validate_physics.bat` passed in 66.8s: Debug built with zero
      warnings, standalone smoke passed, and the 44,401-line varied-scene CSV
      remained byte-exact without a baseline refresh.
- [x] Comment-quality audit completed for all 52 touched source-bearing files
      using `Agentic/Skills/comment-style-audit/skill.md`: 52 checked, 0
      deferred, 0 unchecked. Learning headers remain complete and every stored
      span is a synchronous frame/context borrow with a local lifetime rule.

## R9 — Deterministic solver SIMD

- [x] Baseline profile FIRST: `tools\validate_perf.bat` + SkullScope solver
      phase numbers on the perf scene; record commands, artifact sizes, and
      model-ingested query bytes per the SkullScope reporting rule.
      The pre-edit perf gate passed in 62.2s with `SolveRows` averaging
      0.0157ms. Trace command:
      `Debug\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --shadows off
      --scene SkullbonezData/scenes/physics_bench_varied.scene.json
      --physics-diag Debug\r9_solver_before.physicsdiag.ndjson` (30.5s).
      The NDJSON is 100,811,064 bytes and its SQLite cache is 49,045,504
      bytes; neither raw artifact was read by GPT.
- [x] Bounded SkullScope query:
      `tools\physics_query.bat Debug\r9_solver_before.physicsdiag.ndjson
      solver --frames 0:1200 --limit 12`. Output read: 3,510 characters,
      3,513 UTF-8 bytes, not truncated. Total GPT-read SkullScope output is
      3,510 characters / 3,513 bytes. It reports 13,540 contact rows, 11.2833
      average rows, and 9.3725 average iterations across 1,200 frames.
- [x] Design note: SSE2 only, fixed-width, no FMA, no AVX, no runtime
      dispatch — the deterministic envelope is one code path on all supported
      x64 hardware (matches the documented MSVC v143 envelope).
- [x] Vectorize the PGS row solve (normal + two friction axes accumulate/
      clamp) and solver body scratch velocity updates in
      `PersistentContactSolver.cpp`; keep row ordering identical.
      The candidate preserved normal-before-friction and row order, built
      Profile with zero warnings in 14.7s, then was fully reverted because it
      was slower.
- [x] Rebuild final Debug artifacts; regenerate physics CSV + SkullScope
      baselines from that exact executable/scene/config state. No solver or
      SkullScope baseline changed after the revert. A first deep run exposed a
      separate stale `space_three_body_chaos.csv`: commit `28a1eee0` had raised
      the scene's gravity and velocities without updating its deep baseline.
      Refreshed that 361-line artifact from the final scalar Debug executable.
- [x] Rerun `tools\validate_physics.bat` AND `tools\validate_physics_deep.bat`
      against the refreshed baselines — byte-exact required. Physics passed in
      58.2s (44,401 varied-scene lines exact); the final deep gate passed in
      131.7s, including the repaired 361-line three-body artifact and the
      committed SkullScope query packet.
- [x] After-profile: record measured solver-phase delta. If below noise,
      revert the SIMD change, keep the measurement report, and close the task
      as "measured, declined" — that is an accepted closure.
      Measured, declined: candidate `SolveRows` rose from 0.0157ms to 0.0180ms
      average (+14.6%; compared tail +13.3%). The final scalar run measured
      0.0155ms average, so the candidate added complexity and regressed cost.
- [x] `tools\validate_perf.bat` pass recorded either way. Candidate gate passed
      in 66.4s; final retained-scalar gate passed in 61.4s with allocation,
      structural, and performance budgets clean.

## R10 — Bindless + three frames in flight

Stage A — frame headroom:

- [ ] Inventory every `FRAME_COUNT`-keyed array/allocator: command
      allocators, upload arenas, transient SRV rows, readback slots, fence
      values, per-frame descriptor rings, replay ribbon uploads.
- [ ] Raise `FRAME_COUNT` 2→3 in `RenderBackendDX12.h`; size every per-frame
      resource from the constant (no hardcoded 2s — grep proof).
- [ ] Re-audit upload-arena capacity policy (32 MiB/frame × 3) and the
      overflow policy landed by `upload-arena-overflow-policy`; record the
      memory delta decision.
- [ ] Danger Zone gate: `tools\validate_dx12_renderer.bat` passed 3
      consecutive times; bounded `tools\run_graphics_stress.bat 1` with flat
      memory artifacts recorded.

Stage B — SM6.6 bindless texture access:

- [ ] Device support decision recorded: minimum feature check
      (`HighestShaderModel >= 6_6` + `ResourceBindingTier` check) and the
      explicit fallback ruling (fail-fast with diagnostics vs retained
      table path) BEFORE implementation.
- [ ] Root signatures gain `CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED`; affected
      shaders read material/object textures via `ResourceDescriptorHeap[i]`
      with the index carried in existing per-instance payload rows.
- [ ] DXC bake targets SM6.6 for converted shaders; regenerate
      `ShaderBytecodeManifest` + `GeneratedShaderReflection.h` contracts and
      the committed `.dxil` artifacts in the same commit.
- [ ] Remove the per-draw descriptor-table copy path for converted draws;
      transient SRV ring remains only for genuinely dynamic views (record
      what stays and why).
- [ ] Zero DX12 InfoQueue errors; screenshot baselines match or are
      intentionally refreshed with evidence.
- [ ] `tools\validate_perf.bat` draw-loop delta recorded;
      `tools\run_graphics_stress.bat 1` command, runtime, exit evidence
      recorded.

## Closure

- [ ] Final `tools\validate_full.bat` pass on the aggregate round-3 tree.
- [ ] Single independent rubber-duck review of the whole round (required for
      the R5/R6 ownership moves); findings resolved or reopened as tasks.
- [ ] MASTER-PLAN ledger row updated to 10/10; this checklist and the owning
      plan deleted per inventory rule 4, with closure evidence under
      `Agentic/Reports/`.
