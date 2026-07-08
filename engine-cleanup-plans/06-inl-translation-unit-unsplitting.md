# 06 — `.inl` Translation-Unit Un-Splitting

Date: 2026-07-08
Status: In Progress
Priority: P2
Owner: Runtime
Source issue: audit iss-11 (severity 3)

## Problem

Ordinary (non-template) function definitions are textually pasted into whichever
`.cpp` owns the anonymous namespace, giving all the downsides of one giant
compile unit while defeating file-size tooling and navigation. The file
boundaries are cosmetic, not interface seams.

Verified evidence:

- [`RunReplayTools.cpp`](../SkullbonezSource/Runtime/Replay/RunReplayTools.cpp)
  `#include`s six `.inl` bodies at L271-281 —
  `RunReplayPredictionHelpers.inl` (2,577 lines), `RunReplayScrubberTools.inl`,
  `RunReplayCauseTreeTools.inl`, `RunReplayQueryTools.inl`,
  `RunReplayPredictionVisualizer.inl` — forming a **~5,246-line single TU**.
- [`RunEditorTools.cpp`](../SkullbonezSource/Runtime/Editor/RunEditorTools.cpp)
  splices `.inl` includes mid-file (L323, L1651, L1950-1953).
- These are concrete free functions and out-of-line members, **not** templates
  (e.g. `RunReplayScrubberTools.inl` defines `Run::EnterReplayInspectionCamera`;
  `RunEditorTracer.inl` defines `RunEditorTracer::EmitLine`). ~9,393 lines of
  `.inl` live in the Runtime tree.
- All six share one anonymous-namespace scope, must recompile together on any
  edit, and cannot be built or tested independently.

## Goal

Stop the text-splice illusion. Each former `.inl` either becomes a real,
independently-compiled translation unit with a narrow header, or is inlined back
and the size addressed through genuine decomposition (plans 01, 09).

## Approach

- [ ] **Phase 0 — Decide per file:** promote to a real `.cpp` (give it
  declarations + external linkage), or fold back into the owner and fix size via
  real decomposition.
- [ ] **Phase 1 — Promote** the prediction/scrubber/query/cause-tree helpers to
  real TUs with narrow headers. Mechanical: they are free functions / members,
  so add declarations and compile separately.
- [ ] **Phase 2 — Break the shared anonymous namespace** so files no longer
  force whole-TU recompilation and can be unit-tested in isolation.

## Risks

- Symbols currently in an anonymous namespace may collide once given external
  linkage; give them a proper namespace and internal-linkage where still local.
- Coordinate with plan 09 (replay) — much of this `.inl` mass is replay code that
  plan 09 also restructures. Do 09's split first where they overlap.

## Step-by-step implementation

Do steps in order; build after each promotion and commit. Adding a `.cpp`
requires editing both `SKULLBONEZ_CORE.vcxproj` **and**
`SKULLBONEZ_CORE.vcxproj.filters` — do this carefully; a missing entry fails the
build.

### Phase 0 — Editor `.inl` first (execution slot 3)

- [x] **0.1** List the `.inl` files `RunEditorTools.cpp` splices mid-file (L323,
  L1651, L1950-1953): `RunEditorPlacementAssets.inl`, `RunEditorTracer.inl`,
  `RunMousePickupTools.inl`, `RunEditorGizmoTools.inl`,
  `RunEditorOverlayTools.inl`, `RunEditorObjectPlacement.inl`. No code change.

  Inventory note (2026-07-08): current splice points in
  `SkullbonezSource\Runtime\Editor\RunEditorTools.cpp` are:
  - line 323: `RunEditorPlacementAssets.inl` (80,984 bytes)
  - line 1672: `RunEditorTracer.inl` (48,095 bytes)
  - line 1971: `RunMousePickupTools.inl` (9,870 bytes)
  - line 1972: `RunEditorGizmoTools.inl` (18,861 bytes)
  - line 1973: `RunEditorOverlayTools.inl` (11,265 bytes)
  - line 1974: `RunEditorObjectPlacement.inl` (33,893 bytes)

  Project metadata note: all six are currently listed as `ClInclude` entries in
  `SKULLBONEZ_CORE.vcxproj` and `SKULLBONEZ_CORE.vcxproj.filters`, so each
  promotion in step 0.2 must remove the old include entry and add a real
  `ClCompile` entry.

  Validation note: no repository validation required; documentation-only
  inventory.
