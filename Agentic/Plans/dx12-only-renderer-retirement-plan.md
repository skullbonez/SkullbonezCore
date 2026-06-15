# DX12-Only Renderer Retirement Plan

Status: active
Created: 2026-06-15
Scope: retire OpenGL/DX11, make DX12 validation independent, clean DX12 architecture, preserve future Vulkan/Metal portability
Implementation status: Phase 6 complete on branch `codex/dx12-only-renderer-retirement`; next work starts Phase 7 render-device interface cleanup

## Goal

Move SkullbonezCore from a tri-renderer parity engine to a DX12-only production
engine without painting the architecture into a Direct3D-only corner.

The desired end state is:

- DX12 is the only compiled and validated renderer.
- OpenGL and DX11 code, shaders, launch options, validation launches, and stale
  baselines are removed deliberately.
- DX12 validation no longer depends on comparing against legacy renderers.
- Renderer concepts stay engine-owned and backend-neutral enough that a future
  Vulkan or Metal backend could map into them.
- New shader, material, water, render-pipeline, and render-graph work can target
  DX12 first without keeping GL/DX11 in sync.

This is a retirement plan, not a rewrite plan. The safest route is to preserve
the useful boundaries learned from the parity era, then remove the legacy
implementations after DX12 has its own trustworthy test gate.

## Why Now

The old renderer parity stack has done its job:

- It caught clip-space, matrix, shader, and state drift during the DX12
  migration.
- It kept visual behavior honest while shader paths were duplicated.
- It forced renderer-neutral naming for many runtime resources.

It is now becoming a tax on the next work:

- Water cleanup must preserve three blending/depth/reflection implementations.
- Material work must update GLSL, DX11 HLSL, and DX12 HLSL together.
- Render pipeline extraction has to keep hot-switch behavior alive even when the
  product renderer is DX12.
- Future Vulkan/Metal support is better served by clean engine contracts than by
  carrying old GL/DX11 code as accidental abstraction.

## Guiding Principles

1. Remove legacy renderers only after DX12 has an independent validation gate.
2. Archive one final GL/DX11/DX12 parity snapshot before deletion.
3. Do not collapse engine-facing APIs into raw D3D12 types.
4. Keep scene, style, material, shader-contract, and render-pass data free of
   D3D12 handles, descriptor indices, root parameters, or COM types.
5. Let DX12 be the first and only implementation for now.
6. Preserve backend-neutral names where they describe real engine concepts:
   textures, render targets, passes, frame constants, materials, buffers,
   pipeline states, barriers, and device reset.
7. Delete compatibility code that exists only to make GL/DX11 match DX12.
8. Keep validation discipline at least as strong as the parity era.

## Definitions

| Term | Meaning |
|------|---------|
| Production renderer | DX12, the runtime path users and agents should target. |
| Legacy parity renderers | OpenGL 3.3 and DX11 before removal. |
| Engine render contract | Backend-neutral data and operations the engine owns, such as `RenderFrameContext`, `RenderMaterial`, `RenderTargetHandle`, and pass inputs. |
| Backend mapping | The DX12 implementation detail that maps engine contracts to root signatures, descriptor heaps, resources, barriers, PSOs, and command lists. |
| Future backend | A possible Vulkan or Metal implementation added later against the engine render contract. |

## Non-Goals

- Do not implement Vulkan or Metal in this plan.
- Do not keep GL/DX11 as hidden test-only renderers after retirement.
- Do not introduce a large cross-platform graphics framework.
- Do not convert every DX12 system to an abstract virtual interface before there
  is a second modern backend.
- Do not change visual style, water behavior, material semantics, or pass order
  during the retirement slice unless a small validation fix is required.
- Do not make the first cleanup a root-signature or shader-language migration.

## Readiness Gates

Do not remove GL/DX11 until these gates pass:

1. DX12 can run the renderer suite without launching GL or DX11.
2. DX12 captures compare against committed DX12 baselines, not against GL.
3. DX12 InfoQueue validation is captured and checked for zero errors.
4. Screenshot and artifact manifests clearly identify renderer, scene, frame,
   camera, config, and baseline path.
5. The current GL/DX11/DX12 parity suite has one final archived run.
6. README, AGENTS, SessionState, and validation docs agree that DX12 is the only
   active renderer after the cutover.

