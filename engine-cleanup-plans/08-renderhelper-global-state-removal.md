# 08 — RenderHelper Global-State Removal

Date: 2026-07-08
Status: Complete
Priority: P2
Owner: Rendering
Source issue: audit iss-06 (severity 4)

## Problem

Every visible model is drawn through an all-static "utility" class that owns its
meshes, shaders, material texture, clip plane, staging vectors, and batch flags
as ambient process-global state — making it non-reentrant, untestable, not
instantiable twice, and dependent on manual reset ordering vs backend teardown.

Verified evidence:

- [`RenderHelper`](../SkullbonezSource/Rendering/Helper.h:84) is all-static: ~54
  `static` members in the header — `unique_ptr<IShader> sphereShader`,
  `sphereInstMesh`, `std::vector<float> sphereInstanceData`, file-scope
  `sMaterialTableTexture` / `sConvexHullDynamicVB`, `inline static float
  sClipPlane[4]`, and batch flags `sSphereBatchReady` / `sBoxBatchTransparent`.
- Batch flags are mutated across `DrawSphereBatchBegin/Model/End`; a missed
  `BatchEnd` leaves flags stale into the next frame.
- Use-after-free is prevented only by remembering to call
  `ResetRenderResources()` (called from `RunRender.cpp`) in the right order vs
  backend teardown.
