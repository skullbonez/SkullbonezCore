# Physics Playground Refactor And File And Type Prefix Cleanup Plan

Date: 2026-06-21
Status: Overnight implementation plan
Impact area: source/type naming, Visual Studio project metadata, runtime architecture, physics playground direction
Validation for this document-only change: none required

## Goal

Make SkullbonezCore feel like a deliberate, professional physics playground
instead of an old codebase with the same brand prefix stamped across every
ordinary source file and type name.

SkullbonezCore is the heart of the engine. This plan must not erase that
identity. The goal is to remove blanket superfluous repetition, not to pretend
the engine is no longer SKULLBONEZ CORE.

This plan replaces the narrow `skullbonez-run-decomposition-plan.md` direction.
That older plan is useful as inventory, but it mostly slices `SkullbonezRun`
sideways. The better direction is:

1. Remove the blanket `Skullbonez` prefix from ordinary file and type names as a
   foundation cleanup.
2. Make Visual Studio filters tell the real subsystem story.
3. Fix the formatter so declarations and calls are readable.
4. Keep the first rename pass behavior-preserving.
5. Then extract clean Runtime, Scene, Simulation, Physics, Rendering, Editor,
   Replay, Assets, UI, and Diagnostics ownership.
6. Finish with dead-code removal and a full comment-style pass.

The rename is not vanity. It removes noise before the architecture refactor
starts, makes new module names read like engine code rather than branded demo
code, and gives future files a clean convention.

## Current Read

The implementation workspace is expected to be clean at handoff. The overnight
worker must still run `git status --short --branch` before editing and before
committing, but this plan assumes there are no pre-existing dirty files to
preserve.

Observed filename scope:

| Scope | Count | Notes |
|-------|-------|-------|
| Exact `Skullbonez*` prefix | 175 | 174 C++ source/header files plus `SkullbonezCore.png` |
| Case-insensitive brand prefix | 181 | Adds `SKULLBONEZ_CORE.*`, lowercase docs, and `skullbonezbase.scene.json` |
| Prefixed include directives | About 495 | Must be updated in source and UI files |

The implementation should remove the exact mixed-case `Skullbonez` prefix from
ordinary source filenames and ordinary engine type names. Uppercase build
identity files such as `SKULLBONEZ_CORE.sln`, `SKULLBONEZ_CORE.vcxproj`, output
exe names, macros, and the `SkullbonezCore` namespace remain unchanged in this
plan. A full product rebrand is explicitly out of scope.

## Naming Rules

### Files

Rename every C++ source/header file whose basename starts with exact
`Skullbonez` by deleting that leading prefix when the remaining name is clear:

| Before | After |
|--------|-------|
| `SkullbonezSource/SkullbonezRun.h` | `SkullbonezSource/Run.h` |
| `SkullbonezSource/SkullbonezRunInput.cpp` | `SkullbonezSource/RunInput.cpp` |
| `SkullbonezSource/SkullbonezPhysicsWorld.cpp` | `SkullbonezSource/PhysicsWorld.cpp` |
| `SkullbonezSource/SkullbonezRenderBackendDX12.Textures.cpp` | `SkullbonezSource/RenderBackendDX12.Textures.cpp` |
| `SkullbonezSource/UI/SkullbonezUI.cpp` | `SkullbonezSource/UI/UI.cpp` |

Keep `Skullbonez` in a filename only when the brand is genuinely the subject of
the file, not a blanket prefix. Examples that may keep the name:

- `SkullbonezCore.png`, because it is product branding.
- historical reports or docs that discuss SkullbonezCore by name.
- any future file whose purpose is explicitly about SkullbonezCore identity,
  packaging, release branding, or migration history.

### Types And Symbols

Rename ordinary C++ classes, structs, enums, and free functions whose leading
`Skullbonez` prefix is only redundant branding. Do not rename symbols where
`Skullbonez` is the product identity, namespace, binary contract, macro family,
or external artifact.

Examples:

| Before | After |
|--------|-------|
| `class SkullbonezRun` | `class Run` |
| `class SkullbonezWindow` | `class Window` |
| `class SkullbonezConfig` | `class Config` or `EngineConfig` if `Config` collides |
| `SkullbonezRun::Run()` | `Run::Run()` |
| `namespace SkullbonezCore` | unchanged |
| `SKULLBONEZ_CORE.exe` | unchanged |
| `SKULLBONEZ_PROFILE_ENABLED` | unchanged |