## Target Architecture After Retirement

### Keep Engine Contracts Backend-Neutral

The engine should keep types like:

```cpp
struct RenderFrameContext;
struct RenderMaterial;
struct RenderTargetDesc;
struct TextureDesc;
struct BufferDesc;
struct ShaderProgramDesc;
struct RenderPassInput;
struct RenderPassOutput;
```

Those types describe what the engine wants. They should not expose:

- `ID3D12Resource*`,
- descriptor heap CPU/GPU handles,
- root parameter indices,
- `DXGI_FORMAT` outside DX12-owned translation code,
- D3D12 barrier structs,
- HLSL register binding details in scene/style data.

DX12-specific code can use D3D12 types internally. The important boundary is
that scene loading, material authoring, pass scheduling, and gameplay systems do
not learn D3D12 details.

### Prefer A Thin Backend Mapping Over Premature Abstraction

After GL/DX11 are removed, there is no immediate need for every DX12 subsystem
to be hidden behind virtual interfaces. That would add ceremony without a second
implementation to prove it.

Use this split instead:

| Layer | Owns |
|-------|------|
| Engine render layer | pass order, frame context, material/style data, render target descriptions, shader contracts, resource lifetime events |
| DX12 backend layer | device, swap chain, command lists, fences, descriptors, resource states, PSOs, uploads, readbacks, InfoQueue/DRED |
| Future backend adapter | maps the same engine render layer to Vulkan descriptors/render passes or Metal argument buffers/render encoders |

### Preserve Source Asset Versus GPU Resource Separation

This is the most important portability rule.

Source asset records survive device reset and future backend changes:

- texture path,
- shader base name,
- material name,
- mesh source data,
- scene/style directives.

GPU resources are backend-owned:

- DX12 resources/descriptors,
- future Vulkan images/views/buffers/descriptors,
- future Metal textures/buffers/pipeline states.

Renderer retirement should remove GL/DX11 GPU resources, not collapse source
asset state into DX12 objects.

### Make HLSL Canonical But Not D3D12-Authored

HLSL can be the canonical shader language for DX12 and future shader contracts.
That does not mean scene/material data should speak in D3D12 register slots.

Recommended shape:

- HLSL/DXC reflection is the production source of shader metadata.
- `ShaderProgramDesc` records engine-level bindings:
  - frame constants,
  - material constants,
  - instance data,
  - texture inputs,
  - pass-local resources,
  - samplers.
- DX12 maps those bindings to root signatures and descriptor tables.
- A future Vulkan backend could map the same metadata to SPIR-V descriptor sets.
- A future Metal backend could map it to MSL/argument buffers through a later
  translation or backend-specific compile path.

Do not pick the Vulkan/Metal shader toolchain in this retirement plan. Just keep
the metadata clean enough that the decision remains open.

## Phase Plan

### Phase 0: Retirement Decision And Freeze

Tasks:

1. [x] Add this plan to the active roadmap.
2. [x] Declare GL/DX11 feature work frozen except for final parity validation fixes.
3. [x] Update `Agentic/SessionState.md` to say renderer retirement is the next
   render architecture milestone once approved.
4. [x] Update water/material/render-pipeline plans to defer code-heavy phases until
   after DX12-only validation exists.
5. [x] Record the branch name for implementation:
   `codex/dx12-only-renderer-retirement`.

Validation:

- Documentation only: no validation required.

### Phase 1: Build A DX12-Only Renderer Validation Gate

Goal:

DX12 must be testable without GL/DX11.

Tasks:

1. [x] Add or update a validation entry point such as:
   - `tools\validate_dx12_renderer.bat`, or
   - a `dx12` mode in `tools\validate_select.bat`.
2. [x] Keep `tools\validate_renderers.bat` temporarily as the legacy parity gate
   until final removal.
3. [x] Make the DX12 gate:
   - build `Profile`,
   - launch DX12 render scenes,
   - capture screenshots,
   - compare against DX12 baselines,
   - save an artifact manifest,
   - collect `dx12_validation.txt`,
   - fail if DX12 validation errors are non-zero.
4. [x] Rename or supplement `tools\check_parity.py` with a DX12 baseline comparator
   if the current script assumes GL/DX11 comparison.
5. [x] Ensure renderer launch timeouts stay PID-scoped.
6. [x] Add a compact summary that can be pasted into commit notes.

