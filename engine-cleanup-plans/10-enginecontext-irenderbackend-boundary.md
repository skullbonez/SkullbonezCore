# 10 — EngineContext / IRenderBackend Boundary

Date: 2026-07-08
Status: In Progress
Priority: P2
Owner: Runtime / Rendering
Source issue: audit iss-08 (severity 3)

> Overlaps [`13-facade-retirement.md`](13-facade-retirement.md)
> (FAC-001, FAC-002, FAC-007). That plan owns the rule ("graduate-or-delete; a
> rename is not done"); this plan is the execution detail. **This plan also
> executes FAC-004 (SimulationController)** — see the extra step below — since no
> other plan covers it.

## Problem

Two "temporary/migration" bridges were started broadly and never converged,
leaving dead abstractions and dual ownership that hide the real dependency graph.

Verified evidence:

- [`EngineContext`](../SkullbonezSource/Runtime/EngineContext.h) is documented as
  the boundary for subsystem access, but its sole accessor
  `EngineServices Services()` has **no callers** (verified: only its
  declaration/definition and asserts). The runtime reaches subsystem fields
  directly, and `Bind()` is a one-time null-check dressed as architecture.
  `EngineContextBindings` is a 13-pointer bag; `EngineServices` adds ~7 more.
- [`IRenderBackend`](../SkullbonezSource/Rendering/IRenderBackend.h) is a
  self-described "Compatibility aggregate" that multiply-inherits five capability
  interfaces and declares no methods of its own — the narrow interfaces
  (`IRenderDeviceLifecycle`, `IRenderResourceFactory`, `IRenderCommandContext`,
  `IRenderDiagnostics`, `IRenderCaptureBackend`, `IRenderRayTracing`) already
  exist.
- `RenderBackendDX12` caches raw borrowed aliases
  (`m_device`/`m_swapChain`/`m_commandList`) duplicating pointers
  `Dx12RenderDevice` owns, so device recreation can silently dangle them.

> Note: the audit also alleged a global `IRenderBackend& Gfx()`. That specific
> accessor was **not** found in `IRenderBackend.h` during verification — treat it
> as unconfirmed and check before acting on it.

## Goal

Delete the dead `EngineContext` boundary (or make it real), route render callers
to the narrow interfaces and delete the aggregate type, and fix the DX12
borrowed-alias dual ownership.

## Approach

- [x] **Phase 0 — Resolve `Services()`.** Confirm it is unused → delete it, or
  wire it as the actual boundary. Do not keep decorative architecture.
- [ ] **Phase 1 — Split `EngineContextBindings`** into owner-specific records
  (per facade FAC-002); no subsystem receives the whole runtime graph.
- [ ] **Phase 2 — Route to narrow interfaces** and delete the `IRenderBackend`
  aggregate *type* (per facade FAC-001).
- [ ] **Phase 3 — Fix dual ownership.** `RenderBackendDX12` either borrows the
  device once through a single owner or refreshes its aliases on device
  recreation — no dangling on recreate.

## Risks

- Device-recreation paths are subtle; the alias fix is the highest-risk step.
  Test device-lost / resize paths explicitly.

## Step-by-step implementation

Do steps in order; validate and commit per step. DX12 alias work (Phase 3) is a
danger zone — run the renderer gate 3×.

### Phase 0 — Resolve `Services()`

- [x] **0.1** `rg -n "\.Services\(\)|->Services\(\)" SkullbonezSource` excluding
  `EngineContext.*`. If there are **no** real callers, delete
  `EngineServices Services()` and the unused `EngineServices` plumbing; build;
  commit. If there **are** callers, STOP — this is a "decide" case, surface it.

  Completion note (2026-07-08): the required grep returned no matches, so there
  were no real `Services()` callers to preserve. Deleted `EngineServices`,
  `EngineContext::Services()`, the stale `RunState.h` include, and now-unused
  service forward declarations. The `EngineContext.h` learning header now states
  the no-reach-through contract and points future splits toward owner-specific
  records. Comment audit touched `EngineContext.h` and `EngineContext.cpp`; both
  have learning headers and no deferred comment work. Validation:
  `tools\validate_format.bat` passed, `tools\validate_full.bat` passed in 61.3s
  (`Agentic\Logs\cleanup-10-step-0.1-services-validate-full.log`) with 0 build
  warnings/errors, 0 DX12 validation errors, matching DX12 screenshots, and
  `physics_regression_solver.csv` byte-exact at 20001 lines.

### Phase 1 — Split `EngineContextBindings`

- [ ] **1.1** Inventory every consumer of `EngineContext` / `EngineContextBindings`
  and classify by owner (scene / simulation / render / diagnostics / input /
  capture / world / physics / UI). No code change.
- [ ] **1.2** For **one owner at a time**: create a narrow record holding only the
  pointers that owner uses; pass it to that owner instead of the whole bag. Gate:
  `validate_full`. Commit. Repeat per owner.
- [ ] **1.3** Delete `EngineContextBindings` once no consumer needs the whole
  graph. `rg -n "EngineContextBindings"` → nothing. Build. Commit.

### Phase 2 — Narrow render interfaces, delete the aggregate

- [ ] **2.1** `rg -n "IRenderBackend&|IRenderBackend \*" SkullbonezSource` to list
  callers that take the aggregate. For **one caller at a time**, change its
  parameter to the narrowest capability it actually uses
  (`IRenderDeviceLifecycle` / `IRenderResourceFactory` / `IRenderCommandContext`
  / `IRenderDiagnostics` / `IRenderCaptureBackend` / `IRenderRayTracing`). Gate:
  `validate_dx12_renderer`. Commit. Repeat.
- [ ] **2.2** Delete the `IRenderBackend` aggregate **type** once unreferenced.
  `rg -n "class IRenderBackend"` → nothing. Gate: `validate_dx12_renderer`
  (`dx12_validation.txt` == 0). Commit.

### Phase 3 — DX12 borrowed-alias dual ownership (danger zone)

- [ ] **3.1** In `RenderBackendDX12`, either remove the cached
  `m_device`/`m_swapChain`/`m_commandList` aliases and reach through
  `Dx12RenderDevice`, or refresh them on device recreation. Exercise device-lost
  / resize paths. Gate: `validate_dx12_renderer` **run 3×**, `dx12_validation.txt`
  == 0 each time. Commit.

### Phase 4 — Collapse or graduate `SimulationController` (FAC-004)

- [ ] **4.1** Check whether `SimulationController` only forwards to
  `SimulationSystem` (it exposes `System()`). If it is a pure delegate: delete it,
  call `SimulationSystem` directly, and remove the `System()` reach-through. If it
  must stay: move real timestep policy into it **and** remove `System()`. Gate:
  `validate_physics` (fixed-step determinism must not change). Commit.

## Validation

`tools\validate_dx12_renderer.bat` (+ `dx12_validation.txt` == 0);
`tools\validate_full.bat` for `Run` wiring changes.

## Acceptance (structural)

- [ ] `rg -n "class IRenderBackend" SkullbonezSource` finds nothing (type
  deleted, not renamed).
- [ ] `EngineContextBindings` is deleted or split; no whole-graph bind remains.
- [x] `EngineServices Services()` is removed or has real callers.
- [ ] `RenderBackendDX12` holds no device/swapchain/commandlist aliases that can
  dangle on recreation.
