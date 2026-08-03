# Comment Vocabulary And Banner Convention Audit - CV2 Banner Reconciliation

Date: 2026-08-03
Branch: `nightrunner-3rd-AUG-26`
Status: CV2 complete; Summary/Related audit remains in CV3
Impact area: 35 tracked banner-bearing headers across Core, Maths, Physics,
Rendering, Runtime, Scene, and World

## Outcome

The `git ls-files` scope contains the same 35 files recorded by CV0. At the CV1
commit those files contained 62 legacy `/* -- Name -- */` blocks. The working
tree contains zero.

The reconciliation did not delete information by shape:

- 29 informative banner blocks became standard `Concept:` blocks near the type
  or operation they teach;
- unique ownership, lifetime, units, coordinate, and flow facts from other
  banners moved into the modern learning headers; and
- duplicate, vague, or stale banner prose was removed after the modern header
  was shown to carry the current contract.

The same touched-file comment audit also normalized the six
`Plain-language version/rule` aliases in `RenderDeviceDX12.h` and folded Run's
`Mental model:` into its `Summary:`. Those were already binding CV1 rulings and
leaving them in a touched file would have failed the audit.

## Per-File Dispositions

| File | Disposition and preserved information |
|---|---|
| `Core/Log.h` | Merged lazy synchronized ownership, buffered bulk writes, immediate event flush, and Release no-op behavior into the header; deleted the usage banner. |
| `Core/Profiler.h` | Merged marker-path hierarchy, mismatch handling, macro ownership, and disabled-build behavior into the header; deleted the duplicate banner. |
| `Core/Timer.h` | Deleted the vague “easy to use” banner; repaired the Summary to name frequency capture and the three timing buckets. |
| `Maths/GeometricMath.h` | Merged the static/stateless rule into `Invariants:`; the existing header already owned both plane operations and representation. |
| `Maths/GeometricStructures.h` | Removed seven type-restatement banners; the Summary now names the cross-layer plain-data role and TerrainPost pairing, while field names and the existing invariants carry the remaining contracts. |
| `Maths/Matrix4.h` | Deleted the duplicate layout banner; Summary and invariants now state the shared column-major representation and projection split. |
| `Maths/Quaternion.h` | Deleted the filename restatement; Summary now names composition, shortest-arc interpolation, and normalization. |
| `Maths/RotationMatrix.h` | Deleted the filename restatement; Summary now names basis conversion and support-extent use. |
| `Maths/Vector3.h` | Deleted the duplicate public-component banner; the existing Summary and exact representation invariants already preserve the decision. |
| `Physics/BoundingBox.h` | Merged local half-extents/center-offset ownership, body-supplied orientation, derived queries, and authoring-owned inertia into the header. |
| `Physics/BoundingSphere.h` | Merged local center, orientation independence, derived radius facts, and separately authored drag into the header. |
| `Physics/CollisionShape.h` | Recast the tagged-value, shared-visitor, and double-dispatch explanations as three `Concept:` blocks. |
| `Physics/SpatialGrid.h` | Recast the plain-language cell filter as `Concept:`; the existing header retains fixed storage, earliest-bucket ownership, and canonical ordering. |
| `Rendering/DX12/BLASDX12.h` | Merged vertex-only geometry and scratch/result ownership into the Summary; deleted the duplicate banner. |
| `Rendering/DX12/FramebufferDX12.h` | Merged write/read view flow and descriptor-owner lifetime into the Summary; deleted the duplicate banner. |
| `Rendering/DX12/MeshDX12.h` | Merged default-heap buffer/view ownership and narrow collaborators into the Summary; deleted the duplicate banner. |
| `Rendering/DX12/RenderDeviceDX12.h` | Converted all eight device/fence/descriptor/upload/readback banners to `Concept:` blocks. Also mapped four plain-language explanations to `Concept:` and two descriptor rules to `Invariant:`. |
| `Rendering/DX12/SBTDX12.h` | Merged aligned ray-generation, miss, and hit-group record layout into the Summary; deleted the duplicate banner. |
| `Rendering/DX12/ShaderDX12.h` | Deleted the banner because the existing Summary already states bytecode, reflection, CPU constant copy, upload, and pipeline flow. |
| `Rendering/DX12/TLASDX12.h` | Merged per-frame instance-transform rebuild and device-epoch ownership into the Summary; deleted the duplicate banner. |
| `Rendering/PrimitiveBatchRenderer.h` | Deleted the duplicate owner banner; the existing Summary names retained resources and visible/shadow submission. |
| `Rendering/PrimitiveMeshBuilder.h` | Converted the canonical geometry and renderer-specific packing explanation to `Concept:`; repaired the Summary. |
| `Rendering/RenderGraph.h` | Converted the architecture tutorial to `Concept:` and strengthened the header with declaration/compile/executor flow plus the four bounded external transitions. |
| `Rendering/Text.h` | Merged near-plane frustum units into `Invariants:` and repaired the Summary to name fixed TextBatch and flush ownership; deleted the banner. |
| `Runtime/App/Run.h` | Deleted the obsolete generic harness banner and folded `Mental model:` into the one canonical Summary. |
| `Runtime/App/Window.h` | Deleted the duplicate wrapper banner; Summary now names client/projection state and the borrowed resize owner. |
| `Runtime/Camera/CameraCollection.h` | Deleted the stale encapsulation banner, repaired the Summary, and corrected the reset comment from Run-owned to SceneController-owned storage. |
| `Runtime/Debug/BroadphaseVisualizer.h` | Converted overlay/color semantics to `Concept:` and removed the nested `Layman version:` alias. |
| `Runtime/Debug/CollisionVisualizer.h` | Converted the read-only solver-view explanation to `Concept:`, removed the nested alias, and repaired the Summary. |
| `Runtime/Input/Input.h` | Deleted two obsolete state/Win32 wrapper banners; Summary now names the immutable snapshot, event drain, and automation boundary. |
| `Runtime/Render/RuntimeRenderPasses.h` | Converted all twelve pass-owner banners to `Concept:` blocks beside their pass types. |
| `Scene/AuthoredScene.h` | Deleted the narrow regression-only banner; the current header already states cold parsed-value ownership and deterministic runtime setup. |
| `World/SkyBox.h` | Merged texture padding into `Invariants:`, repaired the Summary, and corrected ownership from Run to RenderResourceLifecycle. |
| `World/Terrain.h` | Merged RAW post-grid, shared render/collision surface, and cached query flow into the Summary; deleted the layman alias/banner. |
| `World/WorldEnvironment.h` | Converted force formulas, partial-submersion drag, and calm/ocean mesh roles to one `Concept:` block; repaired the Summary. |