Validation:

- `tools\validate_fast.bat` passed on 2026-06-15.
- `tools\validate_dx12_renderer.bat` passed on 2026-06-15 after refreshing the
  two DX12 baselines to the current `engine.cfg` capture size.

Acceptance:

- A DX12 screenshot regression can fail without involving GL/DX11.
- A DX12 validation-layer error can fail the gate.
- The artifact manifest is useful enough for future reports.

### Phase 2: Capture Final Legacy Parity Evidence

Goal:

Retain one durable reference showing where GL/DX11 ended.

Tasks:

1. [x] Run the current full renderer parity gate before deletion.
2. [x] Store the manifest path and summary in the retirement PR notes.
3. [x] Optionally add a short report under `Agentic/Reports` or `Agentic/Audits`
   with:
   - date,
   - commit SHA,
   - GL/DX11/DX12 screenshot results,
   - average pixel diffs,
   - DX12 validation log status,
   - known acceptable differences.
4. [x] Do not keep GL/DX11 code solely to regenerate this artifact later.

Validation:

- `tools\validate_renderers.bat` passed on 2026-06-15.

Report:

- `Agentic/Reports/2026-06-15/final-legacy-renderer-parity/report.md`

Acceptance:

- The project has a final known-good parity artifact before renderer removal.

### Phase 3: Remove Runtime Renderer Selection

Goal:

Make DX12 the only runtime renderer path while keeping the code buildable after
each small slice.

Tasks:

1. [x] Remove or deprecate command-line choices:
   - `--renderer gl`,
   - `--renderer dx11`.
2. [x] Keep `--renderer dx12` accepted as a no-op or compatibility alias for one
   release window if useful.
3. [x] Remove UI renderer-switch controls or make them display-only DX12 state.
4. [x] Remove runtime hot-switch behavior that exists only for GL/DX11.
5. [x] Preserve device reset/resource rebuild concepts for DX12 resize, device loss,
   shader reload, and future backend bring-up.
6. [x] Update runtime reference docs and examples.

Validation:

- `tools\validate_fast.bat` passed on 2026-06-15 after validation-tool updates.
- `tools\validate_full.bat` passed on 2026-06-15.
  - DX12 renderer manifest:
    `TestOutput\validation\dx12_renderer\20260615T034906Z\manifest.json`
  - DX12 InfoQueue validation errors: 0.
  - Physics CSV baselines were byte-exact.
  - SkullScope query baseline now records `DirectX 12` as the renderer.
  - Perf validation runs DX12-only scenes.

Acceptance:

- The app starts in DX12 without renderer selection ambiguity.
- Old renderer CLI requests fail clearly; `--renderer dx12` remains a
  compatibility alias.
- The in-game renderer selector is display-only DX12 state.
- Runtime renderer hot-switching is removed.
- DX12 resource reset/rebuild concepts remain isolated for later cleanup.

### Phase 4: Remove GL And DX11 Backends

Goal:

Delete the retired implementations and their project/build references.

Tasks:

1. [x] Remove backend files:
   - `SkullbonezRenderBackendGL.*`,
   - `SkullbonezRenderBackendDX11.*`,
   - GL/DX11-only helper files if any.
2. [x] Remove GLAD and OpenGL-specific third-party source if no longer used.
3. [x] Remove DX11-specific includes, libraries, annotations, and project entries.
4. [x] Remove backend factory cases for GL/DX11.
5. [x] Remove no-op compatibility methods that existed only because `IRenderBackend`
   had to serve three backends.
6. [x] Keep backend-neutral names such as `ResetRenderResources` and
   `RenderCapabilities` where they still describe real concepts.
7. [x] Remove dead renderer-switch tests and scenes only if they have no DX12 value.
8. [x] Update `.vcxproj` and `.vcxproj.filters` carefully.

Validation:

- `tools\validate_full.bat` passed on 2026-06-15 after replacing the accidental
  `d3d11.lib` shader-reflection dependency with `dxguid.lib`.
  - DX12 renderer manifest:
    `TestOutput\validation\dx12_renderer\20260615T040700Z\manifest.json`
  - DX12 InfoQueue validation errors: 0.
  - DX12 screenshots matched committed baselines.
  - Physics, SkullScope, and perf phases passed.