Rules:

- Prefer the shortest clear engine name after removing the prefix.
- If the stripped name collides or becomes too generic, choose the subsystem
  name that reads best in context, such as `EngineConfig`, `RenderDeviceDX12`,
  or `RuntimeDiagnostics`.
- Keep namespace-qualified meaning rather than restamping every type with the
  product name.
- Update constructor/destructor names, forward declarations, friend
  declarations, comments, tests, and project references in the same slice.
- Do not rename public file formats, config keys, scene directives, CLI flags,
  executable names, project names, macros, or namespaces as part of this pass.

This keeps SkullbonezCore as the engine identity while making ordinary code read
like engine code instead of a brand prefix catalog.

### Includes And File Headers

Update all include directives and file header comments that reference renamed
paths:

```cpp
#include "SkullbonezRun.h"       // old
#include "Run.h"                 // new

#include "../SkullbonezText.h"   // old
#include "../Text.h"             // new
```

Update `File:` and `Related:` comment paths in touched files so the repository
comment standard remains true after the rename. Do not rewrite comment prose
unless it names an old path.

### Non-Source Brand Files

Do not rename these in the first overnight implementation:

- `SKULLBONEZ_CORE.sln`
- `SKULLBONEZ_CORE.vcxproj`
- `SKULLBONEZ_CORE.vcxproj.filters`
- `Agentic/Plans/skullbonez-run-decomposition-plan.md`
- `Agentic/Reference/skullbonez-core-class-structure.md`
- `SkullbonezData/scenes/skullbonezbase.scene.json`

Reason: these are product/build identity or historical documentation paths.
Changing them has wider script, README, image, and handoff implications. The
ordinary file/type prefix cleanup gives the architecture immediate clarity
without bundling a full rebrand.

`SkullbonezCore.png` may remain in place during the source pass. If the worker
renames it, use `Core.png` and update README references in the same commit.
The recommended overnight path is to leave the image alone and keep the first
commit source/project focused.

## Formatter Contract

The current formatter pipeline is part of the problem. `.clang-format` has
`ColumnLimit: 0`, and `tools\format_fix.bat` runs
`Agentic\Skills\collapse_params.py`, whose job is to collapse multiline
function parameter lists in headers. That creates exactly the unreadable shape
this refactor is meant to remove.

Required style:

```cpp
// Ray-picks editable objects from the current mouse ray.
bool TryPickEditorModel( const Math::Vector::Vector3& rayOrigin,
                         const Math::Vector::Vector3& rayDirection,
                         int& outIndex ) const;
```

Forbidden style:

```cpp
bool TryPickEditorModel( const Math::Vector::Vector3& rayOrigin, const Math::Vector::Vector3& rayDirection, int& outIndex ) const;
```

Intent:

- Keep the comment on the page near the code it explains.
- Avoid giant single-line declarations that push trailing comments far off to
  the right.
- Prefer a short leading comment above multiline declarations instead of a
  trailing comment after a long signature.
- Keep trailing comments for short fields, short declarations, units, ranges,
  ownership, and sentinel values where the comment remains visually close.

Formatter repair tasks:

1. Remove `collapse_params.py` from `tools\format_fix.bat`.
2. Do not run any script that collapses function declarations or calls into one
   long line.
3. Change `.clang-format` so long parameter and argument lists break across
   lines.
4. Keep Allman braces, four-space indentation, left pointer/reference
   alignment, preserved include order, and the existing space-in-parentheses
   style unless explicitly changed by the user.
5. Update `tools\validate_format.bat` and `tools\format_fix.bat` so they cover
   all C++ files under `SkullbonezSource`, including `SkullbonezSource/UI` and
   future module folders, rather than only top-level `*.cpp` and `*.h`.
6. Keep formatter changes in their own slice before broad architecture movement
   so formatting churn does not hide behavior edits.

Recommended `.clang-format` policy:

```yaml
ColumnLimit: 120
BinPackParameters: false
BinPackArguments: false
AllowAllParametersOfDeclarationOnNextLine: false
AllowAllArgumentsOnNextLine: false
AlignAfterOpenBracket: Align
PenaltyBreakBeforeFirstCallParameter: 0
```

Acceptance:

- The specific long declarations in `Run.h` format with one parameter per line
  once they exceed the column limit.
