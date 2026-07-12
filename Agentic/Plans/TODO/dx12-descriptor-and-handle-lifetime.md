# DX12 Descriptor And Handle Lifetime

Date: 2026-07-12
Status: Not started — 0/5 phases complete
Impact area: DX12 renderer (descriptor allocators, texture registry, framebuffers)
Owner: rendering/DX12
Priority: Must do (highest severity of the 2026-07-12 adversarial review)

## Problem And Evidence (measured 2026-07-12)

The static SRV descriptor allocator is a bump allocator with no free path,
while the resources that consume its rows are recreated at runtime:

- `Dx12DescriptorAllocator::AllocateStatic()` only increments
  (`SkullbonezSource/Rendering/DX12/RenderDeviceDX12.cpp:453-469`); the counter
  resets solely on device init/shutdown (`:405`, `:420`), never on `Resize`.
- `Dx12TextureOwner::DeleteTexture`/`UnregisterSRV` tombstone the registry row
  but never release the static descriptor row
  (`SkullbonezSource/Rendering/DX12/RenderBackendDX12.Textures.cpp:801-849`).
  The tombstone comment (`:727-730`) claims rebuilds do not consume unbounded
  host memory — true, but the descriptor heap is the finite resource.
- Every `FramebufferDX12` allocates two static rows
  (`SkullbonezSource/Rendering/DX12/FramebufferDX12.cpp:203`, `:222`), and the
  runtime recreates the HDR, reflection, shadow, and volumetric targets on any
  window-size change
  (`SkullbonezSource/Runtime/Render/RuntimeRenderPasses.cpp:561-626`).
- Budget is `MAX_STATIC_SRVS = 128`
  (`SkullbonezSource/Rendering/DX12/RenderBackendDX12.h:930`). Roughly 15-25
  interactive window resizes with the cinematic pipeline active exhaust the
  heap and hit `SB_FATAL("DX12 static SRV heap exhausted")`. Texture
  delete/create churn leaks the same way.

Separately, texture handles carry no generation bits. A handle is
`slot index + 1` and deleted slots are reclaimed by the next create
(`RenderBackendDX12.Textures.cpp:731-739`), so a stale handle held across a
delete silently binds whichever texture reclaimed the slot
(`Dx12TextureOwner::BindTexture`, `:772-798`). The physics side already does
this correctly (`PhysicsBodyHandle` generations resolved through slot maps);
the render registry is the weaker of the engine's two handle disciplines.

## Goal

Static descriptor rows are reclaimable with fence-safe timing, framebuffer and
texture teardown returns every row it allocated, and a stale texture handle is
a detectable error rather than a silent alias. A resize/churn loop far past 128
iterations runs without descriptor exhaustion.

## Non-Goals

- No new descriptor heap sizes or heap layout redesign; the static/transient
  split stays.
- No change to transient per-frame descriptor semantics.
- No general render-resource handle system beyond the texture/SRV registry.

## Phases

- [ ] **L1 — Free-list static descriptor allocator.** Replace the bump-only
  static allocator with an in-place free list over the existing static range
  (fixed array, zero runtime heap growth per the allocation policy). Freeing is
  legal only when the caller proves the row is not referenced by in-flight
  command lists; route frees through the existing fence-proven retirement path
  (the same proof `Dx12DeferredReleaseOwner` uses for resources). Keep the
  exhaustion `SB_FATAL` with owner/capacity/high-water diagnostics.
  Acceptance: allocate-free-allocate reuses rows; high-water stat is reported in
  `GetRenderMemoryStats`.
- [ ] **L2 — Teardown returns rows.** `DeleteTexture`, `UnregisterSRV`,
  framebuffer `ResetResources`/destruction, and mip-pipeline shutdown return
  their static rows through L1. Audit every `AllocateStatic` call site
  (backend graph-transient slots, null texture, gen-mips null UAV, DXR
  reflection SRV/UAV, `FramebufferDX12`) and classify each as
  process-lifetime (never freed, documented) or recreated (must free).
  Acceptance: a create/delete texture loop and an FBO recreate loop hold the
  static high-water constant.
- [ ] **L3 — Generation-tagged texture handles.** Widen the registry handle to
  carry a generation (slot bits + generation bits in the existing `uint32_t`).
  `BindTexture`/`ResolveSrv`/`DeleteTexture` validate generation; a stale
  handle binds the null texture and logs a rate-limited diagnostic in Debug.
  Callers that legitimately hold long-lived handles (asset/texture collections)
  are updated in the same phase. Acceptance: a unit/arch test proves a stale
  handle after delete+reuse does not resolve to the new texture.
- [ ] **L4 — Churn regression proof.** Extend the graphics-stress controller
  with a bounded resize + texture-churn scenario that exceeds 128 static-row
  turnovers, and assert final static usage equals the process-lifetime
  baseline. Acceptance: the scenario crashes (or fatally exhausts) on
  pre-plan source and passes after L1-L3.
- [ ] **L5 — Review and gates.** Independent rubber-duck review of descriptor
  lifetime and handle-generation boundaries, comment-style audit of touched
  files, then final validation per the map below.

## Dependencies And Decisions

- No dependency on other 2026-07-12 review plans; may run first.
- Decision to record in-plan: number of generation bits (proposal: 8 bits
  generation / 24 bits slot; registry is far below 2^24 rows).
- Freed-row timing must reuse the existing frame-fence proof; do not invent a
  second retirement queue.

## Acceptance

All phase acceptances above, plus: `dx12_validation.txt` reports zero errors,
visual baselines match, and the L4 churn scenario passes with constant
static-descriptor high water.

## Validation

`tools\validate_dx12_renderer.bat`, then `tools\run_graphics_stress.bat 1`
(record command, measured runtime >= 10s, crash-free exit per MASTER rule 10).
L4 adds its churn scenario to the stress evidence. Arch-test changes register
through `tools\validate_all_cpu_tests.bat` in the same commit.
