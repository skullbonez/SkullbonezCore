# DX12 Binding ABI

Status: current DX12 ordinary raster contract as of the descriptor/upload/root-signature cleanup slice.

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
Current valid slots are `0..3`. Invalid slots are ignored for compatibility and
emit a Debug diagnostic event.

## Shader Contract Implications

`SkullbonezSource/SkullbonezShaderContracts.h` records high-risk runtime shader
contracts. Its resource `slot` values use the ABI above:

- object and terrain base textures use `t0`;
- water reflection textures use `t1`;
- tonemap uses `t0` scene, `t1` depth, and `t2` volumetric;
- optional shadow maps use `t3`.

The shader cleanup branch added CPU `RenderMaterial` data, but material v1 still
flows through the existing packed instance payload. It does not add a material
descriptor table, structured buffer, bindless heap, or new root parameter.

## Descriptor Lifetime

Persistent SRV/UAV descriptors live in the static range:

- loaded textures;
- framebuffer color/depth SRVs;
- DXR reflection UAV/SRV descriptors;
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
runtime contract needs more than `t0..t3`, a material table cannot fit the packed
instance path, or a pass requires a resource model that cannot be expressed by
the current slots.

Deferred binding models remain:

- expanded fixed SRV slots;
- a single contiguous SRV descriptor table;
- material texture;
- structured-buffer material table;
- bindless descriptors;
- broad render graph/resource-barrier migration.