- Function calls with long arguments also break cleanly.
- No broad format pass leaves trailing comments stranded far to the right.
- `tools\validate_format.bat` checks top-level source files, UI files, and
  future module folders.

Validation:

- `tools\validate_format.bat`.
- `tools\validate_build.bat Profile` after tool/config changes.

## Visual Studio Project And Filter Rules

The `.vcxproj` and `.vcxproj.filters` updates are part of the rename. They are
not cleanup after the fact.

### Required Project Metadata Updates

For every renamed `.cpp` and `.h`:

1. Update the matching `ClCompile` or `ClInclude` path in
   `SKULLBONEZ_CORE.vcxproj`.
2. Update the matching `ClCompile` or `ClInclude` path in
   `SKULLBONEZ_CORE.vcxproj.filters`.
3. Keep item type unchanged.
4. Preserve custom metadata if any exists under the item.
5. Add or update matching entries in Agentic test project files that compile
   engine sources:
   - `Agentic/Tests/SceneParserUnitTests/SceneParserUnitTests.vcxproj`
   - `Agentic/Tests/Dx12ArchUnitTests/Dx12ArchUnitTests.vcxproj`

Use a generated old-path to new-path map. Do not hand-edit dozens of path
strings by memory.

### Required Filter Shape

The filters should reflect meaningful engine modules, not just file extensions.
The existing filter tree already has useful anchors. Expand it deliberately.

Required filters:

| Filter | Purpose |
|--------|---------|
| `Source Files\Core` / `Header Files\Core` | platform-neutral core utilities, config, logging, timing, worker support |
| `Source Files\Runtime` / `Header Files\Runtime` | app shell, frame loop, live runtime state, input routing, captures, diagnostics |
| `Source Files\Runtime\Scene` / `Header Files\Runtime\Scene` | scene load/reset/advance orchestration and scene runtime state |
| `Source Files\Runtime\Replay` / `Header Files\Runtime\Replay` | replay recorder/exporter, scrubber, prediction, restore helpers |
| `Source Files\Runtime\Editor` / `Header Files\Runtime\Editor` | editor placement, gizmos, launcher/editor tools once extracted |
| `Source Files\Assets` / `Header Files\Assets` | asset registry, texture collection, style/asset source records |
| `Source Files\Scene` / `Header Files\Scene` | authored scene data, parser, snapshot writer, scene file support |
| `Source Files\GameObjects` / `Header Files\GameObjects` | current compatibility model/entity facade and render stream cache |
| `Source Files\Physics` / `Header Files\Physics` | physics world, bodies, collision, solver, sleep, diagnostics, spatial grid |
| `Source Files\Physics\Debug` / `Header Files\Physics\Debug` | collision, broadphase, and physics debug visualizers |
| `Source Files\Rendering` / `Header Files\Rendering` | renderer-facing contracts, render graph, materials, shadows, text render support |
| `Source Files\Rendering\DX12` / `Header Files\Rendering\DX12` | DX12 backend, device, shader, mesh, framebuffer, DXR/BLAS/TLAS/SBT |
| `Source Files\World` / `Header Files\World` | terrain, sky, world environment, water/world presentation data |
| `Source Files\Maths` / `Header Files\Maths` | vector, matrix, quaternion, rotation, geometry primitives |
| `Source Files\UI` / `Header Files\UI` | all in-engine UI files |

Mapping examples:

| Renamed file | Filter |
|--------------|--------|
| `Run.cpp`, `RunFrame.cpp`, `RunInput.cpp`, `RunInternal.h` | `Runtime` |
| `RunScene.cpp`, `SceneRuntime.cpp/h` | `Runtime\Scene` |
| `ReplayRecorder.cpp/h`, `ReplayExporter.cpp/h`, `ReplaySolverSnapshot.h` | `Runtime\Replay` |
| `CaptureSystem.cpp/h`, `RuntimeDiagnostics.cpp/h`, `RuntimeFileWriter.cpp/h` | `Runtime` or `Runtime\Diagnostics` if that filter is added |
| `PhysicsWorld.cpp/h`, `RigidBody.cpp/h`, `PersistentContactSolver.cpp/h` | `Physics` |
| `CollisionVisualizer.cpp/h`, `BroadphaseVisualizer.cpp/h`, `PhysicsDebugVisualizer.cpp/h` | `Physics\Debug` |
| `RenderBackendDX12.*`, `RenderDeviceDX12.*`, `ShaderDX12.*`, `MeshDX12.*`, `FramebufferDX12.*`, `BLASDX12.*`, `TLASDX12.*`, `SBTDX12.*`, `Dx12RenderGraphExecutor.*` | `Rendering\DX12` |
| `IRenderBackend.cpp/h`, `RenderGraph.cpp/h`, `RenderMaterial.h`, `ShaderContracts.h`, `Shadow.h` | `Rendering` |
| `GameModel*` files | `GameObjects`, except `GameModelRenderer.*` stays `Rendering` |
| `TestScene*`, `SceneSnapshotWriter.*` | `Scene` |
| `Terrain.*`, `TerrainSupportClassifier.h`, `WorldEnvironment.*`, `SkyBox.*` | `World` |
| `Vector3.*`, `Matrix4.*`, `Quaternion.*`, `RotationMatrix.*`, `Geometric*` | `Maths` |
| `UI.cpp/h` and every file under `SkullbonezSource/UI` | `UI` |

