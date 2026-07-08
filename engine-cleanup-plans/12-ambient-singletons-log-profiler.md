# 12 — Ambient Singletons: Log / Profiler

Date: 2026-07-08
Status: Proposed
Priority: P3
Owner: Core
Source issue: audit iss-13 (severity 2)

## Problem

Cross-cutting services are never injected but reached through global accessors
welded into the shared prelude, creating hidden dependencies and lifecycle
hazards.

Original evidence before Phase 0:

- Before Phase 0, [`Common.h`](../SkullbonezSource/Core/Common.h:94)
  `#include`d `Log.h` and defined `inline EngineLog& Log()` — a Meyers singleton
  over a heap-growing `unordered_map<string, FILE*>`. Every TU that included the
  prelude got it ambiently, and the determinism-critical physics loop wrote
  byte-exact CSVs straight through it (it compiles to a Release no-op, so the
  dependency is invisible in shipping builds but real in the validated Debug
  builds).
- `Profiler::Instance()` caches a borrowed `IRenderDiagnostics*
  m_renderDiagnostics`, bound once at `Init` and nulled at shutdown with nothing
  rebinding — so any backend recreation that skips re-bind leaves a dangling
  pointer used later in `ReadPendingGpuResults`. A hash / begin-end mismatch
  aborts the whole process via `std::abort()`.

## Goal

Reduce ambient coupling: the physics diagnostics sink is injected rather than
reached globally, and the profiler's borrowed render-diagnostics pointer cannot
dangle.

## Approach

- [x] **Phase 0 — Unweld `Log` from the prelude.** Remove the forced `Log.h`
  include from `Common.h`; include it explicitly where used, so the dependency is
  visible.
- [x] **Phase 1 — Inject the physics diagnostics sink.** The byte-exact CSV
  output becomes an explicit sink passed into the physics step, not an ambient
  global — improves testability and makes the determinism-relevant IO explicit.
- [ ] **Phase 2 — Make the profiler pointer safe.** Rebind
  `m_renderDiagnostics` on backend recreation (or hold it through the owner);
  replace the `std::abort()` on hash mismatch with `SB_FATAL` (ties to plan 04).

## Risks / determinism

The physics CSV path is a validation artifact — the injected sink must produce
byte-identical output. Gate with `validate_physics`.

## Step-by-step implementation

Do steps in order; validate and commit per step. The physics-sink step (1.1) is
byte-exact gated.

### Phase 0 — Unweld `Log` from the prelude

- [x] **0.1** `rg -n "Log\(" SkullbonezSource --include=*.cpp --include=*.h` and
  note which files use `Log()` but do **not** already `#include "Log.h"` directly
  (they rely on `Common.h` pulling it in). No code change.

  Inventory note (2026-07-08): this local `rg` does not support `--include`, so
  the equivalent command used was
  `rg -l -P "(?<![A-Za-z0-9_])Log\(\)" SkullbonezSource -g "*.cpp" -g "*.h"`.
  Files with actual `Log()` calls and no direct `Log.h` include:
  - `SkullbonezSource\Core\PlatformProfiler.cpp`
  - `SkullbonezSource\Core\Profiler.cpp`
  - `SkullbonezSource\Core\SkullScope.cpp`
  - `SkullbonezSource\Physics\PhysicsDiagnosticsSink.cpp`
  - `SkullbonezSource\Rendering\DX12\RenderBackendDX12.cpp`
  - `SkullbonezSource\Rendering\DX12\RenderBackendDX12.DXR.cpp`
  - `SkullbonezSource\Rendering\DX12\RenderBackendDX12.DynamicGeometry.cpp`
  - `SkullbonezSource\Rendering\DX12\RenderBackendDX12.Pipeline.cpp`
  - `SkullbonezSource\Rendering\DX12\RenderBackendDX12.Profiler.cpp`
  - `SkullbonezSource\Rendering\DX12\RenderBackendDX12.Readback.cpp`
  - `SkullbonezSource\Rendering\DX12\RenderBackendDX12.Resources.cpp`
  - `SkullbonezSource\Rendering\DX12\RenderBackendDX12.Textures.cpp`
  - `SkullbonezSource\Rendering\DX12\ShaderDX12.cpp`
  - `SkullbonezSource\Runtime\Init.cpp`
  - `SkullbonezSource\Runtime\Run.cpp`
  - `SkullbonezSource\Runtime\RunInput.cpp`
  - `SkullbonezSource\Runtime\RuntimeDiagnostics.cpp`
  - `SkullbonezSource\Runtime\Scene\RunScene.cpp`
  - `SkullbonezSource\Runtime\Scene\SceneRuntimeLoad.cpp`

  Direct-include/non-action notes: `Common.h` currently owns the ambient include,
  `Log.cpp` includes `Log.h`, `SceneRuntimeCreate.cpp` already includes
  `../../Core/Log.h`, and `Log.h` only contains `Log()` usage examples.