Acceptance:

- The project builds without GL/DX11 source files or libraries.
- No source includes GL/DX11 backend headers.
- DX12 validation gate passes.

### Phase 5: Remove Legacy Shader Families

Goal:

Stop maintaining GLSL and DX11-only shader duplicates.

Tasks:

1. [x] Inventory every shader file before deletion.
2. [x] Remove active GLSL files once GL is gone:
   - `.vert`,
   - `.frag`,
   - GL-only debug shader families.
3. [x] Keep HLSL files needed by DX12:
   - raster HLSL,
   - `generate_mips.hlsl`,
   - `reflect.rt.hlsl`,
   - checked-in DXIL artifacts with documented rebuild rules.
4. [x] Remove DX11-only shader assumptions if they differ from DX12.
5. [x] Update shader inventory docs so HLSL is canonical production source.
6. [x] Add or preserve shader contract metadata in engine terms, not D3D12 terms.

Validation:

- `python tools\validate_shaders.py` passed on 2026-06-15 with 0 errors and 7
  existing contract-completeness warnings.
- `tools\validate_dx12_renderer.bat` passed on 2026-06-15.
  - DX12 renderer manifest:
    `TestOutput\validation\dx12_renderer\20260615T041546Z\manifest.json`
  - DX12 InfoQueue validation errors: 0.
  - `water_ball_test`: avg_diff=0.0000, max_diff=0, pixels_over_10=0.
  - `solver_smoke`: avg_diff=0.0006, max_diff=170, pixels_over_10=7.
- `tools\validate_fast.bat` passed on 2026-06-15 because
  `tools\shader_contracts.json` changed.

Acceptance:

- No active runtime path loads GLSL.
- Shader docs explain which HLSL files are raster, compute, or DXR.
- Future shader work no longer has to update duplicate GLSL/DX11 sources.

### Phase 6: Rewrite Renderer Validation Documentation And Baselines

Goal:

Make repository instructions match the new renderer reality.

Tasks:

1. [x] Update `AGENTS.md`:
   - DX12 is the only active renderer.
   - Remove GL/DX11 parity language.
   - Replace parity validation requirements with DX12 screenshot and validation
     log requirements.
   - Keep strict rules for DX12 resource barriers, descriptors, uploads, and
     shader changes.
2. [x] Update `README.md` launch examples.
3. [x] Update `Agentic/SessionState.md`.
4. [x] Update validation table mappings:
   - renderer backend/shader changes use the new DX12 renderer gate,
   - broad runtime changes still use `tools\validate_full.bat`,
   - performance changes still use `tools\validate_perf.bat`.
5. [x] Remove obsolete GL/DX11 baselines or archive them under a final parity report.
6. [x] Rename validation artifacts from parity language to DX12 regression language.

Validation:

- `python -m py_compile tools\archive_validation_artifacts.py tools\update_baselines.py tools\validate_concepts.py tools\check_ui_blur.py` passed on 2026-06-15.
- `git diff --check` passed on 2026-06-15.
- `tools\capture_ui_screenshot.bat dx12 Profile\codex_ui_capture_phase6.png 720` passed on 2026-06-15.
- `tools\validate_fast.bat` passed on 2026-06-15.
- `tools\validate_ui.bat`, `tools\validate_ui_stress.bat`, and
  `tools\validate_demo_stress.bat` passed on 2026-06-15.
- `tools\validate_full.bat` passed on 2026-06-15.

Acceptance:

- A fresh agent no longer tries to run GL/DX11.
- Commit notes have a clear DX12 validation command to cite.

### Phase 7: Simplify `IRenderBackend` Into A DX12-Facing Render Device

Goal:

Remove abstraction scars while preserving future backend seams.

Tasks:

1. Review every method on `IRenderBackend`.
2. Classify each method:
   - core engine render contract,
   - DX12 implementation detail,
   - legacy GL/DX11 compatibility leftover,
   - future-backend useful concept.
3. Delete legacy-only no-ops and compatibility flags.
4. Rename the surviving engine-facing surface if useful:
   - `IRenderBackend` can stay if it remains clean,
   - or split into `RenderDevice`, `RenderResourceManager`, and pass-specific
     services if the interface is still too broad.