- ~1 MB of mutable global scratch (`sConvexHullVertexData` ~630 KB and a sibling
  ~410 KB in `CollisionVisualizer.cpp` — the latter inconsistent with that
  class's own instance-member streams).

## Goal

`RenderHelper` becomes an instantiable object owned by the runtime render host,
with lifetime tied to its owner's construction/destruction, and per-batch state
scoped to a batch object rather than global flags.

## Approach

- [x] **Phase 0 — Gather statics** into a `RenderHelperState` struct owned by the
  render host.
- [x] **Phase 1 — Convert static methods to members**; thread the instance to
  call sites (or reach it through the render host).
- [x] **Phase 2 — Scope batches with RAII.** Replace `Begin/Model/End` global
  flags with a batch scope object whose destructor flushes — a missed end
  becomes impossible.
- [x] **Phase 3 — Own the lifetime.** Resource creation/destruction moves to the
  owner's ctor/dtor; delete `ResetRenderResources()` and its manual ordering.
- [x] **Phase 4 — Reconcile `CollisionVisualizer`** scratch to match its own
  instance-member pattern.

## Risks / GPU safety

Resource lifetime vs DX12 device teardown is a hazard zone — tying lifetime to a
clear owner is the point, but validate barriers/validation errors carefully.

## Step-by-step implementation

Do steps in order; validate and commit per step. **Screenshots must stay
unchanged and `dx12_validation.txt` == 0 throughout** — this is a hard gate.

- [x] **0.1** Enumerate every `static` member in `Helper.h` / `Helper.cpp`
  (~54): shaders, meshes, material texture, clip plane, staging vectors, batch
  flags, scratch buffers. No code change.

  Inventory note (2026-07-08): CodeGraph and `rg` found the mutable helper
  state to migrate:
  - Class-owned GPU/resource state in `Helper.h`: `sphereShader`,
    `shadowDepthShader`, `sphereInstMesh`, `sphereVertexCount`,
    `sphereInstanceData`, `lowPolySphereInstMesh`,
    `lowPolySphereVertexCount`, `activeSphereInstMesh`,
    `activeSphereVertexCount`, `boxInstMesh`, `boxVertexCount`,
    `boxInstanceData`, `pineInstMesh`, `pineVertexCount`, `pineInstanceData`,
    and `sClipPlane[4]`.
  - File-scope mutable state in `Helper.cpp`: `sSphereBatchTransparent`,
    `sBoxBatchTransparent`, `sPineBatchTransparent`, `sSphereBatchReady`,
    `sBoxBatchReady`, `sPineBatchReady`, `sMaterialTableTexture`,
    `sConvexHullDynamicVB`, and `sConvexHullVertexData`.
  - File-scope constants/helpers stay static: instance-layout constants,
    material-table constants, `Resources`/`Commands`/`AssetRegistry`/`Config`,
    material packing, shader binding, and light/shadow constant helpers. These
    are not owner state unless later slices need test seams.
  - Static method call sites are concentrated in `GameModelRenderer.cpp`,
    `RunRender.cpp`, `RunPasses.cpp`, and `RunScene.cpp`; raytracing uses
    `EnsureSphereMesh`, `GetSphereInstMeshHandle`, and `GetSphereVertexCount`.
  No repository validation required; documentation-only inventory.
- [x] **1.1** Create a `RenderHelperState` struct holding all of them as
  non-static members; give the render host one owned instance. Build. Commit.
- [x] **2.1** Convert static methods to members operating on that instance;
  thread the instance to call sites (in `RunRender.cpp`) via the render host. Do
  it in **one group at a time** (sphere batch, box batch, hull debug…). Gate:
  `validate_dx12_renderer`. Commit per group.

  Completion note (2026-07-08): `RuntimeRenderer` now owns a `RenderHelper`
  instance and publishes it through `RenderFrameContext` / `RenderHelperContext`.
  Sphere, box, pine, convex-hull, shadow-depth, replay ghost, reflection clip
  plane, terrain clip-plane, and DXR sphere-prewarm call sites now operate on
  that instance. No static mutable `RenderHelper` state or static helper call
  sites remain outside the member definitions in `Helper.cpp`. Added a runtime
  boundary guardrail/self-test blocking new `RenderHelper::` primitive access
  outside the member-definition file.
- [x] **3.1** Replace the `Begin/Model/End` global batch flags with a batch scope
  object (RAII) whose destructor flushes — a missed `End` becomes impossible.
  Gate: `validate_dx12_renderer`. Commit.

  Completion note (2026-07-08): `RenderHelper::PrimitiveBatchScope` is a
  move-only RAII scope that borrows `RenderHelperContext` until destruction and
  flushes visible or shadow primitive batches exactly once. `GameModelRenderer`
  and replay ghost rendering now use scoped sphere/box/pine batches, leaving the
  legacy Begin/Model/End methods private to `RenderHelper`. `tools\validate_dx12_renderer.bat`
  passed with DX12 InfoQueue errors = 0 and committed screenshot baselines
  unchanged.
- [x] **4.1** Tie resource creation/destruction to the owner's constructor/
  destructor; delete `ResetRenderResources()` and its manual calls in
  `RunRender.cpp`. Gate: `validate_dx12_renderer` (`dx12_validation.txt` == 0).
  Commit.

  Completion note (2026-07-08): `RuntimeRenderer` now owns `RenderHelper` as a
  backend-lifetime optional object. Backend release destroys the helper, backend
  rebuild constructs a fresh helper with the active resource factory, and
  `RenderHelper` releases its own mesh/texture/dynamic-VB handles from its
  destructor. The public `ResetRenderResources()` helper API and manual helper
  reset calls in `RunRender.cpp` are gone. `tools\validate_dx12_renderer.bat`
  passed with DX12 InfoQueue errors = 0 and committed screenshot baselines
  unchanged.
- [x] **5.1** Reconcile `CollisionVisualizer`'s ~410 KB global scratch to match
  its own instance-member stream pattern. Gate: `validate_dx12_renderer`. Commit.

  Completion note (2026-07-08): `CollisionVisualizer` now owns its convex-hull
  debug vertex scratch as `m_hullDebugVertexData`, beside its existing sphere and
  box instance staging buffers. The file-scope mutable `sHullDebugVertexData`
  was deleted, and a compile-time assertion keeps the fixed scratch capacity in
  sync with `ConvexHullShape`. `tools\validate_dx12_renderer.bat` passed with
  DX12 InfoQueue errors = 0 and committed screenshot baselines unchanged.

## Validation

`tools\validate_dx12_renderer.bat` (screenshot diff + `dx12_validation.txt` ==
0). Screenshots must be unchanged.

## Acceptance (structural)

- [x] `RenderHelper` has no static mutable members; it is instantiable and owned.
- [x] Batch state is scoped (RAII); no global batch-ready flags persist across
  frames.
- [x] `ResetRenderResources()` manual-ordering call is removed.
- [x] DX12 screenshots unchanged; `dx12_validation.txt` == 0.
