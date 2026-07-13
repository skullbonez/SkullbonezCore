# Adversarial Review Remediation Round 3 — Language, Namespace, And Renderer Modernization

Date: 2026-07-13
Status: Live — 3/10 tasks complete
Impact area: build configuration, Core prelude, UI/Scene/Rendering headers,
namespace layout, math/solver hot path, DX12 backend
Owner: engine architecture

Companion execution checklist (fine-grained, does not add a second ledger
denominator): `Agentic/Plans/TODO/adversarial-review-round-3-progress.md`.

## Problem And Evidence

Source: 2026-07-12 independent adversarial source review of the full
`SkullbonezSource` tree at the `nightrunner-11th-july` tip, re-verified
2026-07-13 against the `nightrunner-12th-july` tip. Findings the owner ruled
in-scope, with current file:line evidence:

1. **Exceptions are banned by policy but enabled by the compiler.** No engine
   project sets `<ExceptionHandling>` or `_HAS_EXCEPTIONS=0`, so `/EHsc`
   unwind machinery is compiled into a runtime whose live `throw` inventory is
   zero. The only `try/catch` is the nlohmann parse boundary in
   `SkullbonezSource/Scene/TestSceneParserSchema.h:798-809`.
2. **Header-scope `using namespace` directives leak into every includer.**
   `SkullbonezSource/UI/UIFrameComposition.h:63-70` (eight directives),
   `SkullbonezSource/Scene/TestSceneParserSchema.h:1040`, and
   `SkullbonezSource/Rendering/ShaderReflectionContracts.h:217`.
3. **Global-namespace capacity constants with stale ownership comments.**
   Before R2, the now-retired `SkullbonezSource/GameObjects/SceneCapacity.h`
   defined `MAX_GAME_MODELS`,
   `TOTAL_CAMERA_COUNT`, `TOTAL_TEXTURE_COUNT`, `DEFAULT_GAME_MODEL_CAPACITY`,
   and `DEFAULT_GAME_MODELS` in the global namespace; its Related block claims
   `Core/Common.h` still includes it (it does not) and points at
   `SkullbonezSource/GameObjects/SceneController.h`, which does not exist.
   `GameObjects/` contains only this one 38-line header.
4. **`Core/Common.h` injects `<windows.h>` into 43 headers** (`Common.h:50`),
   leaking the platform's largest header transitively through physics, UI, and
   scene code that never touches Win32.
5. **`SkullbonezCore::Basics` is a 2005 fossil namespace** spanning 145 files
   and containing the composition root (`Run`), profiler, config, and frame
   state — a utils-bucket name for the most important code in the engine.
