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

Verified evidence:

- [`Common.h`](../SkullbonezSource/Core/Common.h:94) `#include`s `Log.h` and
  defines `inline EngineLog& Log()` — a Meyers singleton over a heap-growing
  `unordered_map<string, FILE*>`. Every TU that includes the prelude gets it
  ambiently, and the determinism-critical physics loop writes byte-exact CSVs
  straight through it (it compiles to a Release no-op, so the dependency is
  invisible in shipping builds but real in the validated Debug builds).
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

- [ ] **Phase 0 — Unweld `Log` from the prelude.** Remove the forced `Log.h`
  include from `Common.h`; include it explicitly where used, so the dependency is
  visible.
- [ ] **Phase 1 — Inject the physics diagnostics sink.** The byte-exact CSV
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

- [ ] **0.1** `rg -n "Log\(" SkullbonezSource --include=*.cpp --include=*.h` and
  note which files use `Log()` but do **not** already `#include "Log.h"` directly
  (they rely on `Common.h` pulling it in). No code change.
- [ ] **0.2** Remove `#include "Log.h"` from `Common.h`. Build
  (`validate_fast`). For each compile error, add an explicit `#include` of
  `Log.h` to that TU. Repeat until it builds. Commit.

### Phase 1 — Inject the physics diagnostics sink

- [ ] **1.1** Find the physics CSV write path that currently goes through
  `Log()`. Introduce an explicit sink (interface or function pointer) passed into
  the physics step and route the CSV writes through it. Output must be
  byte-identical. Gate: `validate_physics` byte-exact. Commit.

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

- [ ] `Common.h` no longer force-includes `Log.h`; consumers include it
  explicitly.
- [ ] The physics diagnostics sink is passed in, not reached via a global.
- [ ] The profiler's borrowed `IRenderDiagnostics*` cannot be used after backend
  recreation; process-abort is replaced by `SB_FATAL`.
- [ ] `tools\validate_physics.bat` byte-exact output unchanged.