5. Move DX12-only operations behind DX12-owned subsystem types:
   - device/frame,
   - descriptors,
   - uploads/readbacks,
   - pipeline states,
   - render graph/barrier diagnostics,
   - DXR reflection.
6. Keep pass code using engine-level handles and descriptions.

Validation:

- `tools\validate_full.bat` for broad render API movement.

Acceptance:

- The public render surface is smaller than the old tri-renderer interface.
- DX12 implementation details are easier to find and review.
- Future Vulkan/Metal backend authors would know which contracts they must
  implement.

### Phase 8: Clean DX12 Around Modern Backend Concepts

Goal:

Use the retirement to make DX12 cleaner, not just smaller.

Tasks:

1. Confirm DX12 resource ownership is split into focused subsystems:
   - `Dx12RenderDevice`,
   - descriptor allocators,
   - upload allocator,
   - readback manager,
   - PSO/root-signature cache,
   - render graph diagnostics,
   - DXR reflection resources.
2. Make transient descriptor reset and upload allocator reset explicitly
   frame/fence safe.
3. Keep render graph transition diagnostics active until graph-owned barriers
   replace hand-written barriers pass by pass.
4. Record DX12 object names for all important resources.
5. Keep DRED and InfoQueue diagnostics easy to find in artifacts.
6. Remove GL/DX11-oriented workaround code from matrix, sampler, texture, and
   shader paths.
7. Replace old parity comments with DX12 validation comments where needed.

Validation:

- `tools\validate_dx12_renderer.bat` or updated renderer gate.
- `tools\validate_perf.bat` if upload/descriptor/render hot paths change.
- Run DX12-heavy scenes three consecutive times if barriers, upload lifetime, or
  descriptor lifetime are touched.

Acceptance:

- DX12 is smaller and more explicit than before retirement.
- Validation artifacts make GPU lifetime bugs visible.

### Phase 9: Establish Future Vulkan/Metal Portability Contracts

Goal:

Document what a future modern backend must implement without adding it now.

Tasks:

1. Add a reference doc such as:
   `Agentic/Reference/render-backend-portability-contract.md`.
2. Define engine-level concepts:
   - resource handles,
   - texture/buffer descriptions,
   - shader program descriptions,
   - vertex layouts,
   - render target descriptions,
   - pass inputs/outputs,
   - synchronization/barrier intent,
   - debug markers,
   - capture/readback support.
3. Include mapping notes:
   - DX12: root signatures, descriptor heaps, barriers, command lists.
   - Vulkan: descriptor sets, pipeline layouts, image layouts, command buffers.
   - Metal: argument buffers/resources, render encoders, command buffers.
4. Mark optional features:
   - raytracing,
   - GPU timers,
   - debug lines,
   - screenshots/readback,
   - compute mip generation.
5. State that future backend support must start from this contract, not from
   resurrecting GL/DX11 code.

Validation:

- Documentation only: no validation required.

Acceptance:

- Removing GL/DX11 does not mean the engine has no portability story.
- Vulkan/Metal remain future backend mappings, not active maintenance burdens.

### Phase 10: Resume Deferred Rendering Work

Goal:

Use the simpler DX12-only base for the work that was too expensive under parity.

Recommended order after retirement:

1. Water rendering cleanup:
   - water shader inventory,
   - reflection input contract,
   - water style params,
   - water pass extraction,
   - water intersection bug investigation.
2. Shader architecture cleanup:
   - HLSL contract metadata,
   - pass binders,
   - missing-input diagnostics.
3. Material system v1:
   - typed CPU render materials,
   - style/scene material mapping,
   - DX12 instance payload update.
4. Render pipeline extraction:
   - frame context,
   - reflection/sky/object/terrain/shadow/water/post passes.
5. Render graph ownership:
   - move the first low-risk pass under graph-owned barriers.

Validation:

- Renderer and shader changes use the new DX12 renderer gate.
- Broad runtime/pass extraction uses `tools\validate_full.bat`.
- Hot-path material/upload changes add `tools\validate_perf.bat`.

## File And System Removal Checklist

Use this as a search checklist during implementation:

| Area | Search Terms |
|------|--------------|
| Renderer selection | `--renderer`, `RendererType`, `gl`, `dx11`, `dx12`, `OpenGL`, `DirectX 11` |
| Backend files | `RenderBackendGL`, `RenderBackendDX11`, `GLAD`, `wgl`, `D3D11`, `DXGI_SWAP_EFFECT` |
| Shader files | `.vert`, `.frag`, `debug_line`, GL-only shader comments |
| Validation | `validate_renderers`, `check_parity`, `pixel diff`, `parity`, `gl.png`, `dx11.png` |
| Docs | `legacy parity`, `OpenGL 3.3`, `DX11`, `GL/DX11 parity` |
| Runtime switching | `ResetGLResources`, renderer hot-switch UI, backend rebuild steps |
| Project files | `.vcxproj`, `.vcxproj.filters`, OpenGL/DX11 libs/includes |

Do not delete solely from search results. Classify first:

- remove,
- rename,
- keep as backend-neutral,
- keep as historical note,
- move to final parity report.

## Validation Matrix

These are PR/commit gates, not commands to run after every edit.

| Change Type | Required Gate |
|-------------|---------------|
| This plan or docs-only updates | No validation required |
| Validation script changes | `tools\validate_fast.bat`, then the changed script |
| New DX12 renderer validation gate | `tools\validate_fast.bat`, then new DX12 renderer gate |
| Final legacy parity archive | `tools\validate_renderers.bat` |
| Runtime renderer selection removal | `tools\validate_full.bat` |
| GL/DX11 backend deletion | `tools\validate_full.bat` |
| Shader family deletion | New DX12 renderer gate; `tools\validate_full.bat` if loader/project behavior changes broadly |
| DX12 descriptors/uploads/barriers/root signatures | New DX12 renderer gate, zero DX12 validation errors, and perf if hot |
| Render API/interface simplification | `tools\validate_full.bat` |
| Performance-sensitive DX12 cleanup | New DX12 renderer gate plus `tools\validate_perf.bat` |

## Risk Register

| Risk | Why It Matters | Mitigation |
|------|----------------|------------|
| DX12-only gate is weaker than parity gate | Visual regressions could slip through | Build committed DX12 screenshot baselines, manifests, and InfoQueue zero-error checks before deletion. |
| Removing GL/DX11 removes useful reference behavior | Debugging convention bugs gets harder | Archive one final parity run and keep historical reports. |
| DX12 details leak into authoring data | Future Vulkan/Metal becomes expensive | Keep scene/style/material/pass contracts backend-neutral. |
| Over-abstracting for hypothetical backends | Code becomes harder without value | Keep DX12 concrete internally; abstract only engine-facing contracts. |
| Project file deletion breaks builds | Large file removal can miss filters/libs | Remove in small slices and validate full build. |
| Shader deletion misses hidden load path | Runtime failure after cleanup | Inventory shader loads with `rg`, keep HLSL canonical, validate DX12 captures. |
| Runtime hot-switch removal breaks reset semantics | Resize/device reset may regress | Preserve resource lifetime events for DX12 reset and future backend migration. |
| Old docs mislead future agents | Agents may keep trying GL/DX11 gates | Update AGENTS, README, SessionState, validation docs in the same retirement branch. |

## Success Criteria

The retirement is complete when:

- The codebase no longer builds or ships OpenGL or DX11 backends.
- The app has one production renderer path: DX12.
- Renderer validation is DX12 screenshot regression plus zero DX12 validation
  errors, not GL/DX11 parity.
- README, AGENTS, SessionState, and validation docs all match the DX12-only
  state.
- Shader sources no longer require GLSL/DX11 duplication.
- DX12 resource lifetime, descriptors, uploads, readbacks, and diagnostics are
  explicit enough to support future render pipeline work.
- Engine render contracts remain backend-neutral enough for a future Vulkan or
  Metal adapter.
- Water/material/render-pipeline cleanup can proceed without legacy renderer
  parity overhead.

## Recommended First Implementation Slice

Start with Phase 1, not deletion.

The first PR should add a DX12-only validation gate while the old renderers still
exist. That gives the project a safety net before the bridge is removed.

Suggested branch:

```text
codex/dx12-only-validation-gate
```

Suggested deliverables:

- a DX12-only renderer validation command,
- DX12 screenshot/baseline artifact manifest,
- DX12 validation log zero-error check,
- documentation explaining how this replaces parity after retirement.

After that gate works, run the final legacy parity archive, then remove GL/DX11
in a separate branch.
