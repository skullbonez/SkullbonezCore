# Data-Driven Shadow Caster Streams — Delete The Hardcoded Pine Special Case

Date: 2026-07-15
Status: Active — 0/3 tasks complete
Impact area: `GameModelRenderer`, render instance records, asset registration,
DX12 renderer validation
Owner: rendering

## Problem And Evidence

The renderer hardcodes content knowledge into shadow submission:

1. `IsPineVisualMaterial( const RenderMaterial& )` at
   `SkullbonezSource/Rendering/GameModelRenderer.cpp:92` sniffs a specific
   visual material to decide shadow treatment.
2. `ResolveShadowCasterStream` (`GameModelRenderer.cpp:99-126`) routes every
   instance into a closed enum of exactly `Sphere`, `Box`, `Pine` shadow
   streams, with `Pine` chosen by the material sniff at line 126 and again at
   line 466.

Adding a fourth renderable look requires editing the renderer. Adversarial
review 2026-07-15 flagged this as finding #3: an engine submission path must
bin by data it does not interpret, not by guessing content categories.

## Goal

The shadow-caster stream an instance belongs to is a plain data field resolved
once at the owner boundary (asset registration / render-instance build), and
`GameModelRenderer` bins instances by that opaque stream id with no
material-name or shape-kind sniffing left in the submission path.
`IsPineVisualMaterial` is deleted.

## Non-Goals

- No general material system, mesh pipeline, or new shadow techniques.
- No visual change: the same instances must land in the same streams, so
  committed DX12 baselines must pass unchanged.
- No scene/asset schema version bump if the stream can be derived during
  registration from existing data; a schema field is the fallback, and if it
  becomes necessary the versioned-migration rule in `AGENTS.md` applies in
  full (version bump, migration step, upgraded committed files, format tests).

## Tasks

- [ ] T1 — Add `shadowCasterStream` (small enum/int) to the render instance
      record (or its build-time source), resolved at asset
      registration/instance-build time. Encode today's exact mapping:
      sphere shape → sphere stream, convex hull with pine visual material →
      pine stream, otherwise box stream. The pine knowledge moves to the
      registration site as data (an asset-library property or a single
      registration-time table), with a `Why:` comment naming this plan.
- [ ] T2 — Rewrite `ResolveShadowCasterStream`/`AppendShadowCasterToBatches`
      to bin purely on the instance field. Delete `IsPineVisualMaterial` and
      both call sites (`GameModelRenderer.cpp:126,466`). Grep-proof that no
      material sniffing remains in `Rendering/`.
- [ ] T3 — Final gates: `tools\validate_dx12_renderer.bat` with zero InfoQueue
      errors and byte-matching committed baselines (no refresh — this is a
      pure refactor), then `tools\run_graphics_stress.bat 1` with recorded
      command, measured runtime, and clean exit.

## Dependencies And Decisions

- Owner decision 2026-07-15: minimal data-driven fix now; a real material
  system is explicitly out of scope.
- Independent of the other 2026-07-15 remediation plans.

## Acceptance

- `IsPineVisualMaterial` no longer exists; the submission path contains no
  content-category conditionals.
- DX12 screenshot baselines pass unchanged; stress run is crash-free.
- The stream mapping lives in exactly one registration-side location.

## Validation

- `tools\validate_dx12_renderer.bat` + `tools\run_graphics_stress.bat 1`
  (mapped row for `GameModelRenderer`/render submission changes), output
  pasted at closure.
