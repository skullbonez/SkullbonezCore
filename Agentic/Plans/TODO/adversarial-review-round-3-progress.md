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

- [ ] Inventory every name each directive currently resolves in
      `UI/UIFrameComposition.h` (build once with directives commented out and
      collect the error list as the work queue).
- [ ] `UI/UIFrameComposition.h:63-70`: delete all eight directives; qualify
      names or add class/function-scoped `using`-declarations only.
- [ ] `Scene/TestSceneParserSchema.h:1040`: stop exporting
      `TestSceneParserDetail` into includers; qualify call sites inside the
      header instead.
- [ ] `Rendering/ShaderReflectionContracts.h:217`: replace the directive with
      explicit `UnifiedRasterRootSignature::` qualification (or keep it only
      if it is provably function-scoped and bounded — record the decision).
- [ ] Prove clean: `rg -n '^\s*using namespace' --glob '*.h' SkullbonezSource`
      returns zero rows.
- [ ] Zero-warning Profile build evidence captured for the PR gate.

## R2 — Scene capacity constant ownership

- [ ] Choose and record the namespace (proposal:
      `SkullbonezCore::Scene::Capacity` or the R5 target namespace — decide
      once, consistent with R5 mapping).
- [ ] Wrap `TOTAL_CAMERA_COUNT`, `TOTAL_TEXTURE_COUNT`,
      `DEFAULT_GAME_MODEL_CAPACITY`, `MAX_GAME_MODELS`, `DEFAULT_GAME_MODELS`
      in that namespace; keep `constexpr` (they size fixed arrays — an
      `extern const` would break constant expressions; decision recorded).
- [ ] Update every consumer qualification (`rg -l 'MAX_GAME_MODELS|TOTAL_CAMERA_COUNT|TOTAL_TEXTURE_COUNT|DEFAULT_GAME_MODEL'`).
- [ ] Fix the stale Related block: remove the `Core/Common.h` aliasing claim;
      point at the real `Runtime/Scene/SceneController.h`.
- [ ] Decide `GameObjects/` directory fate: relocate `SceneCapacity.h` to
      `Runtime/Scene/` (and update `.vcxproj` + `.filters`) or record why the
      directory remains; no orphan folder left behind.
- [ ] `tools\validate_project_filters` clean after any file move.

## R3 — `Common.h` platform prelude removal

- [ ] Inventory which of the 43 `Common.h`-including headers actually use
      Win32 types (HWND, DWORD, LARGE_INTEGER, CRITICAL_SECTION, …) versus
      only C/C++ stdlib.
- [ ] Add `Core/PlatformWin32.h` owning `WIN32_LEAN_AND_MEAN` + `<windows.h>`
      (+ `NOMINMAX` decision — record it; current code uses `(std::max)`
      parenthesis-guards, so `NOMINMAX` is the cleaner end state).
- [ ] Remove `<windows.h>` and `WIN32_LEAN_AND_MEAN` from `Core/Common.h`;
      keep the CRT debug block and stdlib includes, or shrink further if
      consumers allow.
- [ ] Point true platform consumers (Window, Timer, WorkerPool, Input, DX12
      device/backend, Init/Run message pump, capture/file IO) at the new
      prelude or at `<windows.h>` in their `.cpp`.
- [ ] Compile-fix the fallout headers with narrow stdlib includes only —
      physics/maths/UI-layout/scene headers must not gain a platform include.
- [ ] Prove: `rg -ln 'windows.h' SkullbonezSource` lists only platform-owner
      files; zero warnings in Debug, Profile, Release.

## R4 — Compile out engine exceptions

- [ ] Set `<ExceptionHandling>false</ExceptionHandling>` and add
      `_HAS_EXCEPTIONS=0` to preprocessor definitions for SKULLBONEZ_CORE,
      SKULLBONEZ_MATHS, SKULLBONEZ_PHYSICS — all x64 configurations.
- [ ] Keep SKULLBONEZ_TESTS and `Agentic/Tests/*` on `/EHsc` (doctest needs
      exceptions); record the boundary rule: engine TUs compiled under the
      test project inherit test flags, and no STL object with EH-dependent
      layout crosses a binary boundary.
- [ ] Define `JSON_NOEXCEPTION` (or `JSON_THROW_USER=SB_FATAL` routing —
      decide and record; `JSON_NOEXCEPTION` + discard-checked parse is the
      default choice) wherever `ThirdPtySource/nlohmann/json.hpp` is compiled
      under an engine project.
- [ ] Rework `TestSceneParserSchema.h` `ReadJsonFile`: replace `try/catch`
      with `Json::parse(input, nullptr, false)` and `is_discarded()`;
      malformed JSON stays a Lane R `Fail(path, message)` recoverable result.
- [ ] Audit every engine-project nlohmann call site
      (`ShaderBytecodeManifest.cpp`, `InteractionAutomationController.cpp`,
      `ContactAudioService.cpp`, `DemoDirector.cpp`, `SceneSnapshotWriter.cpp`,
      `Init.cpp`, `ReplayV2Artifact.cpp`, `RunScene.cpp`,
      `SceneRuntimeCreate.cpp`, `EditorPlacementAssets.h`,
      `RunEditorTools.cpp`): under `JSON_NOEXCEPTION`, unchecked `.get<T>()`
      on a wrong type aborts — every read must flow through the checked
      `Require*`/`Read*` helpers or an `is_*()` guard first.
- [ ] Check stdlib fallout under `_HAS_EXCEPTIONS=0` (e.g. `std::stof/stoi`
      usage, `std::filesystem` throwing overloads → error_code overloads).