- [x] **0.2** Remove `#include "Log.h"` from `Common.h`. Build
  (`validate_fast`). For each compile error, add an explicit `#include` of
  `Log.h` to that TU. Repeat until it builds. Commit.

  Completion note (2026-07-08): removed the `Log.h` prelude include from
  `Common.h`, moved the global `Log()` convenience accessor into `Log.h`, and
  added explicit `Log.h` includes to the 0.1 inventory plus the existing
  direct-include users. A structural check found only `Log.h` itself as a
  `Log()` definition without a direct include, which is expected.

  Validation note: `tools\validate_fast.bat` initially stopped on formatting
  debt in the earlier DisjointSet slice (`PhysicsWorld.cpp`, then
  `DisjointSet.h`) and then on the project-filter owner prefix for
  `DisjointSet.h`; those metadata/layout issues were repaired in this slice.
  Final `tools\validate_fast.bat` passed
  (`Agentic\Logs\cleanup-12-step-0.2-validate-fast.log`). Because
  `tools\validate_project_filters.py` was updated to recognize `DisjointSet`,
  the focused `tools\validate_project_filters.bat` check was also run and
  passed (`Agentic\Logs\cleanup-12-step-0.2-validate-project-filters.log`).

### Phase 1 — Inject the physics diagnostics sink

- [x] **1.1** Find the physics CSV write path that currently goes through
  `Log()`. Introduce an explicit sink (interface or function pointer) passed into
  the physics step and route the CSV writes through it. Output must be
  byte-identical. Gate: `validate_physics` byte-exact. Commit.

  Completion note (2026-07-08): `PhysicsDiagnosticsSink` no longer includes
  `Log.h` or calls `Log()` directly. Runtime now binds a plain
  `PhysicsDiagnosticsCsvWriter` value in `RunFrame.cpp`, passes it through
  `PhysicsEngine::Step` / `PhysicsScene::RunPhysics` / `PhysicsWorld`, and the
  diagnostics sink formats the same CSV rows through that explicit writer.
  `EngineLog::WriteVf` was added so the runtime writer can forward `va_list`
  formatting without pre-buffering rows or changing byte output.

  Structural check: `rg -n "Log\(\)|Core/Log\.h" SkullbonezSource\Physics`
  finds no source dependency on the logger owner; the only remaining `Log()`
  text is the explanatory comment on `PhysicsDiagnosticsCsvWriter`.

  Validation note: `tools\validate_physics.bat` passed
  (`Agentic\Logs\cleanup-12-step-1.1-validate-physics.log`):
  `physics_regression_solver.csv` matched byte-exactly at 20001 lines, with
  0 build warnings and 0 build errors.

### Phase 2 — Make the profiler pointer safe

- [ ] **2.1** In `Profiler.cpp`, guard the borrowed `m_renderDiagnostics`: rebind
  it on backend recreation, or null-check before use in `ReadPendingGpuResults`
  so a stale pointer cannot be dereferenced. Gate: `validate_full`. Commit.
- [ ] **2.2** Replace the `std::abort()` on hash / begin-end mismatch with
  `SB_FATAL(owner, ...)` (aligns with plan 04). Gate: `validate_full`. Commit.

## Validation

`tools\validate_full.bat`; `tools\validate_physics.bat` for the diagnostics-sink
change (byte-exact).

## Acceptance (structural)

- [x] `Common.h` no longer force-includes `Log.h`; consumers include it
  explicitly.
- [x] The physics diagnostics sink is passed in, not reached via a global.
- [ ] The profiler's borrowed `IRenderDiagnostics*` cannot be used after backend
  recreation; process-abort is replaced by `SB_FATAL`.
- [ ] `tools\validate_physics.bat` byte-exact output unchanged.
