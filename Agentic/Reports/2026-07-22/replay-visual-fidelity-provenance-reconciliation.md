# Replay Visual Fidelity Provenance Reconciliation

Date: 2026-07-22
Branch: `nightrunner`
Result: Complete; replay-fidelity provenance blocker resolved
Authorization: User explicitly accepted the config change and requested the update

## Cause

Commit `7543b1c8` (`Remove render-only terrain tessellation`) changed the exact
bytes of `SkullbonezData/engine.cfg` by bumping `format_version` from 5 to 6
and removing `terrain_render_step_size = 2`. The replay visual baseline kept
the preceding config fingerprint, so its provenance guard stopped before the
behavioral comparison.

## Mechanical Reconciliation

This uses the standing provenance-only golden ruling. The replacement config
hash was computed from the exact final authored config file:

- old `configSha256`:
  `83401df03cb6e212a6a74a38e815fc550d57aa983fc9b792c2c8f4e5c784a3f4`
- exact `SkullbonezData/engine.cfg` SHA-256 and new `configSha256`:
  `bd0bb719aad7231cf500ca9a61af7d2f017e557b1b18b7de82df7eb93a3b5d93`

Changing that single field mechanically changed the complete visual-baseline
file hash:

- old visual-baseline SHA-256:
  `68b8cee200e21e97645880daaadbb61e502738b6e46c27974795e82165d79dcf`
- new visual-baseline SHA-256 and causal `visualBaselineSha256`:
  `4f086ba75acd43f0e37edc4d12d52c5d3da39f5fb8ac9b7af5e601eb11a34226`

A recursive JSON leaf comparison against `HEAD` reports exactly two changes:
`configSha256` in the visual baseline and its derived
`visualBaselineSha256` in the causal baseline. No tick, frame, topology,
sample, artifact, scene, shader, physics, screenshot, or behavioral golden
value changed.

## Validation

The desktop shell could not open a separate visible console, so the mapped
gate ran in the app shell and its output was captured there.

| Command | Time | Result |
|---|---:|---|
| exact SHA-256 and recursive JSON leaf-diff proof | 0.9 s | PASS; one allowed hash leaf in each baseline |
| `tools\validate_replay_visual_fidelity.bat` | 435.3 s | PASS |

The gate proved one engine process, one prediction generation, one presented
cascade, and zero nested scrub runs. It matched 2,401 ticks, 200 moved wall
bricks, 175 toppled wall bricks, 200 causal nodes, and 62 saved/loaded ticks.
All visual, causal, semantic-packet, artifact-byte, prediction-artifact, and
determinism negative controls detected their injected divergence.

The previous provenance blocker is resolved. This was not a baseline refresh;
it was a two-field provenance reconciliation followed by a passing comparison
against every existing behavioral golden value.
