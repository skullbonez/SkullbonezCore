# Broadphase Pair Dedup BD0 Profile Artifacts

Date: 2026-08-02
Plan phase: Broadphase Pair Dedup Cost BD0
Profile source commit: `6514636cd7e02588d204d8bd7f75db4bcb581f44`
Profile executable SHA-256: `343E2F5CCD2412E132F100295F961849C4588FA58C3C9F13BBE5E44FEDC6F275`
Profile executable bytes: 4,239,360

These are the untouched, uninstrumented Profile measurements taken before any
BD0 source edit. The Debug-only pair stream and invocation counter are absent
from this configuration. Marker units are milliseconds.

| Artifact | Frames | SHA-256 |
|---|---:|---|
| `physics_bench_perf.json` | 2,340 | `EEAA9CB752708490C07F326B8E943D761A866F334CF455CDE416A8E0D747893D` |
| `physics_scale_200_perf.json` | 1,140 | `C62555C1BD34D03A81FA72F19D323DE8D48C768E7A4DFA127CE1BF4A1889C46F` |
| `physics_scale_520_perf.json` | 1,140 | `FD168B28A8B09DF3DE02A04F39FDB4EA2C0AA01B6DD0A41FC22929E5E2C0DDEA` |
| `physics_scale_1000_perf.json` | 1,140 | `50B745C5E1F2885B0CD51F6BB08F8676AA009C7FAFCBE95EB95693263358886A` |
| `physics_scale_2000_perf.json` | 1,140 | `9597FFD5AC5310A4F5FEA2B52674E737A188A385C51FC0AD31A644336C1CABCE` |
| `physics_scale_sleepy_5000_perf.json` | 1,140 | `B0E3F09E0C72263EB14D63B851A1AC4B7B88073AD8B283C76D9CBB4AF064FACF` |
| `bd0_sparse4000_w0_perf.json` | 1,140 | `08F3ED134ACDB60F264F8FB6444C89083CDFDB4B8C7363424EB01D5D1F1B817D` |

The 37/200/520/1,000/2,000/5,000 artifacts came from the normal performance
matrix. The exact-capacity 4,000 artifact came from the compressed sparse input
preserved beside the pair oracles, with workers 0 and sleep disabled. This
separates the default-capacity dense-bitset cost from the tracked sleepy scene's
5,000 actual bodies despite that scene's authored `modelCapacity` value of
6,000.

The first full `validate_perf.bat` invocation produced these artifacts and
passed all absolute budgets. Its comparison gate exited 7 on unrelated whole-
frame run-to-run noise, so no baseline was refreshed. Commit-preparation gate
results are recorded in the phase report rather than modifying these locked
pre-edit measurements.
