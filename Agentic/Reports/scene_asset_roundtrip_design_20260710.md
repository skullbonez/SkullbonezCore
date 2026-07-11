# Scene Asset-Instance Round-Trip Architecture Audit

Date: 2026-07-10

Owners: scene lifecycle, scene/entity identity, physics creation, scene save

Plans:

- `Agentic/Plans/TODO/physics-authority-and-identity.md` C0-C5
- `Agentic/Plans/TODO/runtime-shell-decomposition.md` extraction 3
- `Agentic/Plans/TODO/behavioral-test-depth.md` P3

## Outcome

A parser-only load/save test cannot honestly close P3. The current parser
expands `assetInstances[]` into shape-specific object rows and discards the
asset library token, asset name, instance identity, part identity, and original
part order. Runtime creation then groups by shape section, collection metadata
stores dense model indices, and `SceneSnapshotWriter` can emit only flattened
`objects[]`. A shallow test could compare names, but it would certify the data
loss instead of preventing it.

P3 therefore lands with the scene/entity ownership work. The binding direction
is a scene-owned `SceneEntityStore`, a single preflighted creation transaction,
stable `PhysicsSceneObjectId` identity, and a writer that borrows concrete owner
views rather than `GameModelCollection`.

## Confirmed Data-Loss And Correctness Defects

1. `TestSceneParser::LoadAssetLibrary()` retains raw asset JSON but not the
   authored library token that supplied each definition.
2. `ApplyAssetInstance()` knows the asset and instance names, but
   `ApplyAssetPrimitivePart()` receives only the generated display name and
   effective values. `TestScene` has no asset-instance record.
3. Compound part positions use `instancePosition + partOffset`; the part offset
   is not rotated by the instance orientation. Instance and part Euler values
   are added component-wise instead of composing orientations.
4. Mixed-shape asset recipes lose authored part order because runtime setup
   consumes separate ball, box, and hull vectors.
5. Asset affiliation and behavior grouping are conflated. Current
   `SceneObjectGroupMetadata` represents only releasable trees, and runtime
   grouping uses `rootModelIndex`, a movable dense-row cursor.
6. The writer pairs model, body, and collider state by dense model index and
   silently skips a row when topology disagrees, allowing a plausible but
   incomplete file.
7. Contact-release fields validate for every asset primitive but survive
   expansion only for convex hulls. Authored sphere moment/inertia intent is
   accepted but recomputed and discarded.
8. Effective render material values mostly survive, but they are associated by
   mutable display name rather than stable scene/entity and asset-part identity.

## Binding Domain Model

### Parsed authored records

- `SceneAssetLibraryRef`: authored token and optional resolved asset id.
- `SceneAssetInstanceRecord`: library token, asset name, instance name,
  authored transform, override mask, and ordered part references.
- Every expanded shape record carries stable object id, asset-root object id,
  part name/index, and effective authored descriptors.
- Instance transforms compose as
  `worldPosition = instancePosition + instanceOrientation * partOffset` and
  `worldOrientation = instanceOrientation * partOrientation`.

These records are cold scene-file data and may use parser-owned dynamic
storage. They are not retained as a growing steady-runtime container.

### Runtime scene/entity owner

`SceneController` owns a preallocated `SceneEntityStore` and the scene-lifetime
`PhysicsScene`. Each entity record contains:

- stable `PhysicsSceneObjectId`;
- live `PhysicsBodyHandle`;
- fixed-capacity display name;
- durable render material intent;
- asset root id and part identity;
- a separate behavior group `{kind, rootObjectId, partIndex}`.

Asset affiliation and behavior grouping are orthogonal. Dense indices are
short-lived typed row hints only. The store reserves to configured scene/model
capacity before gameplay and does not grow during steady runtime.

### Creation transaction

Scene creation preflights the complete instance or authored object batch:
capacity, unique ids, part topology, body/collider descriptors, and group roots.
Only then does it commit scene metadata, body, collider, and render rows. A
recoverable authored-input failure leaves no partial compound asset.

### Save boundary

Replace the broad boolean writer boundary with:

```cpp
SbResult SceneSnapshotWriter::Save(
    const SceneSaveView& scene,
    const SceneSaveRequest& request );
```

`SceneSaveView` borrows scene metadata, body/collider stores, and scalar
world/camera values. It owns nothing and does not retain callbacks. Save resolves
rows by stable entity/body identity, emits asset-backed rows through
`assetInstances[]`, and emits only detached/non-asset entities through
`objects[]`. File open/write failure is Lane R; owner-topology mismatch is a
fatal invariant and must never silently omit an entity.

Schema version 2 stores per-part live state under each asset instance so
independently simulated compound parts round-trip without pretending the
original root transform is still authoritative. Version 1 remains readable.

## Required No-`Run` Regression Fixture

Add a small asset library and scene fixture containing:

- a mixed box/sphere/hull compound in an order different from shape sections;
- lateral part offsets and non-commuting instance/part rotations;
- two instances with repeated part names;
- non-contiguous explicit object ids;
- velocity, angular velocity, fixed/sleeping, restitution, contact material,
  inertia, and contact-release intent;
- distinct complete render materials and a separate behavior group.

The production test path is:

```text
parse -> create scene/entity + physics rows -> mutate one part through owner API
      -> save -> parse -> create a second owner set -> compare by object id
```

Compare exact entity count, ids, display/asset/instance/part identity, asset and
behavior roots, transforms (quaternion-equivalent), shape descriptors, physics
descriptors, and every durable render material field. Also inspect the saved
JSON for `assetLibraries[]` and `assetInstances[]`. A set-of-object-names
comparison is explicitly insufficient.

## Implementation Sequence

1. Preserve parser provenance, ordered part references, explicit ids, and
   correct compound transform composition.
2. Extract the preallocated `SceneEntityStore`; replace `rootModelIndex` with
   stable root object id while keeping behavior groups separate from assets.
3. Promote `SceneController` ownership and land the all-or-nothing creation
   transaction for metadata/body/collider/render rows.
4. Narrow the writer to `SceneSaveView`/`SceneSaveRequest`, emit schema v2 asset
   part states, and delete collection/model-order serialization.
5. Add the no-`Run` round-trip and injected-failure proof, then delete the old
   collection save facade and remaining model-owned identity fields.

## Validation

- Parser/provenance slice: `tools\validate_all_cpu_tests.bat`.
- Scene creation/identity slice: CPU umbrella + `tools\validate_physics.bat`.
- Writer and full P3 slice: CPU umbrella + `tools\validate_full.bat`.
- Every touched source file: comment-style audit before handoff.
