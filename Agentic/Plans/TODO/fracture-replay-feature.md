# Reversible GPU Fracture Replay

Date: 2026-07-10 (dependency reconciled)
Status: Backlog — 0/7 phases complete; blocked
Impact area: DX12 rendering, shaders, replay presentation, scene authorship,
physics trigger commands
Owner: fracture feature; it must extend existing render/replay owners

## Goal

A breakable authored object produces deterministic GPU-presented shards that
can be replayed backward into the original object. Seed, fracture template, and
presentation samples reproduce identical shard trajectories.

## Blocking Preconditions

- `replay-architecture-and-right-sizing.md` R3 must define the retained
  presentation-sample extension contract and capacity policy.
- `render-backend-decomposition.md` must identify the concrete resource/pipeline
  owner used by shard templates and instancing.
- `validation-gate-integrity.md` must make CPU tests mandatory.

Do not begin implementation while any precondition is open. This feature must
not create a parallel replay buffer, custom allocation exception, or new
`Run::*` integration path.

## Phases

- [ ] F0. Re-verify design against current replay/render/scene owners and record
  memory/GPU budget.
- [ ] F1. Add registered breakable asset/scene authorship and deterministic
  fracture seed/template data.
- [ ] F2. Route shooting/fracture trigger through physics/scene commands.
- [ ] F3. Add fixed-capacity shard template and instanced renderer.
- [ ] F4. Add deterministic GPU shard motion and terrain bounce.
- [ ] F5. Extend the approved replay presentation sample and reverse scrub.
- [ ] F6. Add UI, CPU determinism tests, renderer/replay/perf validation, and
  independent closure review.

## First Useful Slice

One registered breakable ball → bounded shards → terrain bounce, without replay
integration. It starts only after F0 and all blocking preconditions.

## Validation

Renderer slices use DX12 architecture tests + renderer gate; GPU simulation uses
perf; replay uses CPU replay tests + replay scrub + full gate; physics trigger
uses physics determinism. All gates inherit the mandatory CPU umbrella.
