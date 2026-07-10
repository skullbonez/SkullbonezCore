# SkullbonezCore Session State

Date: 2026-07-10

Keep this file operational and short. Detailed evidence belongs in plans,
reports, and git history.

## Current State

| Field | Value |
|---|---|
| Branch | `engine-cleanup-10th-july`, tracking `origin/engine-cleanup-10th-july` |
| Pushed baseline before DX12 D4-D5 | `7fdd91d3 feat: stabilize scene behavior group roots` |
| Current objective | Complete ReplayRuntime workspace ownership, then RuntimeRenderer composition ownership |
| Last broad local gate | `tools\validate_full.bat` passed final DX12 D4-D5 source with zero-warning builds, the mandatory CPU umbrella, DX12 with zero InfoQueue errors/matching screenshots, standalone physics smoke, and byte-exact physics in 49.5s |
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
- `28a8b205 feat: move scene entity metadata to scene owner`
- `e147f9b8 feat: make scene creation transactional`
- `119b359c feat: preserve live asset scene snapshots`
- `7fdd91d3 feat: stabilize scene behavior group roots`

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

DX12 D1-D5 is complete. Required startup and optional-feature failure paths
retain one result, resize publishes only complete replacement sets, device loss
is sticky and tears down without later queue/present work, and the Debug fault
probe exits 1 before the sole submission site with one 457-byte diagnostic and
zero submissions/InfoQueue errors. The plan-level adversarial review found and
fixed device-loss teardown issuing a later fence signal; the repeat review was
clean. Final fast, architecture, fault, three consecutive renderer, allocation,
and full gates passed.

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

Transactional creation C3 is complete. `TryCreateSceneEntity` preflights every
same-row metadata, physics, and render owner before mutation; recoverable input
failure leaves every count unchanged, while topology/reservation drift is
fatal. Render rows publish during creation, the old append API and duplicate id
parameter are deleted, and clear proves zero topology. The waited standalone
smoke reports `creation_atomic=pass`; CPU, allocation, fast, physics,
performance, and full gates pass from the final source.

Scene snapshot ownership C4 is complete. `SceneSnapshotWriter` borrows explicit
owner data, resolves body/collider rows through stable scene identity, emits
schema-v2 asset instances with authoritative per-part live state, and fails
topology drift fatally instead of silently skipping rows. The collection save
facade is deleted. A mixed-shape no-`Run` writer/parser regression and a waited
production building-asset save/reload probe pass; allocation policy, CPU, and
full gates pass from the final source.

Stable behavior ownership C5 is complete. `SceneEntityStore` owns behavior
groups separately from asset affiliation and stores stable root object ids;
collection physics paths derive dense rows only at cold compatibility
boundaries. The collection group sidecar/types/creation argument and scoped
row-root spellings are deleted. The C1-C5 adversarial review found and fixed
parser publication of missing roots plus incomplete no-`Run` evidence. The
corrected fixture recreates fresh owners and passes 444 stable-id comparisons;
the follow-up review is clean. Parser, CPU, allocation, physics, performance,
and full gates pass from the final source.

## Ten Workstreams To Prioritize

1. Move replay workspace decisions, tools, and overlays behind `ReplayRuntime`.
2. Move render composition, bindings, and overlay views behind `RuntimeRenderer`.
3. Close the dependent B1f scene/input seam and promote `SceneController` to
   own real load/reset/save lifecycle and delete `Run` scene callbacks.
4. Finish physics stable-identity D1-D4 and the remaining interaction/UI work.
5. Finish validation-gate V3-V4 and behavioral-test P3/P5/P6 evidence.
6. Close remaining interaction/UI, replay sizing, and physics authority items.
7. Close renderer decomposition and shadow quality after the five `Run`
   ownership extractions establish their boundaries.
8. Run the final ownership and campaign adversarial reviews, fixing every
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
