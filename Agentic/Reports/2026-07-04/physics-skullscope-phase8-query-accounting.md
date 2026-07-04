# Physics SkullScope Phase 8 Query Accounting

Date: 2026-07-04
Branch: `codex/physics-store-authority`
Validation log: `TestOutput\agent_validate_physics_query_phase8_close.log`

## Result

`tools\validate_physics_query.bat` passed. The gate generated a deterministic
SkullScope trace from `physics_bench_varied.scene.json`, ran the bounded query
packet, matched `TestOutput\baselines\physics_query_varied.json`, and rebuilt
Debug with 0 warnings and 0 errors.

## Trace Command

```bat
Debug\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --shadows off --scene SkullbonezData/scenes/physics_bench_varied.scene.json --physics-diag Debug\physics_query_varied.physicsdiag.ndjson
```

## Artifact Sizes

| Artifact | Bytes |
|----------|------:|
| `Debug\physics_query_varied.physicsdiag.ndjson` | 104,766,944 |
| `Debug\physics_query_varied.physicsdiag.sqlite` | 51,146,752 |
| `TestOutput\baselines\physics_query_varied.json` | 180,019 |

## Bounded Query Commands

Raw NDJSON and SQLite artifacts were not loaded into model context. The
following bounded commands are the `tools\physics_query.bat` equivalents of the
regression packet in `tools\check_physics_query_regression.py`.

| Query | Command | Output chars |
|-------|---------|-------------:|
| `body_roll_a` | `tools\physics_query.bat "Debug\physics_query_varied.physicsdiag.ndjson" body roll_a --frames 0:1200 --limit 12` | 9,208 |
| `broadphase` | `tools\physics_query.bat "Debug\physics_query_varied.physicsdiag.ndjson" broadphase --frames 0:1200 --limit 12` | 3,150 |
| `compare_self` | `tools\physics_query.bat "Debug\physics_query_varied.physicsdiag.ndjson" compare "Debug\physics_query_varied.physicsdiag.ndjson" --limit 8` | 620 |
| `contact_audio_body_roll_a` | `tools\physics_query.bat "Debug\physics_query_varied.physicsdiag.ndjson" contact-audio-body --body roll_a --limit 8` | 5,921 |
| `contact_audio_events` | `tools\physics_query.bat "Debug\physics_query_varied.physicsdiag.ndjson" contact-audio-events --limit 8` | 4,824 |
| `contact_audio_rejections_propagated` | `tools\physics_query.bat "Debug\physics_query_varied.physicsdiag.ndjson" contact-audio-rejections --reason propagated_impulse --limit 8` | 223 |
| `contact_audio_summary` | `tools\physics_query.bat "Debug\physics_query_varied.physicsdiag.ndjson" contact-audio-summary --limit 8` | 9,992 |
| `contact_audio_timeline` | `tools\physics_query.bat "Debug\physics_query_varied.physicsdiag.ndjson" contact-audio-timeline --limit 8` | 2,010 |
| `contacts_penetration` | `tools\physics_query.bat "Debug\physics_query_varied.physicsdiag.ndjson" contacts --top penetration --limit 12` | 6,280 |
| `energy` | `tools\physics_query.bat "Debug\physics_query_varied.physicsdiag.ndjson" energy --frames 0:1200 --limit 12` | 6,704 |
| `events` | `tools\physics_query.bat "Debug\physics_query_varied.physicsdiag.ndjson" events --limit 20` | 23,471 |
| `events_penetration` | `tools\physics_query.bat "Debug\physics_query_varied.physicsdiag.ndjson" events --type penetration_sustained,penetration_growing --limit 20` | 134 |
| `frame_600` | `tools\physics_query.bat "Debug\physics_query_varied.physicsdiag.ndjson" frame 600 --limit 8` | 9,235 |
| `island_1_final` | `tools\physics_query.bat "Debug\physics_query_varied.physicsdiag.ndjson" island 1 --frame 1199 --limit 12` | 586 |
| `pipeline` | `tools\physics_query.bat "Debug\physics_query_varied.physicsdiag.ndjson" pipeline --frames 0:1200 --limit 12` | 5,587 |
| `question_penetration_spikes` | `tools\physics_query.bat "Debug\physics_query_varied.physicsdiag.ndjson" questions penetration_spikes` | 378 |
| `question_stack_health` | `tools\physics_query.bat "Debug\physics_query_varied.physicsdiag.ndjson" questions stack_health` | 367 |
| `rolling` | `tools\physics_query.bat "Debug\physics_query_varied.physicsdiag.ndjson" rolling --frames 0:1200 --limit 12` | 7,904 |
| `solver` | `tools\physics_query.bat "Debug\physics_query_varied.physicsdiag.ndjson" solver --frames 0:1200 --limit 12` | 3,632 |
| `stacks` | `tools\physics_query.bat "Debug\physics_query_varied.physicsdiag.ndjson" stacks --frames 0:1200 --limit 12` | 4,018 |
| `summary` | `tools\physics_query.bat "Debug\physics_query_varied.physicsdiag.ndjson" summary --limit 8` | 9,393 |

Total bounded query payload: 113,637 characters.