- [ ] **0.2** For **one `.inl` at a time**: (a) create a matching header
  declaring its free functions / out-of-line members; (b) rename the `.inl` to a
  real `.cpp` that includes that header; (c) remove the mid-file `#include` from
  `RunEditorTools.cpp`; (d) add the new `.cpp` to `SKULLBONEZ_CORE.vcxproj` and
  `.filters`; (e) give former anonymous-namespace symbols a proper namespace, or
  keep them `static` if genuinely file-local. Gate: `validate_fast` then
  `validate_full`. Commit. Repeat for each file.

  Promotion progress:
  - [x] `RunMousePickupTools.inl` -> `RunMousePickupTools.cpp` (2026-07-08).
    This file only defines `Run` member functions already declared in `Run.h`,
    so no duplicate matching header was added; the promoted TU includes
    `RunInternal.h` like the other split `Run` implementation files. The old
    mid-file include was removed from `RunEditorTools.cpp`; project and filter
    metadata now compile the new `.cpp`; mouse-pickup-only tuning constants moved
    with the implementation. `tools\check_runtime_boundaries.py` was updated so
    its mouse-pickup guardrail and synthetic self-tests scan the new `.cpp` path.
    Comment audit touched `RunMousePickupTools.cpp`, `RunEditorTools.cpp`, and
    `tools\check_runtime_boundaries.py`; all already had learning headers and no
    deferred files remain.

    Validation note: initial `tools\validate_fast.bat` failed because the
    runtime-boundary checker still scanned the old `.inl` path; after updating
    the checker, `python tools\check_runtime_boundaries.py` and
    `python tools\check_runtime_boundaries.py --self-test` passed. A second
    `tools\validate_fast.bat` passed in 46.3s
    (`Agentic\Logs\cleanup-06-step-0.2-mousepickup-validate-fast.log`), and
    `tools\validate_full.bat` passed in 45.2s
    (`Agentic\Logs\cleanup-06-step-0.2-mousepickup-validate-full.log`): 0 build
    warnings/errors, 0 DX12 validation errors, DX12 screenshots matched
    committed baselines, and `physics_regression_solver.csv` matched
    byte-exactly at 20001 lines.
  - [x] `RunEditorGizmoTools.inl` -> `RunEditorGizmoTools.cpp` (2026-07-08).
    The promoted TU now includes `EditorTools.h` plus the narrow runtime,
    physics-store, collider, shape, and math headers it actually uses. Its
    former mid-file include was removed from `RunEditorTools.cpp`, and
    `SKULLBONEZ_CORE.vcxproj` / `.filters` now compile the new `.cpp`.
    Store-backed gizmo helper declarations live under `RunInternal` in
    `EditorTools.h`; the matching helper definitions were lifted out of the
    anonymous namespace in `RunEditorTools.cpp` so gizmo math, overlay tracing,
    and placement commits share one live body/collider authority path.
    `TryGetEditorSelectionFrame` was narrowed to the actually used store-backed
    query shape because no caller used the old hidden group-output parameters.
    `tools\check_runtime_boundaries.py` now scans the new `.cpp` path for the
    gizmo selection-frame guardrail, and its synthetic self-tests were updated.
    Comment audit touched `RunEditorGizmoTools.cpp`, `RunEditorTools.cpp`,
    `EditorTools.h`, and `tools\check_runtime_boundaries.py`; all have learning
    headers and required local `Concept`/`Why`/`Invariant` comments, with no
    deferred files.

    Validation note: initial `tools\validate_fast.bat` failed once because the
    runtime-boundary checker still read the old `.inl` path, then failed once in
    the Profile build on a missing promoted-file `Quaternion` using declaration.
    After fixes, `python tools\check_runtime_boundaries.py` passed in 16.3s and
    `python tools\check_runtime_boundaries.py --self-test` passed in 0.3s.
    `tools\validate_fast.bat` passed in 43.8s
    (`Agentic\Logs\cleanup-06-step-0.2-gizmo-validate-fast.log`), and
    `tools\validate_full.bat` passed in 45.4s
    (`Agentic\Logs\cleanup-06-step-0.2-gizmo-validate-full.log`): 0 build
    warnings/errors, 0 DX12 validation errors, DX12 screenshots matched
    committed baselines, and `physics_regression_solver.csv` matched
    byte-exactly at 20001 lines.
  - [x] `RunEditorOverlayTools.inl` -> `RunEditorOverlayTools.cpp` (2026-07-08).
    The promoted TU now includes `EditorOverlayTools.h`, `EditorTools.h`,
    runtime tool declarations, and the collection/body/collider store headers it
    reads directly. Its former mid-file include was removed from
    `RunEditorTools.cpp`, and `SKULLBONEZ_CORE.vcxproj` / `.filters` now compile
    the new `.cpp`. `EditorColliderRadius` is declared through `EditorTools.h`
    so overlay tracing can keep sharing the store-backed shape/radius helper
    lifted during the gizmo split. `tools\check_runtime_boundaries.py` now scans
    the `.cpp` path for mouse-pickup, selection, and attached-camera overlay
    guardrails, and its synthetic self-tests were updated. Comment audit touched
    `RunEditorOverlayTools.cpp`, `RunEditorTools.cpp`, `EditorTools.h`, and
    `tools\check_runtime_boundaries.py`; all have learning headers and required
    local `Concept`/`Why`/`Invariant` comments, with no deferred files.

    Validation note: initial `tools\validate_fast.bat` failed because the
    runtime-boundary checker still read the old `.inl` path. After fixes,
    `python tools\check_runtime_boundaries.py` passed in 16.3s and
    `python tools\check_runtime_boundaries.py --self-test` passed in 0.3s.
    `tools\validate_fast.bat` passed in 47.9s
    (`Agentic\Logs\cleanup-06-step-0.2-overlay-validate-fast.log`), and
    `tools\validate_full.bat` passed in 45.2s
    (`Agentic\Logs\cleanup-06-step-0.2-overlay-validate-full.log`): 0 build
    warnings/errors, 0 DX12 validation errors, DX12 screenshots matched
    committed baselines, and `physics_regression_solver.csv` matched
    byte-exactly at 20001 lines.
  - [ ] `RunEditorPlacementAssets.inl`
  - [ ] `RunEditorTracer.inl`
  - [x] `RunEditorOverlayTools.inl`
  - [ ] `RunEditorObjectPlacement.inl`
- [ ] **0.3** `rg -n '#include ".*\.inl"' SkullbonezSource/Runtime/Editor` —
  confirm no non-template `.inl` is included mid-`.cpp` in Editor/.

### Phase 1 — Replay `.inl` (execution slot 8, with plan 09)

- [ ] **1.1** Defer the `RunReplay*.inl` promotion until plan 09 splits the
  prediction state (same code). When 09 lands, promote each remaining replay
  `.inl` to a real TU using the 0.2 procedure. Gate: `validate_full` + replay
  scrub regression. Commit.

### Phase 2 — Break the shared anonymous namespace

- [ ] **2.1** Confirm the promoted TUs no longer share one anonymous namespace
  and each compiles independently (touch one, build, verify only it recompiles).
  Commit any final cleanup.

## Validation

`tools\validate_fast.bat` (build) then the area gate
(`tools\validate_full.bat` for Runtime).

## Acceptance (structural)

- [ ] No non-template `.inl` is `#include`d mid-`.cpp`.
- [ ] Each former `.inl` compiles as its own TU, or is genuinely small after
  decomposition.
- [ ] No multi-thousand-line file is assembled purely by text splicing.
