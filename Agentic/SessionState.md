# SkullbonezCore Session State

Date: 2026-07-10

Keep this file operational and short. Detailed evidence belongs in plans,
reports, and git history.

## Current State

| Field | Value |
|---|---|
| Branch | `engine-cleanup-10th-july`, tracking `origin/engine-cleanup-10th-july` |
| Pushed baseline before the current scene commit | `2592c0ac fix: make DX12 command failures fail closed` |
| Current objective | Finish and commit the `ApplicationExitState`/`InputRouter` foundation, then wire exit ownership |
| Last broad local gate | `tools\validate_full.bat` passed final C1a source through the CPU umbrella, DX12 lane with zero InfoQueue errors and matching screenshots, and byte-exact physics regression |
| Native evidence | Injected heap-use-after-free caught; healthy ASan and five-file `/analyze` passed in 16.185s |

## Pushed Cleanup Commits

- `3d25cf48 docs: rebuild the engine cleanup control plane`
- `31395ba5 docs: reconcile stale engine plan references`
- `125bb8a9 docs: inventory DX12 failure surfaces`
- `0c9097ec fix: reduce box manifolds by depth and spread`
- `e752c395 style: format manifold reducer`
- `6d8a3aff fix: make broad validation a CPU-test superset`
- `c13e26ba docs: bind scene asset round-trip ownership`
- `6976c61e docs: hand off the active engine cleanup wave`
- `1e7846d5 ci: add honest CPU and native validation lanes`
- `4a326189 docs: refresh the engine cleanup handoff`
- `2592c0ac fix: make DX12 command failures fail closed`

## Uncommitted Wave — Preserve All Files

All current dirty files belong to this cleanup session. Do not discard or reset
them.

1. **First `Run` extraction cores.** Allocation-free `InputRouter` and
   `ApplicationExitState`, with CPU tests, are now registered in production and
   test projects. They compile and their tests pass through `validate_fast`, but
   they are not wired into `Run`; no extraction deletion proof is complete.
2. **Project-filter metadata.** `validate_project_filters.py` recognizes the two
   new runtime owners and currently passes. Commit it with the `Run` core slice.

DX12 D1-D3 is complete: `SbResult` is non-discardable, command recording and
submitted-work state fail closed, every D3 timestamp/map/present/resize/wait
result is checked, 17 CPU architecture tests passed, three consecutive renderer
gates reported zero InfoQueue errors with matching baselines, and the final full
gate passed. D4-D5 remains in the owning plan.

Scene provenance C1a is complete: parser-owned library/instance/ordered-part
records retain exact shape sources, hierarchy transforms use rotated offsets
and quaternion composition, duplicate explicit/asset/ragdoll names fail
atomically, and runtime ragdoll names preflight before the first append. Parser,
CPU umbrella, physics, and full gates passed from the final source.

## Ten Workstreams To Prioritize

1. Wire `ApplicationExitState` and fix nonzero `WM_QUIT`/first-failure result
   propagation.
2. Wire the immutable `InputRouter` keyboard path and delete the first callback
   pack from `RunInput.cpp`.
3. Finish input pointer/focus/cursor capture ownership and remove later direct
   hardware polling.
4. Split the omnibus runtime command queue into scene, capture, render-default,
   and application owners.
5. Add explicit schema-versioned scene object IDs and deterministic v1 upgrade
   behavior (C1b).
6. Make DX12 resize/resource recreation transactional and define device-loss
   recovery (D4-D5).
7. Promote `SceneController` to own real load/reset/save lifecycle and delete
   `Run` scene callbacks.
8. Move replay workspace decisions and overlays into `ReplayRuntime`.
9. Move render composition/bindings and overlay views into `RuntimeRenderer`.
10. Close the remaining interaction/UI, replay sizing, physics authority,
   renderer decomposition, shadow quality, and behavioral-test plans after the
   five `Run` ownership extractions establish their boundaries.

## Binding Decisions And External Blocker

- `Run` remains process/frame composition after five owner extractions:
  `InputRouter`, owner queues/application exit, `SceneController`,
  `ReplayRuntime`, and `RuntimeRenderer`.
- Input uses a pre-UI immutable device snapshot, one post-UI hit snapshot, then
  post-UI routing. Later phases do not poll hardware directly.
- Persistent self-hosted DX12 CI may run trusted `main`/manual refs only. A
  disposable isolated GPU runner is required before public-PR GPU execution can
  become merge-blocking.

## Non-Negotiable God-Object Closure Gate

Do not close the runtime plan or engine-cleanup campaign merely because
`Run.cpp` becomes short. Treat `Run.h`, every `Run*.cpp`, shared internal
headers, callback/context bags, and forwarding facades as one logical object.
Its only permitted responsibilities at closure are owner construction/wiring,
startup/shutdown, OS message pumping, top-level frame order, and final exit
reporting. `RunInternal.h` and equivalent renamed shared-state hubs must be
gone, and the five extracted owners must remain cohesive rather than becoming
replacement god objects.

The final independent adversarial review must report zero credible god-object,
callback-bag, forwarding-facade, or disguised shared-state-hub findings across
the runtime shell and current cleanup hotspots. Any credible finding reopens
the relevant checklist item and blocks completion; log and fix it rather than
deferring it as optional follow-up.

`Agentic/Plans/MASTER-PLAN.md` remains the authoritative plan index.
