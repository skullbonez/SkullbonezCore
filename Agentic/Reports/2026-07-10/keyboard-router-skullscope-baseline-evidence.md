# Keyboard Router SkullScope Baseline Evidence

Date: 2026-07-10  
Owning slice: runtime-shell B1a/B1c and the C1a scene-provenance fixture repair

## Why the baseline moved

The B1a/B1c performance gate reached `physics_bench_varied.scene.json` after
C1a began rejecting duplicate authored names. Eight ball rows shared names with
box rows. Renaming only those ball rows made the fixture valid without weakening
the parser. The known stacking signature also proved stale: three final Debug
runs produced the same 6,178,560-byte CSV and SHA-256
`481faded9dd26df90b5f9b000545188dff259a37128f720002a9565d940c1f96`.

`tools\validate_physics_deep.bat` passed after regenerating the two affected
tracked baselines. Its aggregate console transcript was truncated by the shell
renderer, but the gate completed with exit code 0 and
`VALIDATE_PHYSICS_DEEP: ALL PASSED`. No conclusion below depends on truncated
query text.

## Trace command

```text
Debug\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --shadows off --scene SkullbonezData/scenes/physics_bench_varied.scene.json --physics-diag Debug/physics_query_varied.physicsdiag.ndjson
```

## Bounded queries

Each command below was run by `tools/check_physics_query_regression.py`. The
size columns measure that query's normalized, indented canonical JSON in the
tracked regression packet. The query payloads were not exposed to GPT; only
this size metadata was read.

| Query | Canonical chars | UTF-8 bytes |
|---|---:|---:|
| `python tools\physics_query.py Debug\physics_query_varied.physicsdiag.ndjson summary --limit 8` | 13,758 | 13,758 |
| `python tools\physics_query.py Debug\physics_query_varied.physicsdiag.ndjson events --limit 20` | 30,848 | 30,848 |
| `python tools\physics_query.py Debug\physics_query_varied.physicsdiag.ndjson contact-audio-summary --limit 8` | 12,867 | 12,867 |
| `python tools\physics_query.py Debug\physics_query_varied.physicsdiag.ndjson contact-audio-events --limit 8` | 6,137 | 6,137 |
| `python tools\physics_query.py Debug\physics_query_varied.physicsdiag.ndjson contact-audio-rejections --reason propagated_impulse --limit 8` | 254 | 254 |
| `python tools\physics_query.py Debug\physics_query_varied.physicsdiag.ndjson contact-audio-body --body roll_a --limit 8` | 8,063 | 8,063 |
| `python tools\physics_query.py Debug\physics_query_varied.physicsdiag.ndjson contact-audio-timeline --limit 8` | 2,834 | 2,834 |
| `python tools\physics_query.py Debug\physics_query_varied.physicsdiag.ndjson frame 600 --limit 8` | 12,562 | 12,562 |
| `python tools\physics_query.py Debug\physics_query_varied.physicsdiag.ndjson body roll_a --frames 0:1200 --limit 12` | 12,226 | 12,226 |
| `python tools\physics_query.py Debug\physics_query_varied.physicsdiag.ndjson energy --frames 0:1200 --limit 12` | 8,786 | 8,786 |
| `python tools\physics_query.py Debug\physics_query_varied.physicsdiag.ndjson events --type penetration_sustained,penetration_growing --limit 20` | 159 | 159 |
| `python tools\physics_query.py Debug\physics_query_varied.physicsdiag.ndjson contacts --top penetration --limit 12` | 7,971 | 7,971 |
| `python tools\physics_query.py Debug\physics_query_varied.physicsdiag.ndjson island 1 --frame 1199 --limit 12` | 773 | 773 |
| `python tools\physics_query.py Debug\physics_query_varied.physicsdiag.ndjson stacks --frames 0:1200 --limit 12` | 5,662 | 5,662 |
| `python tools\physics_query.py Debug\physics_query_varied.physicsdiag.ndjson rolling --frames 0:1200 --limit 12` | 10,262 | 10,262 |
| `python tools\physics_query.py Debug\physics_query_varied.physicsdiag.ndjson broadphase --frames 0:1200 --limit 12` | 4,288 | 4,288 |
| `python tools\physics_query.py Debug\physics_query_varied.physicsdiag.ndjson solver --frames 0:1200 --limit 12` | 4,570 | 4,570 |
| `python tools\physics_query.py Debug\physics_query_varied.physicsdiag.ndjson pipeline --frames 0:1200 --limit 12` | 7,688 | 7,688 |
| `python tools\physics_query.py Debug\physics_query_varied.physicsdiag.ndjson questions penetration_spikes` | 421 | 421 |
| `python tools\physics_query.py Debug\physics_query_varied.physicsdiag.ndjson questions stack_health` | 406 | 406 |
| `python tools\physics_query.py Debug\physics_query_varied.physicsdiag.ndjson compare Debug\physics_query_varied.physicsdiag.ndjson --limit 8` | 769 | 769 |
| **Total normalized query output** | **151,304** | **151,304** |

## Data-size accounting

- Raw trace on disk: `Debug/physics_query_varied.physicsdiag.ndjson`,
  100,811,064 bytes.
- SQLite cache on disk: `Debug/physics_query_varied.physicsdiag.sqlite`,
  49,045,504 bytes.
- GPT-read query payload: 0 characters / 0 bytes. The validation script parsed
  and compared query JSON locally; GPT read only the bounded size rows above.
- Truncation: the aggregate deep-gate transcript was truncated; individual
  query payloads were neither printed nor read, and the narrower metadata pass
  completed without truncation.

## Final evidence

- `tools\validate_perf.bat`: passed, including the allocation guard, DX12 perf,
  physics perf, and threshold checks.
- `tools\validate_physics_deep.bat`: passed in 81.85 seconds.
- `tools\validate_full.bat`: passed in 48.35 seconds with zero build warnings,
  zero DX12 validation errors, matching screenshots, and a 20,001-line
  byte-exact core physics baseline.
