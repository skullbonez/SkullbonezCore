# SkullbonezCore Session State

Date: 2026-07-10

Keep this file operational and short. Detailed evidence belongs in plans,
reports, and git history.

## Current State

| Field | Value |
|---|---|
| Branch | `engine-cleanup-10th-july`, tracking `origin/engine-cleanup-10th-july` |
| Pushed tip | `c13e26ba docs: bind scene asset round-trip ownership` |
| Current objective | Resume the engine-cleanup campaign from the uncommitted implementation wave described below |
| Last green targeted build | DX12 architecture: 17 tests in 8.88s; Profile build: 0 warnings/errors in 10.30s |
| Commit readiness | Documentation/tooling is staged but not committed; source wave is intentionally uncommitted |

## Pushed Branch History

The branch already contains and has pushed these cleanup commits:

- `3d25cf48 docs: rebuild the engine cleanup control plane`
- `31395ba5 docs: reconcile stale engine plan references`
- `125bb8a9 docs: inventory DX12 failure surfaces`
- `0c9097ec fix: reduce box manifolds by depth and spread`
- `e752c395 style: format manifold reducer`
- `6d8a3aff fix: make broad validation a CPU-test superset`
- `c13e26ba docs: bind scene asset round-trip ownership`

## Uncommitted Wave — Preserve All Files

All current dirty files were created or changed by this cleanup session. Do not
discard or reset them.

1. **Validation/CI V3-V4 (currently staged).** Three GitHub workflows, real
   PR/merge-base blob-size checking, bounded ASan plus `/analyze`, suppression
   governance, evidence reports, and docs. Native self-test passed; static
   analysis passed with five fresh sidecars and zero warnings. `actionlint`
   v1.7.12 passed all workflows. This scope still needs the final fast gate.
2. **Scene provenance C1a (unstaged).** Parser retains library/instance/ordered
   part/source provenance, composes rotations/offsets correctly, rejects global
   generated-name collisions including ragdoll parts, and has focused tests.
   The standalone parser suite last passed 5/5. Required physics/full gates
   remain before commit.
3. **DX12 D1-D3 subset (unstaged).** Checked recording epochs, close/reset/map/wait
   propagation, strict flush ordering, submitted-but-unfenced state, safe
   Resize/Shutdown drain rules, and 17 CPU fault-order tests. Profile builds
   cleanly. Required three consecutive renderer gates plus `validate_full`
   remain before commit. Do not mark plan D3 complete: timestamp-frequency
   handling at `RenderBackendDX12.cpp:1639` is still ignored. D4 transactional
   ResizeBuffers/GetBuffer/depth rollback and device recreation also remain open.
4. **Run extraction cores (untracked prototypes).** Allocation-free
   `InputRouter` plus tests and `ApplicationExitState` plus tests exist in new
   files only. They are not registered in project files, have not compiled, and
   must not be committed as complete until integrated and tested.

## Last Gate And Exact Blocker

`tools\validate_fast.bat` was attempted while packaging the staged
validation/tooling commit. It stopped at formatting before metadata/size/build:

```text
FAIL: Rendering/DX12/RenderBackendDX12.cpp
FAIL: Rendering/DX12/RenderBackendDX12.Profiler.cpp
FAIL: Rendering/DX12/SBTDX12.cpp
FAIL: Runtime/InputRouter.cpp
FAIL: Runtime/RunStress.cpp
FAIL: 5 files need formatting.
```

Do not claim `validate_fast` passed. Format only these owned files first, rerun
the comment audit for changed source, then rerun the gate. Avoid a broad format
command that could touch unrelated files.

## Resume Order

1. Run `git status --short --branch`; preserve the staged/unstaged split.
2. Format the five named files with the repository clang-format path and verify
   `git diff --check`.
3. Rerun `tools\validate_fast.bat`. If green, commit/push the staged
   validation/CI scope with its reports and `validation-gate-integrity.md`.
4. Register and compile the two pure Run-extraction cores, review their APIs,
   and integrate application-exit propagation only after the DX12 edits in
   `Run.h`/`RunFrame.cpp` are stable.
5. Run the scene parser/CPU/physics gates for C1a and commit it separately.
6. Run `tools\validate_dx12_renderer.bat` three consecutive times, then
   `tools\validate_full.bat`, before committing D1-D3.
7. Update `dx12-failure-propagation.md`, MASTER, plan phase counts, and this
   handoff only after those gates supply final evidence.

## Binding Decisions And External Blocker

- `Run` remains process/frame composition after five owner extractions:
  `InputRouter`, owner command queues/application exit, `SceneController`,
  `ReplayRuntime`, and `RuntimeRenderer`.
- Input is a two-phase immutable snapshot: pre-UI route, one UI hit snapshot,
  then post-UI route. Later phases do not poll hardware directly.
- Command ownership needs scene, capture, render-default, and application-exit
  owners; three generic queues are insufficient.
- Persistent self-hosted DX12 CI may run trusted `main`/manual refs only. A
  disposable isolated GPU runner is required before public-PR GPU execution can
  become merge-blocking.

`Agentic/Plans/MASTER-PLAN.md` remains the authoritative plan index.