Acceptance for filters:

- No renamed source/header file is left under only top-level `Source Files` or
  `Header Files` unless it is genuinely unclassified.
- Every `ClCompile` and `ClInclude` item in `.vcxproj` has a matching entry in
  `.vcxproj.filters`.
- No `.filters` entry points to a path that no longer exists.
- New filter nodes have stable GUIDs and are committed with the rename.
- The Visual Studio Solution Explorer tells the same architecture story as the
  planned module extraction.

## Implementation Slices

### Slice 0: Guardrails And Rename Map

Tasks:

1. Run `git status --short --branch`.
2. Confirm the workspace is clean before starting. If it is not clean, stop and
   ask for a clean handoff rather than working around dirty files.
3. Generate a rename map for exact `Skullbonez*` source/header basenames.
4. Generate a type rename map for ordinary C++ symbols with a redundant
   `Skullbonez` prefix.
5. Assert every target path and target symbol is collision-free before moving
   anything.
6. Assert the file map has 174 source/header entries unless the worktree
   changed.

Suggested map generation:

```powershell
$renameMap = rg --files SkullbonezSource |
  Where-Object { [IO.Path]::GetFileName($_) -clike 'Skullbonez*' } |
  ForEach-Object {
    $dir = Split-Path $_ -Parent
    $name = [IO.Path]::GetFileName($_)
    [pscustomobject]@{
      Old = $_
      New = Join-Path $dir ($name -replace '^Skullbonez', '')
    }
  }
```

Acceptance:

- The rename map is deterministic and sorted.
- No target collision exists.
- The type map explicitly distinguishes ordinary redundant prefixes from
  product identity names that must remain.
- The workspace is clean before mutation starts.

Validation:

- No repository validation required.

### Slice 1: Mechanical File And Type Rename

Tasks:

1. Use `git mv` for every old path to new path.
2. Do not use broad delete or clean commands.
3. Keep physical source location flat for this first slice except existing
   `SkullbonezSource/UI`.
4. Apply the approved type rename map to declarations, definitions,
   constructors/destructors, call sites, forward declarations, friends, and
   comments.
5. Keep `SkullbonezCore`, `SKULLBONEZ_CORE`, macro prefixes, project names,
   executable names, CLI flags, config keys, and file formats unchanged.

Acceptance:

- `rg --files SkullbonezSource | rg '[/\\]Skullbonez[^/\\]*\.(cpp|h)$'`
  returns no results.
- Ordinary engine type declarations no longer carry the blanket `Skullbonez`
  prefix unless the plan records a specific reason.
- Brand identity symbols remain intact.

Validation:

- No formal validation yet. This slice will not compile until include and
  project metadata references are updated.

### Slice 2: Include And Comment Path Update

Tasks:

1. Apply the rename map to quoted include paths in source and UI files.
2. Apply the same path map to file header `File:` and `Related:` entries.
3. Apply the map to source references in README, AGENTS, Agentic docs, and test
   project sources where those references are current operational docs.
4. Leave old historical plan text alone unless it is used as current guidance.

Acceptance:

- No active source file includes a removed `Skullbonez*.h` path.
- No active source file header starts with `File: SkullbonezSource/Skullbonez`.
- Historical audit/plan references can remain if they describe old work.

Validation:

- A targeted `tools\validate_build.bat Profile` is allowed during implementation
  if compile feedback is needed.

### Slice 3: Project And Filters

Tasks:

1. Update `SKULLBONEZ_CORE.vcxproj` with renamed `ClCompile` and `ClInclude`
   paths.
2. Update `SKULLBONEZ_CORE.vcxproj.filters` with renamed paths.
3. Add or adjust meaningful filters from the required filter shape above.
4. Keep DX12 files under `Rendering\DX12`.
5. Keep runtime/replay/editor files grouped by intent, not merely by old
   filename location.
6. Update Agentic unit test project paths.

Use XML-aware editing or a generated path replacement over a validated map. Do
not manually patch a handful of project paths and hope the rest follows.

Acceptance:

- `SKULLBONEZ_CORE.vcxproj` contains no `SkullbonezSource\Skullbonez` source
  or header path.
- `SKULLBONEZ_CORE.vcxproj.filters` contains no stale renamed path.
- Unit test project files compile the renamed engine source paths.
- Solution Explorer grouping is meaningful enough to guide future subsystem
  extraction.

Validation:

- Targeted `tools\validate_build.bat Profile` after metadata update.

### Slice 4: Formatter Tool Repair

Tasks:

1. Remove the `collapse_params.py` call from `tools\format_fix.bat`.
2. Update `.clang-format` with the multiline declaration/argument policy from
   this plan.
3. Update formatter scripts to recurse through all C++ files under
   `SkullbonezSource`.
4. Run the formatter only on files intentionally touched by the current branch
   unless the branch is explicitly the dedicated formatter pass.
5. Add a small before/after note to the commit body showing that long
   signatures keep comments near the declaration.

Acceptance:

- Long parameter lists break across multiple lines.
- Leading comments remain near the declaration they explain.
- `Run.h` no longer formats into ultra-wide function declarations.
- `tools\validate_format.bat` catches UI and future module-folder files.

Validation:

- `tools\validate_format.bat`.
- `tools\validate_build.bat Profile`.

### Slice 5: Source Build Repair

Tasks:

1. Build Profile.
2. Fix any stale include path missed by the map.
3. Fix any source file whose comment tooling expects the old filename.
4. Do not introduce behavior changes while repairing path references.

Acceptance:

- Profile build succeeds at `/W4` with zero warnings.
- The diff remains dominated by moves and path references.

Validation:

- `tools\validate_build.bat Profile`.

### Slice 6: Final Rename Gate

Tasks:

1. Run `git status --short --branch`.
2. Inspect `git diff --stat` and confirm the change is rename/path-reference
   dominant.
3. Run the broad runtime gate because `Run*`, project metadata, and core headers
   are touched.
4. Record output in `TestOutput/agent_logs/`.

Required PR-bound validation:

```bat
tools\validate_full.bat
```

Acceptance:

- Validation succeeds.
- No DX12 validation errors are introduced.
- Physics deterministic baseline still matches because no behavior changed.
- The final staged set contains only this planned work.

## Architecture Refactor After Rename

The rename pass is the foundation. The architecture work starts after it lands
so new files and filters can use the clean names immediately.

### Phase A: Runtime Shell

Target shape:

- `Run.h/cpp` remains the public launch facade.
- New work goes into owned systems:
  - `Runtime/EngineContext`
  - `Runtime/RuntimeCommandQueue`
  - `Runtime/RuntimeViewModel`
  - `Runtime/SceneController`
  - `Runtime/SimulationController`
  - `Runtime/CaptureController`
  - `Runtime/DiagnosticsController`

Rules:

- Keep the renamed `Run` facade as the public launch surface until the runtime
  shell is stable.
- Do not add new feature logic directly to `RunInput.cpp` or `RunScene.cpp`.
- UI and raw hardware input emit commands; runtime systems consume commands in
  the current order.

Validation:

- `tools\validate_full.bat`.

### Phase B: Physics Playground Core

Target shape:

- `PhysicsScene` owns deterministic physics state.
- `PhysicsBodyStore` owns body transforms, velocities, masses, and sleep flags.
- `ColliderStore` owns shapes and collision material metadata.
- `RenderInstanceStore` owns render-facing transforms/material handles.
- `GameModelCollection` becomes a compatibility adapter until stores become
  authoritative.

Rules:

- Preserve body order and replay body IDs.
- Preserve floating-point operation order inside solver-sensitive loops.
- Keep `PhysicsWorld::RunPhysics()` behavior stable until store migration is
  separately validated.
- Add snapshots for replay and SkullScope before changing solver storage.

