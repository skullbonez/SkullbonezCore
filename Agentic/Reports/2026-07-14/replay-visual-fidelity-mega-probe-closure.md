# Replay Visual Fidelity Mega Probe Closure

Date: 2026-07-14

Branch: `nightrunner-13th-july`

Plan: `Agentic/Plans/TODO/replay-visual-fidelity-mega-probe.md`

Result: V0-V6 complete; replay monolith decomposition is unblocked

## Product Contract

`tools\validate_replay_visual_fidelity.bat` is the permanent immutable oracle
for the 200-box prediction cascade. It launches one engine process, performs one
prediction generation, and presents one contiguous 2,401-tick reveal. It then
crosses into a one-way offline-verification mode and reconstructs the saved RVPD
state using CPU domain values only. No second engine, prediction, or Present is
permitted.

`Toppled` means at least 101 of the 200 authored wall bricks are directly on the
flat y=0 terrain and solver-sleeping throughout every one of the final 121
samples. Direct ground support uses oriented vertical support extent plus the
physics contact tolerance; a sleeping brick perched on another brick does not
count. The immutable working base records 187.

## Authoritative Result

- launcher: one engine process, one Predict action, one presented cascade, no
  nested scrub run;
- 2,401 visual ticks, first reveal 0, last reveal 2400;
- 200 moved wall bricks, 200 settled wall bricks, 187 grounded sleepers;
- 199 downstream causal nodes;
- schema-4 artifact with 2,401 RVIS rows and typed RVPD state;
- 797 retained trajectory records and 957,601 trajectory points;
- focused visual-packet suite: 15/15 cases and 67/67 assertions;
- exact active trajectory, causal topology, marker, ghost, and renderer-span
  fingerprints match the approved manifest;
- inactive completed-prediction worker records remain semantically bound but do
  not contaminate the presentation fingerprint.

The gate rejected every deliberate first divergence: ordinary vertex,
incomplete horizon, causal activation, topology, revealed segment, semantic
packet, artifact byte, and RVPD byte. Its determinism controls rejected seed
mismatch, missing tick, event mutation, non-fixed step, truncated horizon,
record reordering, vertex-byte change, dropped geometry, reserve growth, and
duplicate prediction generation.

## Validation Evidence

- Final `tools\validate_replay_visual_fidelity.bat`: PASS after formatting and
  the broad gate, about six minutes. Profile build succeeded with zero warnings
  and errors; the launcher shape, focused tests, immutable run, durable state,
  and all false-pass controls passed.
- `tools\validate_replay_scrub.bat --prove-failure-propagation`: returned the
  expected synthetic exit code 37. Static launcher inspection passed. The normal
  alias was not launched because it would repeat the product prediction.
- `tools\validate_replay_v2_artifact.bat`: PASS. Schema-4 current/writer,
  deterministic schema-3 migration, future rejection, corrupt visual state,
  restore/query, and generated-topology lanes passed with zero-warning Debug
  and Profile builds.
- `python tools\migrate_data_formats.py --check`: PASS, 39 authored files.
- `tools\validate_full.bat`: PASS in roughly eight minutes. Mandatory CPU
  suites, zero-warning Profile/Debug builds, DX12 comparison with zero InfoQueue
  errors, standalone physics, and the byte-exact 44,401-line varied baseline
  passed.
- Secondary screenshots:
  `TestOutput/validation/replay_visual_fidelity/reveal_building.bmp`,
  `reveal_mid.bmp`, `reveal_late.bmp`, and `reveal_complete.bmp`.

## SkullScope Accounting

The artifact gate used these trace/export commands:

```text
Debug\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --shadows off --scene SkullbonezData/scenes/replay_v2_solver_one.scene.json --frames 120 --replay on --replay-seconds 1 --replay-save-probe TestOutput\validation\replay_v2\replay_save_probe.skreplay --physics-diag TestOutput\validation\replay_v2\replay_save_probe_runtime.physicsdiag.ndjson
Debug\SKULLBONEZ_CORE.exe ... --replay-restore-failure-file-probe ... --physics-diag TestOutput\validation\replay_v2\replay_restore_failure.physicsdiag.ndjson
Debug\SKULLBONEZ_CORE.exe ... --scene SkullbonezData/scenes/replay_v2_generated_topology.scene.json ... --physics-diag TestOutput\validation\replay_v2\replay_generated_topology_runtime.physicsdiag.ndjson
tools\replay_query.bat ... export-skullscope --frames 0:5 --out TestOutput\validation\replay_v2\replay_save_probe.physicsdiag.ndjson --run-id replay_v2_artifact
```

Bounded queries exposed to the model:

```text
tools\physics_query.bat TestOutput\validation\replay_v2\replay_restore_failure.physicsdiag.ndjson restore --limit 4
tools\physics_query.bat TestOutput\validation\replay_v2\replay_save_probe.physicsdiag.ndjson summary
```

On-disk sizes were 72,441 bytes for the runtime trace, 2,303 bytes for the
exported trace, 204,800 bytes for its SQLite cache, 71,407 bytes for the restore
failure trace, 225,280 bytes for its cache, and 163,696 bytes for the generated
topology trace. The bounded restore and summary query outputs were 1,812 and
1,844 bytes respectively, for 3,656 GPT-read bytes. Neither bounded query was
truncated. Other replay-query tool outputs totalled 19,234 bytes but were not
ingested as raw SkullScope data.

## Independent Review And Comment Audit

The independent read-only review found no blocking issue. It verified the
single-generation capability boundary, offline verifier isolation, exact
renderer-visible worker-bank selection, semantic coverage of the inactive bank,
non-vacuous mutation controls, and the grounded-and-sleeping topple definition.
It noted only two non-blocking follow-ups: an additional inactive root-worker
unit mutation could complete the local matrix, and the high permitted replay
reserve-growth grant count is a separate performance/log-volume concern.

Touched source/tool checklist: 21/21 inspected, 0 deferred.

- `InteractionAutomationController.cpp/.h`
- `ReplayPredictionArchive.cpp/.h`
- `ReplayRuntime.cpp/.h`
- `ReplayV2Artifact.cpp/.h`
- `ReplayVisualPacket.h`
- `ReplayVisualPacketFingerprint.cpp/.h`
- `RunReplayProbes.cpp`
- `RunReplayTools.cpp`
- `RunFrame.cpp`
- `TestReplayVisualPacket.cpp`
- `check_replay_v2_artifact.py`
- `check_replay_visual_fidelity.py`
- `replay_query.py`
- `validate_project_filters.py`
- `validate_replay_scrub.bat`
- `validate_replay_visual_fidelity.bat`

The source files contain the required learning headers and local comments for
single-generation authority, offline lifetime, active-versus-worker-bank
selection, exact presentation hashing, and grounded/sleeping completion.

## Handoff

The immutable baseline must not be refreshed during ownership refactors. Every
M0-M8 task in `replay-monolith-decomposition.md`, including the inventory task,
runs `tools\validate_replay_visual_fidelity.bat` exactly once before checkoff or
commit. A failure reopens the current task.
