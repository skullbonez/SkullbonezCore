# DX12 Binding ABI

Status: current DX12 ordinary raster contract as of the terrain detail-shadow slice.

Validation: renderer behavior changes require `tools\validate_dx12_renderer.bat`; documentation-only edits to this file do not.

## Ordinary Raster Root Signature

The main DX12 graphics root signature is intentionally small and fixed:

| Root parameter | HLSL register | Engine meaning |
|----------------|---------------|----------------|
| 0 | `b0` | Per-draw constant buffer emitted by `ShaderDX12::FlushCB()` |
| 1 | `t0` | Texture slot 0 from `BindTexture(handle, 0)` |
| 2 | `t1` | Texture slot 1 from `BindTexture(handle, 1)` |
| 3 | `t2` | Texture slot 2 from `BindTexture(handle, 2)` |
| 4 | `t3` | Texture slot 3 from `BindTexture(handle, 3)` |
| 5 | `t4` | Object material table from `BindTexture(handle, 4)` |
| 6 | `t5` | Terrain detail-shadow map from `BindTexture(handle, 5)` |

Static samplers:

| Register | Mode | Common use |
|----------|------|------------|
| `s0` | linear wrap | terrain, object, skybox, regular texture sampling |
| `s1` | linear clamp | framebuffer, depth, reflection, and post-process texture sampling |
| `s3` | point clamp | manual shadow-map PCF sampling |

The public compatibility contract remains:

```cpp
BindTexture(handle, slot)
```

For ordinary raster shaders, `slot` maps directly to HLSL SRV register `t<slot>`.
Current valid slots are `0..5`. Invalid slots are ignored for compatibility and
emit a Debug diagnostic event.

## Shader Contract Implications

`SkullbonezSource/Rendering/ShaderContracts.h` records high-risk runtime shader
contracts. Its resource `slot` values use the ABI above:

- object and terrain base textures use `t0`;
- water reflection textures use `t1`;
- tonemap uses `t0` scene, `t1` depth, and `t2` volumetric;
- optional shadow maps use `t3`.
- the instanced object material-kind default table uses `t4`.
- terrain optionally layers the tight object-shadow map from `t5` without
  changing the `t4` material-table row.

The shader cleanup branch added CPU `RenderMaterial` data, expanded the
instanced object payload to material rows, and added a tiny material texture
table at `t4`. It does not add a structured-buffer material table, bindless
heap, or descriptor-indexing API.

## Descriptor Lifetime

Persistent SRV/UAV descriptors live in the static range:

- loaded textures;
- framebuffer color/depth SRVs;
- DXR reflection UAV/SRV descriptors;
- the generated object material-table texture;
- generated null descriptors.

Per-draw or per-dispatch table rows live in the transient range. The renderer
copies static descriptors into transient shader-visible rows before binding root
descriptor tables. Transient rows are split by frame allocator and reset only
after that allocator's fence has completed.

The descriptor diagnostics report:

- static SRV capacity and use;
- transient SRV capacity per frame;
- transient SRV peak use for the run;
- invalid descriptor index/capacity failures with exact row counts.

## Upload Lifetime

Per-frame upload arenas are fence-aligned with the command allocator and
transient descriptor range. When the backend reuses a frame allocator, it has
already waited for the fence value that protects:

- command allocator memory;
- upload bytes for constants, dynamic vertices, instance data, and texture rows;
- transient descriptor rows bound by command lists using that allocator.

The upload diagnostics report peak bytes and total per-frame capacity at the
renderer architecture boundary. Upload overflow recovery is still handled by
submitting current work, waiting for the GPU, resetting the current frame arena,
and then retrying the allocation.

## Deferred Work

Do not expand this root signature opportunistically. Revisit it only when a
runtime contract needs more than `t0..t5`, the current material texture cannot
express the material data, or a pass requires a resource model that cannot be
expressed by the current slots.

Deferred binding models remain:

- expanded fixed SRV slots;
- a single contiguous SRV descriptor table;
- structured-buffer material table;
- descriptor-indexed material indirection;
- bindless descriptors;
- broad render graph/resource-barrier migration.