- [ ] Evidence: `dumpbin /headers /unwindinfo` (or equivalent) spot check on
      `Profile\SKULLBONEZ_CORE.exe` objects shows no EH unwind tables for
      engine TUs; a deliberately malformed scene file still loads-fails
      recoverably with owner/message.
- [ ] `tools\validate_full.bat` pass recorded.

## R5 — Retire `SkullbonezCore::Basics`

- [ ] Record the binding target map BEFORE editing (working proposal):
      - `Run`, `RunFrame`/timer/camera/debug state, launch options, frame
        loop shell → `SkullbonezCore::Runtime`
      - `Profiler`, `Timer`, `EngineConfig`, `Log`, `WorkerPool`-adjacent
        service types currently in `Basics` → `SkullbonezCore::Core`
      - Types that already have a domain owner namespace stay put.
      Resolve collisions with the existing `Runtime::Audio` nesting and any
      `Core` name clashes; adjust the map here if needed.
- [ ] Mechanical rename across all 145 files (namespace blocks, `Basics::`
      qualifications, `using` declarations, forward declarations, friend
      decls, `.filters` untouched).
- [ ] No compatibility alias namespace, no `namespace Basics = ...;` shim —
      atomic cutover only (migration-noun rule).
- [ ] Prove: `rg -n 'namespace Basics|Basics::' SkullbonezSource` returns
      zero rows.
- [ ] Zero warnings all configurations; no behavior change claimed anywhere.
- [ ] `tools\validate_full.bat` pass recorded.

## R6 — Dissolve `Rendering/Helper.{h,cpp}`

- [ ] Produce the routine inventory: every function/struct in `Helper.h` +
      `Helper.cpp` (1,424 lines) with its real state owner named.
- [ ] Move primitive/instance batch scope machinery → `PrimitiveMeshBuilder`
      / `RenderInstanceStore` (or the runtime render pass owner that flushes
      it).
- [ ] Move shadow-related helpers → the `Shadow.h` owner.
- [ ] Move DXR instance/upload bridging → `Rendering/DX12/` DXR owners
      (BLAS/TLAS/SBT files).
- [ ] Move runtime-facing composition helpers → `Runtime/Render/` owners.
- [ ] Anything left without a clear owner gets a named decision here — no new
      `*Util`/`*Common`/`*Misc` file is allowed to absorb it.
- [ ] Delete `Helper.h`/`Helper.cpp`; update `.vcxproj` + `.filters` +
      includes; `tools\validate_project_filters` clean.
- [ ] Learning-header/comment audit on every touched destination file.
- [ ] `tools\validate_dx12_renderer.bat` pass + `tools\run_graphics_stress.bat 1`
      command, measured runtime, and exit evidence recorded.

## R7 — C++20 upgrade

- [ ] `<LanguageStandard>stdcpp20</LanguageStandard>` in SKULLBONEZ_CORE,
      SKULLBONEZ_MATHS, SKULLBONEZ_PHYSICS, SKULLBONEZ_TESTS and
      `Agentic/Tests/*` projects — every x64 configuration.
- [ ] Fix /W4 fallout to zero warnings (typical MSVC C++20 items: implicit
      `this` capture in lambdas, deprecated `u8` conversions, aggregate init
      changes, `std::result_of` remnants in third-party headers — wrap third
      party in the existing `#pragma warning(push,0)` pattern only).
- [ ] Confirm doctest + nlohmann + stb compile clean under stdcpp20 (all are
      C++20-compatible at current vendored versions; record versions).
- [ ] Determinism proof: `tools\validate_physics.bat` byte-exact against the
      existing committed baseline (no refresh allowed in this task).
- [ ] `tools\validate_full.bat` pass recorded.

## R8 — `std::span` at dense-store boundaries

- [ ] Inventory pointer+count and `const std::vector<T>&` view parameters on:
      `PhysicsBodyStore`, `ColliderStore`, `PersistentContactSolverContext`,
      solver side-effect queues, sleep/island passes, broadphase candidate
      consumers, `RenderInstanceStore` submission, replay sample views.
- [ ] Convert read seams to `std::span<const T>`, write seams to
      `std::span<T>`; storage ownership and capacities unchanged.
- [ ] Guard rail: spans are frame-scoped borrows; no span stored as a member
      that outlives the store contents it views (comment the lifetime rule at
      each stored-view site if any must exist).
- [ ] Prove no copies were introduced: `tools\validate_perf.bat` within noise
      of the pre-change run (record both numbers).
- [ ] `tools\validate_physics.bat` byte-exact (no refresh allowed).

## R9 — Deterministic solver SIMD

- [ ] Baseline profile FIRST: `tools\validate_perf.bat` + SkullScope solver
      phase numbers on the perf scene; record commands, artifact sizes, and
      model-ingested query bytes per the SkullScope reporting rule.
- [ ] Design note: SSE2 only, fixed-width, no FMA, no AVX, no runtime
      dispatch — the deterministic envelope is one code path on all supported
      x64 hardware (matches the documented MSVC v143 envelope).
- [ ] Vectorize the PGS row solve (normal + two friction axes accumulate/
      clamp) and solver body scratch velocity updates in
      `PersistentContactSolver.cpp`; keep row ordering identical.
- [ ] Rebuild final Debug artifacts; regenerate physics CSV + SkullScope
      baselines from that exact executable/scene/config state.
- [ ] Rerun `tools\validate_physics.bat` AND `tools\validate_physics_deep.bat`
      against the refreshed baselines — byte-exact required.
- [ ] After-profile: record measured solver-phase delta. If below noise,
      revert the SIMD change, keep the measurement report, and close the task
      as "measured, declined" — that is an accepted closure.
- [ ] `tools\validate_perf.bat` pass recorded either way.

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
