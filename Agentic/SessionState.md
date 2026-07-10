# SkullbonezCore Session State

Date: 2026-07-10

Keep this file operational and short. Detailed evidence belongs in plans,
reports, and git history.

## Current State

| Field | Value |
|---|---|
| Branch | `engine-cleanup-10th-july`, tracking `origin/engine-cleanup-10th-july` |
| Pushed baseline before scene entity ownership | `d817a995 feat: stabilize authored scene identities` |
| Current objective | Make scene metadata/body/collider/render creation transactional in physics-authority C3 |
| Last broad local gate | `tools\validate_full.bat` passed scene-owned entity metadata through 116 CPU tests/2,122 assertions, zero-warning builds, DX12 with zero InfoQueue errors/matching screenshots, and byte-exact physics |
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
- `5b56af13 fix: preserve application exit failures`
- `e7c2e4a2 feat: route runtime keyboard actions through InputRouter`
- `225b9688 feat: centralize runtime pointer input ownership`
- `fd48d658 feat: split runtime requests by owner`
- `d817a995 feat: stabilize authored scene identities`

## Current Queue

`InputRouter` now owns the complete device snapshot, semantic and pointer edge
memory, all-of binding contexts, one post-UI hit value, focus cancellation, and
native capture/cursor intent. Direct later hardware polls, duplicate UI/replay/
editor pointer memories, and both input callback packs are deleted. B1a-B1e are
complete except the intentionally separate B1f final `Run` method/state
extraction proof.

Owner queue B2b-B2e is complete. Capture, render-default persistence, and scene
requests now use fixed owner storage; application exit remains value-only.
The generic queue/type/mixed switch and dead advance/quit cases are deleted.
Replay records only successful owner events on explicit stable wire codes, and
the scene batch accepts at most the first same-frame transition. The final fast,
CPU, interaction, perf, and full gates passed from the completed boundary.

The first perf run exposed eight duplicate names in the varied physics bench
fixture after C1a made name collisions honest. The ball rows now have unique
names; the final performance and full gates pass without relaxing validation.

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

Stable scene identity C1b is complete. Schema v2 requires explicit nonzero ids
for direct objects and ordered asset parts; duplicate/missing/wrong-version
input fails atomically. Version 1 upgrades once in the historical runtime
section order, authored creation consumes stored ids rather than allocating by
loop order, and later runtime spawns continue above the highest sparse id. The
parser, CPU umbrella, physics, and full gates passed from the final source.

Scene entity ownership C2 is complete. `SceneController` owns the preallocated
stable-id/body, display-name, material, and asset-affiliation rows; `GameModel`
retains only transient contact-highlight timers. Creation callers publish
`SceneEntityCreateDesc`, and replay/save/style/selection/automation consumers
read the scene owner. An initial eager-array +5.3 MB regression was corrected by
reserving configured cold rows before population; allocation policy, CPU, fast,
physics, performance, and full gates pass from the final source.

## Ten Workstreams To Prioritize

1. Complete transactional scene creation so metadata, body, collider, and
   render rows commit together or not at all (physics-authority C3).
2. Complete v2 asset round-trip and stable-root
   behavior ownership (C4-C5), then close the dependent B1f scene/input seam.
3. Make DX12 resize/resource recreation transactional and define device-loss
   recovery (D4-D5).
4. Promote `SceneController` to own real load/reset/save lifecycle and delete
   `Run` scene callbacks.
5. Move replay workspace decisions and overlays into `ReplayRuntime`.
6. Move render composition/bindings and overlay views into `RuntimeRenderer`.
7. Finish validation-gate V3-V4 and behavioral-test P3/P5/P6 evidence.
8. Close remaining interaction/UI, replay sizing, and physics authority items.
9. Close renderer decomposition and shadow quality after the five `Run`
   ownership extractions establish their boundaries.
10. Run the final ownership and campaign adversarial reviews, fixing every
   credible finding before closure.

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
