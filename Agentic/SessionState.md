# SkullbonezCore Session State

Date: 2026-07-10

Keep this file operational and short. Detailed evidence belongs in plans,
reports, and git history.

## Current State

| Field | Value |
|---|---|
| Branch | `engine-cleanup-10th-july`, tracking `origin/engine-cleanup-10th-july` |
| Pushed baseline before exit wiring | `670a9fcb feat: add runtime input and exit owners` |
| Current objective | Wire the immutable `InputRouter` keyboard path and delete `MappedKeyboardDispatchContext` plus its callback pack |
| Last broad local gate | `tools\validate_full.bat` passed ApplicationExitState production wiring through the CPU umbrella, DX12 lane with zero InfoQueue errors and matching screenshots, and byte-exact physics regression |
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
- `acdc994c feat: preserve scene asset provenance`
- `670a9fcb feat: add runtime input and exit owners`

## Current Queue

`ApplicationExitState` is now the production frame-loop result owner. It
preserves the first capture/input/automation/replay/renderer failure, translates
nonzero `WM_QUIT`, and prevents later normal shutdown from erasing evidence.
B2a is complete with CPU and full-gate proof. `InputRouter` remains the next
foundation owner to wire; the next slice routes keyboard actions and deletes
the first input callback/context pack.

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

1. Wire the immutable `InputRouter` keyboard path and delete the first callback
   pack from `RunInput.cpp`.
2. Finish input pointer/focus/cursor capture ownership and remove later direct
   hardware polling.
3. Split the omnibus runtime command queue into scene, capture, render-default,
   and application owners.
4. Add explicit schema-versioned scene object IDs and deterministic v1 upgrade
   behavior (C1b).
5. Make DX12 resize/resource recreation transactional and define device-loss
   recovery (D4-D5).
6. Promote `SceneController` to own real load/reset/save lifecycle and delete
   `Run` scene callbacks.
7. Move replay workspace decisions and overlays into `ReplayRuntime`.
8. Move render composition/bindings and overlay views into `RuntimeRenderer`.
9. Finish validation-gate V3-V4 and behavioral-test P3/P5/P6 evidence.
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