Validation:

- `tools\validate_physics.bat`.
- Add `tools\validate_perf.bat` for storage or hot-loop changes.

### Phase C: Render Snapshot And Pipeline

Target shape:

- `RenderPipeline` consumes immutable `RenderSceneSnapshot`.
- Runtime no longer passes `GameModelCollection&` through render passes.
- Render graph pass callbacks are introduced only after snapshot output matches
  the existing pass order.

Rules:

- DX12 remains the only runtime renderer.
- Renderer-facing interfaces stay in engine terms, not D3D12 terms.
- Resource ownership and barrier changes happen in focused DX12 slices.

Validation:

- `tools\validate_dx12_renderer.bat`.
- Use `tools\validate_full.bat` if runtime lifecycle changes too.

### Phase D: Editor And Replay Tools

Target shape:

- `EditorTools` owns placement, selection, gizmos, viewport look, and scene save
  commands.
- `LauncherTools` owns raycast, projectile, laser, and repro snapshot behavior.
- `ReplayTools` owns scrubber, cause tree, prediction, velocity edit, restore,
  and visualizers.

Rules:

- Tools mutate the world through commands or explicit subsystem APIs.
- Replay prediction remains contiguous because it temporarily mutates live
  solver/model state.
- Do not mix editor extraction with physics storage changes.

Validation:

- `tools\validate_full.bat`.
- Add `tools\validate_physics.bat` for replay restore or prediction changes.

### Phase E: Physical Folder Layout

Do this only after the rename foundation and first subsystem extractions prove
stable.

Suggested final folders:

```text
SkullbonezSource/
  Core/
  Runtime/
    Scene/
    Replay/
    Editor/
  Assets/
  Scene/
  Physics/
    Debug/
  Rendering/
    DX12/
  World/
  Maths/
  UI/
```

Rules:

- Move files physically only when their ownership is already clear.
- Update `.vcxproj`, `.vcxproj.filters`, Agentic test projects, includes, and
  docs in the same slice.
- Filters should mirror physical folders once folders exist.

Validation:

- `tools\validate_full.bat` for broad movement.
- Add subsystem-specific gates when behavior changes.

### Phase F: Dead Code And Comment Pass

This phase happens at the end, after file names, filters, folders, formatter
policy, and subsystem ownership are stable. Do not perform a full comment pass
before the architecture settles, because moved or deleted code would create
avoidable churn.

Dead-code cleanup tasks:

1. Remove uncalled functions, obsolete compatibility helpers, deprecated
   comments, and unused variables only when they are proven unused.
2. Use compiler warnings, linker/build failures, `rg`, project references,
   call-site searches, and Visual Studio find-references style evidence.
3. Do not remove CLI flags, scene directives, validation hooks, debug-only
   helpers, or replay/SkullScope paths just because they are not obvious from
   ordinary runtime flow.
4. Treat functions used by validation scripts, scene data, shader reflection,
   exported tooling, or debug builds as live unless proven otherwise.
5. Remove dead declarations and definitions in the same slice.
6. Remove now-unused includes after code deletion.

Comment-style cleanup tasks:

1. Load `Agentic/Reference/comment-style-guide.md`.
2. Use `Agentic/Skills/comment-style-audit/skill.md` as the repeatable
   procedure.
3. Audit the full source tree, not just touched files, because this is an
   explicit whole-codebase cleanup request.
4. Ensure file learning headers use the renamed filenames.
5. Replace restatement comments with `Concept:`, `Why:`, `Invariant:`,
   `Lifetime:`, or `Hazard:` comments where the guide calls for them.
6. Define local acronyms in rendering, DX12, physics, scene, replay, UI, and
   tools files.
7. Move comments near the code they explain. For multiline function
   declarations, prefer a leading comment directly above the declaration rather
   than a trailing comment pushed beyond the visible page.
8. Keep useful teaching comments. Do not rewrite good comments just to make the
   diff look comprehensive.

Acceptance:

- No compiler-visible unused variables remain in touched code.
- Deleted code has a named reason in the commit body.
- A reviewer can open a module and learn purpose, mental model, local glossary,
  invariants, and nearby ownership.
- Long function declarations keep comments visible near the declaration.
- The final comment pass does not introduce behavior changes.

Validation:

- Comment-only changes require no repository validation.
- Dead-code removal requires at least `tools\validate_build.bat Profile`.
- Broad dead-code cleanup after architecture movement uses
  `tools\validate_full.bat`.
