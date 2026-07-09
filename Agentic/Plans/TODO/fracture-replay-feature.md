# Reversible GPU Fracture Replay (feature backlog)

Date: 2026-07-09 (consolidated from `reversible-fracture-replay-plan.md`;
full phase detail in that file's git history)
Status: Not started — 0%. Feature backlog, not architecture debt.
Impact area: DX12 renderer, shaders, replay UI, scene system, physics triggers

## Goal

Shoot a breakable object, watch it burst into many tiny GPU-simulated shards,
then drag the replay slider backward and watch the shards rejoin the original
object. Central rule: shard motion is deterministic per fracture seed, so
reverse scrubbing replays the same trajectories backward.

## Shape (from the draft)

1. Fracture plan hooks + scene authorship (mark breakables, author shard
   templates).
2. Fracture trigger from shooting.
3. GPU shard template + instanced rendering.
4. GPU shard simulation with terrain bounce (deterministic per seed).
5. Replay presentation samples for shards.
6. Replay UI slice (reverse scrub).
7. Focused validation + docs.

First useful slice: one breakable ball → shards → terrain bounce, no replay
integration yet.

## Preconditions

- Replay presentation-sample work should land after
  `TODO/replay-prediction-and-memory.md` phase B decides the visual-sample
  data model, or shard samples will be built on a format that plan changes.
- Re-verify the draft's repo-fit notes against current replay/render code
  before implementation; the draft predates the 2026-07 replay and renderer
  changes.

## Validation

Renderer slices: `validate_dx12_renderer` (+ `validate_perf` for GPU sim
cost). Replay slices: `validate_full` + `validate_replay_scrub`. Physics
trigger slices: `validate_physics`.