6. **`Rendering/Helper.{h,cpp}` (1,424 lines) is a self-confessed legacy
   grab-bag** ("Collects legacy helper routines that bridge engine
   subsystems", `Helper.h:4`) holding cross-subsystem render bridging that
   belongs to named owners.
7. **C++17 blocks `std::span`** (`<LanguageStandard>stdcpp17` in every
   project); dense-store views are passed as pointer+count or
   `const std::vector<T>&` today.
8. **The solver inner loop is scalar.** SSE appears in exactly three places
   (`Maths/Matrix4.cpp:490`, `Maths/RotationMatrix.h:77`,
   `Physics/BoundingSphere.cpp:190`); `PersistentContactSolver` row solve and
   integration are scalar float math.
9. **The DX12 backend binds per-draw descriptor tables with two frames in
   flight.** `RenderBackendDX12.h` pins `FRAME_COUNT = 2` and a 2,048/frame
   transient SRV table; no SM6.6 bindless (`ResourceDescriptorHeap`) path and
   no third frame of CPU/GPU latency headroom.

Owner rulings recorded 2026-07-12/13: replay right-sizing, unit-test depth,
sleep parallel-array consolidation, and `Init.cpp` size are explicitly out of
scope. Broader `RenderBackendDX12` decomposition is paused pending owner
feedback; only task R10 below touches its architecture. The `/fp` pinning
finding was already closed by `determinism-contract-hardening` (2026-07-12).

## Goal

Close every in-scope round-3 finding: an exception-free build contract that is
compiler-enforced, clean header namespace hygiene, owner-named namespaces and
constants, a platform prelude that stays in platform code, C++20 with
`std::span` at dense-store boundaries, a deterministic SIMD solver inner loop,
and a bindless, three-frames-in-flight DX12 backend.

## Non-Goals

- No replay subsystem restructuring and no new unit-test-depth campaign.
- No sleep working-set struct consolidation and no `Init.cpp` decomposition.
- No `RenderBackendDX12` interface re-partitioning beyond what R10 requires.
- No behavior changes hidden inside mechanical renames; tasks that can change
  physics-visible float results must say so and refresh baselines explicitly.

## Tasks

Ordered for minimal rework: header hygiene first (cheap, unblocks clean
renames), then the two broad mechanical passes, then language/feature
upgrades, then the two performance/architecture tasks.

- [x] **R1 — Remove header-scope `using namespace` directives.** Delete the
  eight directives in `UI/UIFrameComposition.h:63-70`, the detail-namespace
  export in `Scene/TestSceneParserSchema.h:1040`, and the directive in
  `Rendering/ShaderReflectionContracts.h:217`. Qualify names explicitly or use
  targeted `using`-declarations/aliases scoped inside functions or classes.
  Acceptance: `rg -n '^\s*using namespace' --glob '*.h' SkullbonezSource`
  returns zero rows; zero warnings. Validation: `tools\validate_fast.bat`
  passed on 2026-07-13 in 53.7s: formatting, project filters, 177 doctest
  cases/4,059 assertions, and zero-warning Profile/Debug builds were clean.
- [x] **R2 — Give scene capacity constants an owner.** Move the
  `SceneCapacity.h` constants into a named namespace (owner decision: they
  must remain `constexpr`, not `extern const`, because they size fixed arrays
  and appear in constant expressions). Correct the stale Related references,
  update every consumer's qualification, and either retire the `GameObjects/`
  directory by relocating the header next to its real consumers
  (`Runtime/Scene/`) or record why the directory stays. Acceptance: no
  engine-owned identifiers remain in the global namespace; stale references
  gone. Validation passed on 2026-07-13: `tools\validate_fast.bat` (73.7s),
  `tools\validate_physics.bat` (50.8s), `tools\validate_full.bat` (122.7s),
  `tools\validate_dx12_renderer.bat` (49.4s), and
  `tools\run_graphics_stress.bat 1` (61.8s, exit 0). Allocation-policy
  self-test/repo scan and project-filter validation also passed.
- [x] **R3 — Stop `Common.h` injecting `<windows.h>`.** Remove
  `<windows.h>`/`WIN32_LEAN_AND_MEAN` from `Core/Common.h`. Audit all 43
  including headers; the consumers that genuinely need Win32 types (window,
  timer, worker pool, DX12 device, input) include a narrow platform prelude
  (`Core/PlatformWin32.h` with `WIN32_LEAN_AND_MEAN` + `<windows.h>`) or
  `<windows.h>` directly in their `.cpp`. Physics, maths, UI-layout, and scene
  headers must compile without Win32. Acceptance: `Common.h` has no platform
  include; a grep inventory shows `windows.h` only in platform-owning files;
  zero warnings in all configurations. Validation passed on 2026-07-13:
  Profile/Debug/Release builds were zero-warning; `tools\validate_fast.bat`
  (53.7s), `tools\validate_full.bat` (132.7s),
  `tools\validate_dx12_renderer.bat` (48.9s), and
  `tools\run_graphics_stress.bat 1` (62.2s, exit 0) passed.
- [x] **R4 — Compile out exceptions in engine projects.** Set
  `<ExceptionHandling>false</ExceptionHandling>` (`/EHs-c-`) and define
  `_HAS_EXCEPTIONS=0` for SKULLBONEZ_CORE, SKULLBONEZ_MATHS, and
  SKULLBONEZ_PHYSICS in all configurations. Switch nlohmann to
  `JSON_NOEXCEPTION` (or `JSON_THROW_USER` routed to `SB_FATAL`) and rework
  `ReadJsonFile` in `TestSceneParserSchema.h` to
  `Json::parse(input, nullptr, /*allow_exceptions=*/false)` +
  `is_discarded()` so the last `try/catch` disappears; malformed JSON stays a
  Lane R recoverable failure. Audit type-error paths (`.get<T>()` on wrong
  types terminates under `JSON_NOEXCEPTION`) — every read must go through the
  existing `Require*`/checked helpers first. Decide and record the test-binary
  story: SKULLBONEZ_TESTS keeps `/EHsc` (doctest requires exceptions) and
  compiles engine sources under its own flags; document that mixed-flag STL
  objects must not cross the test boundary by ABI. Acceptance: engine binaries
  contain no EH unwind tables (`dumpbin /unwindinfo` spot check or equivalent
  evidence); malformed scene JSON still fails recoverably with owner/message;
  zero warnings. Completed 2026-07-13 with `/EHs-c-`, `_HAS_EXCEPTIONS=0`,
  `JSON_NOEXCEPTION`, guarded JSON reads, and nonthrowing filesystem probes.
  Profile object spot checks found no C++ EH handler/FuncInfo symbols; ordinary
  x64 stack-unwind metadata remains. The malformed-scene owner/message doctest
  passed. SKULLBONEZ_TESTS keeps `/EHsc` for doctest but defines
  `_HAS_EXCEPTIONS=0` so its STL ABI matches linked engine libraries; Release
  and Profile-WPO links prove the boundary. `tools\validate_full.bat` passed in 127.6s with 177 tests/4,059
  assertions, all CPU lanes, zero-warning builds, zero DX12 errors and matching
  screenshots, plus the 44,401-line physics baseline byte-exact.
- [x] **R5 — Retire the `SkullbonezCore::Basics` namespace.** Decide the
  target mapping first and record it in the companion checklist before any
  edit; the working proposal is: frame loop/shell types
  (`Run`, `RunFrame` state, launch options) → `SkullbonezCore::Runtime`;
  engine services (`Profiler`, `Timer`, `EngineConfig`, `Log`) →
  `SkullbonezCore::Core`; anything already owner-named keeps its owner. One
  atomic mechanical rename across all current consumers — no compatibility alias
  namespace, no transitional `using` shims (migration-noun rule applies).
  Acceptance: `rg -n 'namespace Basics|Basics::' SkullbonezSource` returns
  zero rows; zero warnings; no behavior change. Completed across 270 files on
  2026-07-13; the dated 145-file estimate was stale. All four configurations
  built with zero warnings. A 279-file touched-source comment audit found no
  deferrals. `tools\validate_full.bat` passed in 162.8s with all CPU lanes,
  zero DX12 errors and matching screenshots, plus the 44,401-line physics
  baseline byte-exact.
- [ ] **R6 — Dissolve `Rendering/Helper.{h,cpp}` into named owners.**
  Inventory every routine and struct in the file, assign each to its real
  owner (candidates: primitive/instance batching → `PrimitiveMeshBuilder` /
  `RenderInstanceStore`; shadow math → `Shadow.h` owner; DXR instance/upload
  bridging → the DXR owners under `Rendering/DX12/`; anything runtime-facing →
  `Runtime/Render/` owners), and delete the Helper files. No routine may land
  in a new `*Util`/`*Common`/`*Misc` bag. Acceptance: `Helper.h`/`Helper.cpp`
  deleted; each moved routine sits with the owner whose state it reads; zero
  warnings. Validation: `tools\validate_dx12_renderer.bat` then
  `tools\run_graphics_stress.bat 1` (render source moved), recorded with
  command output.
- [ ] **R7 — Upgrade the toolchain contract to C++20.** Set
  `<LanguageStandard>stdcpp20</LanguageStandard>` across all projects and
  configurations (engine + tests + standalone CPU test projects). Fix all
  /W4 fallout to zero warnings; no feature adoption in this task beyond what
  compiling cleanly requires. Confirm the determinism envelope is unaffected:
  same `/fp:precise`, same instruction generation for physics TUs (byte-exact
  baseline is the proof). Acceptance: all configurations build with zero
  warnings under stdcpp20; physics baseline byte-exact. Validation:
  `tools\validate_full.bat`.
- [ ] **R8 — Adopt `std::span` at dense-store boundaries.** Replace
  pointer+count and `const std::vector<T>&` view parameters with
  `std::span<T>`/`std::span<const T>` on the read/write seams of
  `PhysicsBodyStore`, `ColliderStore`, solver contexts
  (`PersistentContactSolverContext`, side-effect queues), broadphase
  candidate-pair consumers, and `RenderInstanceStore` submission views. Views
  only — no ownership or lifetime changes; spans must not outlive the frame
  scope of the store they view. Acceptance: hot-path signatures take spans;
  no added copies (perf gate holds); physics baseline byte-exact. Validation:
  `tools\validate_physics.bat` + `tools\validate_perf.bat`.
- [ ] **R9 — Deterministic SIMD in the solver inner loop.** Vectorize the
  `PersistentContactSolver` row solve (normal + two friction axes) and body
  velocity scratch updates with fixed-width SSE2 intrinsics — no runtime
  dispatch, no FMA/AVX variants, so the Windows x64 MSVC v143 determinism
  envelope holds on every supported machine. Hazard: lane reassociation is a
  physics-visible float change; this task must (a) profile first with
  `tools\validate_perf.bat` + SkullScope evidence, (b) implement, (c) refresh
  the physics CSV/SkullScope baselines from the final Debug artifacts, and
  (d) rerun `tools\validate_physics.bat` and `tools\validate_physics_deep.bat`
  to re-prove byte-exactness against the new baselines. If measured solver
  gain is below noise, record the measurement and revert the complexity —
  keeping scalar is an acceptable closure. Acceptance: measured solver-phase
  improvement recorded (or documented revert), deterministic baselines green.
  Validation: `tools\validate_physics.bat`, `tools\validate_physics_deep.bat`,
  `tools\validate_perf.bat`.
- [ ] **R10 — Bindless textures and three frames in flight.** Two staged
  changes to the DX12 backend, each independently gated. Stage A: raise
  `FRAME_COUNT` 2→3; audit every per-frame array, upload arena, transient SRV
  allocator, readback slot, and fence assumption keyed on frame index; verify
  no memory-growth regression with bounded stress. Stage B: adopt SM6.6
  dynamic resources (`ResourceDescriptorHeap` indexing) for material/object
  texture reads so per-draw descriptor-table copies disappear; requires shader
  model bump in the DXC bake (`tools\bake` manifest), root-signature
  `CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED` flag, device support check with a
  recorded fallback decision for pre-SM6.6 hardware, and reflection-contract
  regeneration. Acceptance: zero DX12 InfoQueue errors, screenshot baselines
  match (or are intentionally refreshed with evidence), stress run shows flat
  memory. Validation: `tools\validate_dx12_renderer.bat`, then
  `tools\run_graphics_stress.bat 1` (Danger Zone: upload buffer/frame
  allocator changes also require 3 consecutive `validate_dx12_renderer`
  passes), `tools\validate_perf.bat` for the draw-loop delta.

## Dependencies And Decisions

- R1 → R5: header hygiene lands before the namespace rename so the rename
  diff stays purely mechanical.
- R7 → R8: `std::span` requires the C++20 task to be complete.
- R9 and R10 are independent of each other but come last; both carry baseline
  or perf-evidence obligations that should not interleave with mechanical
  renames.
- R4 records an owner-approved compatibility decision for test binaries
  (doctest keeps exceptions); this is a build-flag boundary, not a runtime
  compatibility shim.
- R9 is the only task allowed to change physics-visible behavior, and only
  via the documented baseline-refresh procedure.
- R10 Stage B needs a device-support decision (SM6.6 minimum vs fallback);
  record it in the companion checklist before implementation.

## Validation

Per-task gates are named in each task above and deferred to PR-bound commits
per the repository rule. The final round-3 closure requires one full
`tools\validate_full.bat` pass plus the DX12 stress evidence from R6/R10 and
the refreshed physics baselines from R9. Every DX12-touching task records its
bounded `tools\run_graphics_stress.bat 1` command, measured runtime, and exit
evidence.

## Definition Of Done

- Engine projects compile with exceptions off and contain no EH unwind
  metadata; JSON failures remain Lane R recoverable.
- No header-scope `using namespace` and no engine identifiers in the global
  namespace.
- `Common.h` is platform-free; `windows.h` lives only in platform owners.
- `SkullbonezCore::Basics` and `Rendering/Helper.*` no longer exist; their
  contents live with named owners.
- All projects build C++20 at /W4 zero-warning; dense-store seams use
  `std::span`.
- Solver SIMD decision is closed with measurements either way; baselines are
  byte-exact against the committed artifacts.
- DX12 runs bindless material/object texture access with three frames in
  flight, zero InfoQueue errors, and flat stress memory.
- A single independent rubber-duck review at the end of the whole plan (per
  the migration-cleanup review rule), not one per task.