- If removed code is in physics, renderer, shader, or validation tooling, add
  the owning validation gate from `AGENTS.md`.

## Risks And Mitigations

| Risk | Mitigation |
|------|------------|
| Handoff is not clean | Stop before editing and ask for a clean workspace, because this plan assumes clean handoff. |
| Include update misses UI relative paths | Apply a generated rename map to both `"Header.h"` and `"../Header.h"` forms. |
| Visual Studio project compiles wrong paths | Validate every `ClCompile`/`ClInclude` item exists on disk after the update. |
| Filters become misleading | Use the required module filter table and review the Solution Explorer grouping as an acceptance criterion. |
| Rename hides behavior edits | Keep the first PR rename/type-update-only except required path comments and constructor/destructor changes. |
| Formatter preserves the bad one-line signature style | Remove `collapse_params.py` from the format path and set a real column limit before broad formatting. |
| Comments drift away from declarations | Prefer leading comments above multiline signatures and keep trailing comments only when they remain visually close. |
| Dead-code removal deletes hidden tool paths | Prove unused status through search/build/reference evidence and preserve CLI, scene, validation, debug, replay, and SkullScope entry points unless clearly dead. |
| Windows path case quirks cause false confidence | Remove a real prefix rather than only changing case; assert old paths are gone with `rg --files`. |
| Historical docs become noisy | Update current operational docs and leave old audits/plans alone unless they are actively referenced. |
| Broad validation is skipped | Treat rename plus `Run*`/project metadata touches as `validate_full` at PR gate. |

## Overnight Worker Checklist

1. Read root `AGENTS.md`, `README.md`, `Agentic/README.md`, and
   `Agentic/SessionState.md`.
2. Run `git status --short --branch`; stop if the workspace is not clean.
3. Use the orchestrator skill for implementation coordination.
4. Generate and review the source/header rename map.
5. Generate and review the ordinary type rename map.
6. Move files with `git mv`.
7. Apply type renames while preserving SkullbonezCore identity symbols.
8. Update includes, active file header paths, project files, filters, and Agentic
   test project paths.
9. Verify `.vcxproj.filters` grouping matches the module table in this plan.
10. Fix formatter policy before running any broad formatting:
   - remove `collapse_params.py` from the format path,
   - set a real column limit,
   - make long declarations and calls multiline,
   - ensure format scripts recurse into UI and future module folders.
11. Run a targeted Profile build while repairing path references.
12. Run `tools\validate_format.bat`.
13. Run `tools\validate_full.bat` as the PR-bound final gate.
14. After architecture movement, run the final dead-code cleanup and full
    comment-style audit as a separate endgame slice.
15. Commit on the feature branch with a detailed message that explains:
    - file prefix removal scope,
    - type prefix removal scope,
    - SkullbonezCore identity preserved,
    - project/filter updates,
    - formatter policy fix,
    - dead-code cleanup scope when applicable,
    - comment-style audit scope when applicable,
    - clean worktree confirmation,
    - validation command and result.
16. Push the feature branch when ready. Do not merge or submit a PR unless the
    user explicitly asks.

## Success Criteria

Near-term:

- No active C++ source/header filename begins with `Skullbonez`.
- No active source include points at a removed prefixed filename.
- Ordinary engine classes and structs no longer carry the redundant
  `Skullbonez` prefix unless an exception is documented.
- `SkullbonezCore`, `SKULLBONEZ_CORE`, product branding, namespaces, macros,
  file formats, config keys, and executable identity remain intact.
- `SKULLBONEZ_CORE.vcxproj` and `.filters` reference the renamed files.
- Visual Studio filters are meaningful and module-aligned.
- The formatter breaks long declarations and calls so comments stay near the
  code they explain.
- Profile build and `tools\validate_full.bat` pass.
- First implementation commit contains no intentional behavior changes.

Long-term:

- `Run` is a small launch/frame facade, not the owner of every system.
- Physics is the engine center: deterministic stepping, replay, snapshots,
  SkullScope, debug visualizers, and scene iteration are first-class.
- Rendering consumes snapshots and graph-owned pass/resource intent.
- Editor and replay tools have obvious homes outside central runtime files.
- New files use clean names from birth.
- Deprecated or uncalled code is removed only after proof and validation.
- The whole codebase has passed the repository comment-style audit once the
  refactor shape is stable.