## Comment-Style Audit

Checklist: `Agentic/Plans/TODO/comment-vocabulary-audit.md`

- checked: 35/35;
- deferred: 0;
- unchecked: none.

Every touched file has `File`, `Purpose`, honest `Summary`, `Invariants`, and
`Related` fields. All repository-relative Related paths in the scoped headers
resolve. No Glossary definition moved, so CV2 does not trigger the strict
glossary inventory; CV4 retains the plan's final inventory obligation.

The audit verified the changed ownership and behavior claims against current
source:

- `RenderResourceLifecycle` owns, initializes, and releases the active SkyBox;
- SceneController value-owns the active CameraCollection;
- `Input::CaptureDeviceInputFrame` builds the one keyboard/mouse snapshot and
  injects automation into it;
- Log buffering/event flush, Profiler macro/no-op behavior, and Timer bucket
  ownership match their implementations;
- BoundingBox/BoundingSphere offsets and derived facts match the concrete
  methods and retained fields;
- the RenderGraph exception list matches the current closure inventory; and
- the converted DX12 and runtime-pass concepts remain adjacent to the types
  whose members enforce them.

One stale ownership claim (Run-owned SkyBox) and one stale reset comment
(Run-owned CameraCollection storage) were corrected. No wording remains for
human approval.

## Mechanical Evidence

```text
tracked checklist files: 35
legacy banner blocks at CV1 commit: 62
legacy banner blocks after CV2: 0
required learning-header fields: 35/35
broken scoped Related paths: 0
comment-stripped code comparison: 35 files, 0 mismatches
git diff --check: PASS
```

The comment-stripped comparison reads each CV1-commit blob and working-tree
file, removes C/C++ line and block comments while preserving literals, then
compares normalized remaining code. It proves the complete source diff is
comment-only.

## Validation Decision

No repository validation was run. CV2 changes source comments and documentation
only, and the comment-stripped comparison proves that no code token changed.
The plan's final `tools\validate_fast.bat` remains deferred to CV4.

