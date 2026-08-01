# Contact Energy And Warm-Start Integrity — ES2

Date: 2026-08-02
Branch: `nightrunner-1st-AUG-26`
Scope: giant tower scene and semantic gate; no production solver behavior or capacity changed

## Result

The repository now carries schema-v3
`SkullbonezData/scenes/contact_energy_tower_64.scene.json`: one fixed foundation
and 64 centered wide slabs under the exact ES0 geometry and unchanged production
gravity, friction, restitution, timestep, damping, sleep, and 12-iteration
settings. The scene contains no rotation, velocity, angular velocity, authored
impulse, or other disturbance field.

`tools/check_contact_energy_scenes.py` is the single semantic owner for the
64-level tower, the existing four-brick reproduction, and the existing 200-box
topple. It consumes compact, non-truncated SkullScope JSON packets and checks:

- precision-derived mechanical-energy gain;
- complete frame/body topology and the unchanged solver cap;
- tower/four-brick upward reversals, launch speed, penetration, final-300 cache
  misses, support, and permanent sleep;
- wall final-300 relaunches, all 211 permanent sleepers, finite speed, unique
  final body identities, and physical terrain clearance using sphere radius or
  quaternion-oriented box support extent; and
- the exact known current failure codes when `--expect-current-failure` is used,
  so an arbitrary failure cannot false-pass the pre-correction witness.

The checker emits the exact bounded SQL commands through `--print-questions`.
The tower and four-brick each require one aggregate row; wall requires one
aggregate row and one at-most-211-row final-body answer. Its self-test plants
energy, launch, penetration, cache, support, sleep, and below-terrain failures.
That self-test is now part of `validate_physics_deep` while the live trace gate
remains deliberately unarmed until the production correction exists.

## Capacity Ruling

ES0's 64-level candidate-list fatal is retained as a semantic `incomplete`
failure. The four-pairs-per-body reservation was not increased: the tower reaches
that ceiling only after the current solver destabilizes and collapses it. Raising
the list merely to observe more of the blow-up would hide a behavior symptom and
would contaminate the pre-correction gate. A corrected stable tower must remain
inside the existing admitted topology.

The 128-level spatial-entry fatal remains a stretch result and is likewise not
retuned in ES2.

## Current Authoritative Failures

The canonical summary questions reproduce the ES0 evidence:

| Workload | Locked tolerance | Current semantic failures |
|---|---:|---|
| tower 64 | `279.92909129620364` | incomplete at frame 37, final-300 cache evidence unavailable/nonzero, penetration, support, and sleep; post-frame-300 launch is unavailable because the run is incomplete |
| four-brick | `0.6819238001708984` | cache tail, 566 launch reversals, upward speed, and sleep |
| wall 200 | `57.60880513711552` | `127.770669` energy gain, striker below physical terrain support, and only 210/211 sleepers |

All three `--expect-current-failure` commands exit zero only because their exact
required defect sets are present. `--expect-pass` remains the corrected gate and
would reject each current packet.

## Scene Admission Proof

The static scene check passes the exact schema, body count, geometry, material,
world, timestep, and no-disturbance contract. Project metadata reports 804
project items and 804 filter items with zero errors.

The fresh one-frame runtime command is:

```powershell
Debug\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --fixed-step `
  --shadows off --hide-top-text --automation-hidden-window --frames 1 `
  --scene SkullbonezData\scenes\contact_energy_tower_64.scene.json `
  --physics-diag TestOutput\contact_energy_es2\tower_load.physicsdiag.ndjson
```

It exits zero. The bounded summary reports DirectX 12, model count 65, gravity
`-50`, 12 solver iterations, 64 dynamic slabs, and one fixed foundation.

## SkullScope Accounting

No raw NDJSON or SQLite contents were shown to the model.

Primary artifacts:

| Trace | NDJSON bytes | SQLite bytes |
|---|---:|---:|
| ES2 one-frame tower admission | 64,266 | 233,472 |
| ES0 tower 64 | 3,269,091 | 1,675,264 |
| ES0 four-brick | 18,823,863 | 8,249,344 |
| ES0 wall 200 | 1,579,164,069 | 815,837,184 |

Canonical question invocations were generated and run as follows; the checker
prints the literal SQL inside each command, and every SQL invocation used its
shown limit:

```powershell
python tools\check_contact_energy_scenes.py --workload tower64 `
  --trace TestOutput\contact_energy_es0\tower_64_g.physicsdiag.ndjson --print-questions
tools\physics_query.bat TestOutput\contact_energy_es0\tower_64_g.physicsdiag.ndjson `
  sql "<exact emitted tower64_summary SQL>" --limit 5

python tools\check_contact_energy_scenes.py --workload four_brick `
  --trace TestOutput\contact_energy_es0\bv0_g.physicsdiag.ndjson --print-questions
tools\physics_query.bat TestOutput\contact_energy_es0\bv0_g.physicsdiag.ndjson `
  sql "<exact emitted four_brick_summary SQL>" --limit 5

python tools\check_contact_energy_scenes.py --workload wall200 `
  --trace TestOutput\contact_energy_es0\wall200_g.physicsdiag.ndjson --print-questions
tools\physics_query.bat TestOutput\contact_energy_es0\wall200_g.physicsdiag.ndjson `
  sql "<exact emitted wall200_summary SQL>" --limit 5
tools\physics_query.bat TestOutput\contact_energy_es0\wall200_g.physicsdiag.ndjson `
  sql "<exact emitted wall200_final_bodies SQL>" --limit 300
```

Those redirected packet outputs were 1,812, 1,808, 1,868, and 34,788 bytes;
zero query-result characters were exposed to GPT. Five earlier one-row
exploratory versions selected the same dynamic-count, launch, cache-tail,
permanent-sleep, and wall-final-body facts into 512-, 507-, 374-, 34,788-, and
575-byte ignored packets; their outputs were also redirected and unread.

The one-frame admission trace was queried twice with the exact command below:

```powershell
tools\physics_query.bat TestOutput\contact_energy_es2\tower_load.physicsdiag.ndjson summary
```

The first 4,249-character result was read by GPT and was not truncated. The
second cached result was suppressed; only its 4,250-character size was exposed,
so its GPT-read query payload is zero. Reused ES0 bounded packets contributed
8,585 read characters, and the three semantic-checker summaries contributed
498. Total GPT-read SkullScope-derived output for ES2 is **13,332 characters**.
No successful bounded result was truncated.

## Validation And Comment Audit

- checker self-test and Python compilation: PASS;
- exact static tower-scene contract: PASS;
- one-frame hidden DX12 scene load: PASS;
- direct project/filter validation: PASS, 804/804;
- three authoritative `--expect-current-failure` checks: PASS;
- `tools\validate_physics_deep.bat`: PASS, including byte-exact broad Physics,
  known-issue, shooting-reaction, SkullScope baseline, semantic controls, and
  ready Debug/Profile builds;
- `tools\validate_fast.bat`: PASS; and
- `git diff --check`: PASS.

Touched source-bearing audit is 2/2 with zero deferred:

- `tools/check_contact_energy_scenes.py`; and
- `tools/validate_physics_deep.bat`.

The checker teaches packet ownership, precision-only tolerances, physical
support extent, and final-window cache/settling hazards. The deep gate's existing
learning header remains accurate and now links the checker. Strict glossary
inventory reports 587 files / 993 unique terms with zero duplicates, drift, or
rulings; all 587 Related-path scans pass. No wording requires owner input.

Independent rubber-duck review remains reserved for ES6 closure, where it can
review the final correction, scale results, candidate artifacts, and complete
baseline-governance packet rather than this pre-behavior gate alone.
