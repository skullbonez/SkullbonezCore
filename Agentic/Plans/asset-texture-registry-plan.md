# Asset And Texture Registry Plan

Status: planning draft  
Created: 2026-06-11  
Scope: source asset records, texture registry, shader asset records, backend GPU resource rebuilds, scene/style asset references  
Implementation status: plan only, no code changes in this pass

## Goal

Create a small asset and texture registry that separates source asset identity from backend GPU resources.

This is needed for:

- renderer hot switching,
- material system texture references,
- terrain texture overrides,
- style files,
- shader preload/cache,
- future hot reload,
- cleaner validation artifacts.

## Current Read

Existing pieces:

- `SkullbonezTextureCollection` maps hashed texture IDs to backend texture handles.
- `Gfx().CreateTexture2D` returns backend-specific opaque handles.
- Shader creation is by base path, such as `shaders/lit_textured`.
- `Assets::AssetSystem` scaffold exists and has started path/source-record work.
- Terrain/core texture path resolution has started moving through asset scaffolding.
- Style files already want render-look data and material assignments.

Current pain:

- Texture identity and GPU handle lifetime are closely coupled.
- Scene/style files cannot cleanly refer to arbitrary texture assets yet.
- Renderer switch requires every system to know how to rebuild its GPU handles.
- Shader source and compiled backend object are not tracked as an asset pair.

## Design Principles

1. Source asset records survive renderer switching.
2. GPU resources are backend-specific and disposable.
3. Asset handles should be stable inside a run.
4. Asset loading should be explicit and logged.
5. Keep the first registry small.
6. Do not require async loading for v1.
7. Do not rewrite all texture code at once.

## Asset Types

```cpp
enum class AssetKind
{
    Texture2D,
    ShaderProgram,
    MeshSource,
    MaterialPreset,
    StyleFile,
    SceneFile
};
```

### Texture Source Record

```cpp
struct TextureSourceAsset
{
    AssetId id;
    char logicalName[64];
    char path[MAX_PATH];
    bool generateMips;
    bool linearFilter;
    int channelsHint;
};
```

### GPU Texture Record

```cpp
struct GpuTextureAsset
{
    AssetId sourceId;
    uint32_t backendHandle;
    int width;
    int height;
    int channels;
    uint64_t sourceTimestamp;
};
```

### Shader Source Record

```cpp
struct ShaderSourceAsset
{
    AssetId id;
    char baseName[128]; // e.g. shaders/lit_textured
    ShaderProgramKind kind;
    ShaderProgramDesc contract;
};
```

### Shader GPU Record

```cpp
struct GpuShaderAsset
{
    AssetId sourceId;
    std::unique_ptr<IShader> shader;
    uint64_t sourceHash;
};
```

## Texture Handle Compatibility

Do not break current code immediately. Add a compatibility layer:

- Existing code can still use numeric texture constants.
- Registry maps those constants to source asset records.
- `TextureCollection::GetTextureHandle(hash)` can eventually delegate to registry.

Example:

```cpp
AssetId sphereTexture = Assets().FindTexture("bounding_sphere");
uint32_t gpuHandle = RenderAssets().GetTextureHandle(sphereTexture);
Gfx().BindTexture(gpuHandle, 0);
```

## Scene And Style Asset References

Future syntax:

```text
terrain_texture textures/ground_dry.jpg
sphere_texture textures/ball_highres.png
material chrome texture=textures/chrome_mask.png
style low_poly_art_style
```

Rules:

- Relative paths resolve under `SkullbonezData/`.
- Style files may reference textures.
- Scene-local texture directives override style defaults.
- Missing textures fail clearly with path and directive name.

## Shader Asset Handling

Shader registry should track:

- base name,
- GL source files,
- HLSL source file,
- DXR source/bytecode where applicable,
- shader contract,
- source timestamps/hashes.

V1:

- registry creates shaders through existing `Gfx().CreateShader`.
- no compiled bytecode cache.

V2:

- DX11/DX12 shader bytecode cache keyed by source hash and compile flags.
- GL source hash for hot reload or diagnostics.
- DXR `.dxil` rebuild metadata.

## Phase Plan

### Phase 1: Source Asset Registry

Tasks:

1. Extend or formalize `Assets::AssetSystem`.
2. Add records for known built-in textures.
3. Add records for shader base names.
4. Keep backend GPU handle ownership unchanged.

Validation:

- `tools\validate_fast.bat` if parser/runtime behavior is not visible.

### Phase 2: Texture Registry Bridge

Tasks:

1. Let `SkullbonezTextureCollection` resolve through asset records.
2. Preserve existing hash constants.
3. Add debug asset dump:
   - logical name,
   - source path,
   - backend handle,
   - dimensions.

Validation:

- `tools\validate_renderers.bat`.

### Phase 3: Scene/Style Texture Directives

Tasks:

1. Add terrain texture override directive.
2. Add sphere/object texture override only if material v1 needs it.
3. Validate paths during scene/style load.
4. Keep core default textures unchanged.

Validation:

- Parser-only: `tools\validate_fast.bat`.
- Visible texture changes: `tools\validate_renderers.bat`.

### Phase 4: GPU Resource Rebuild From Source Records

Tasks:

1. On renderer switch, discard GPU texture records.
2. Recreate GPU texture handles from source records.
3. Rebind systems that store texture handles.
4. Tie into resource lifetime reset order.

Validation:

- `tools\validate_renderers.bat`.
- Hot-switch manual check if not covered.

### Phase 5: Shader Registry

Tasks:

1. Register shader source base names and contracts.
2. Create shaders through registry.
3. Add optional preload for known shader set.
4. Add diagnostics for missing backend files.

Validation:

- `tools\validate_renderers.bat`.

### Phase 6: Shader/Texture Hot Reload

Tasks:

1. Track timestamps/hashes.
2. Add manual reload command or dev-only key.
3. Rebuild affected GPU resources only.
4. Keep validation path deterministic by default.

Validation:

- `tools\validate_renderers.bat`.

## Validation Matrix

| Change | Validation |
|--------|------------|
| Docs only | No validation required |
| Source registry with no visible behavior | `tools\validate_fast.bat` |
| Texture loading path changes | `tools\validate_renderers.bat` |
| Scene/style texture directives | `tools\validate_fast.bat`, renderer validation if visible |
| Renderer-switch GPU rebuild | `tools\validate_renderers.bat` |
| Shader creation registry | `tools\validate_renderers.bat` |
| Hot reload | `tools\validate_renderers.bat` |

## Risks

| Risk | Mitigation |
|------|------------|
| Texture handles become stale after switch | Source records survive, GPU records rebuild, consumers update handles. |
| Asset registry becomes too broad | Start with texture and shader records only. |
| Scene path behavior changes | Keep relative path rules explicit and test parser errors. |
| Startup time increases | Lazy-load initially; preload only known core assets if needed. |
| Renderer parity changes from filtering defaults | Preserve existing mip/filter defaults per texture. |

## Success Criteria

- Texture source identity is separate from backend handle identity.
- Renderer switch rebuilds GPU resources from source records.
- Scene/style files can eventually reference texture assets cleanly.
- Shader source assets have contracts and clear backend file expectations.
