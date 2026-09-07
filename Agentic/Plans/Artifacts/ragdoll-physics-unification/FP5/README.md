# Point-joint comparison

The fixture compares the actual solver immediately before stage (a)
(`87d501af5^`), completed stage (a) (`87d501af5`), and this branch's solver.
Each probe compiles that revision's `Ragdoll.cpp` and `Ragdoll.h` against the
same unchanged portable body-store and maths library. The current candidate's
source parent is `88d09e78f9d8a8f5d1867945065fb5c8f6e78ee9`.

`loaded_chain_probe.cpp` applies gravity to ten unit-mass links below a fixed
anchor for 240 steps at 1/120 second, using four joint iterations. It reports
final and peak bottom-link sag in metres, RMS frame-to-frame bottom-link sag
change over the last 120 steps, mean kinetic energy over those steps, peak
kinetic energy over the full run, and time spent inside the joint solve. Unit
mass and identity rotational inertia make energy `0.5 * (|v|² + |ω|²)` joules
per link. This fixture isolates joints; it does not measure contact solving,
rendering, or a complete ragdoll scene.

Each reported timing averages 100 complete chains. There are ten measurements
per executable and two alternating pre-(a)/stage-(a)/candidate rounds. The
reported cost divides total solve time, including warm start, by the number of
joint iterations. Setup and measurement arithmetic are outside the timed region.
Timing is observational, never a machine-dependent unit-test assertion.

Build the repository's portable Release support library, then run from the
repository root (substitute the local CMake installation):

```powershell
python Agentic/Plans/Artifacts/ragdoll-physics-unification/FP5/measure.py --repo . --cmake "C:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --support-library TestOutput/native-ui-purge-portable/Release/skullbonez_portable_cpu.lib
```

The script places isolated source copies, build outputs, and raw logs under
`TestOutput/ragdoll-physics-unification/FP5/measurements/`. `--run-only` repeats
measurements from already built probes.

The native performance gate currently stops on 91 pre-existing allocation
policy findings. The focused comparison is separate evidence and does not
turn that gate green.

## Results

Median over twenty measurements per implementation:

| Measurement | Before stage (a) | Stage (a) | FP5 |
|---|---:|---:|---:|
| Final sag, mm | 35.084 | 24.379 | 7.113 |
| Peak sag, mm | 50.000 | 44.105 | 7.113 |
| Settled sag-change RMS, mm/step | 0.373 | 0.281 | 1.730 |
| Settled mean kinetic energy, J | 0.34836 | 0.02275 | 0.18876 |
| Peak kinetic energy, J | 1.10979 | 0.99955 | 0.59670 |
| Solve cost, microseconds/joint/iteration | 0.14490 | 0.15299 | 0.28583 |

FP5 improves anchor pinning and peak energy, but costs 1.87 times stage (a)
and has more settled jitter and kinetic energy. The remaining ad-hoc damping
and positional correction are deliberately measured here; FP6 must evaluate
their replacement against these results. No jitter or timing baseline was
weakened. All raw timing rows and test outcomes are retained; repetitive capacity
announcements are omitted from these text copies. Unfiltered stdout remains
in the documented TestOutput measurement directory. Source/executable/support-library digests accompany
`measurements.json`.

## Native replay transition

The unchanged scene launches a sphere through the ragdoll before the wall.
The coupled joint response changes that interaction and the subsequent wall
cascade: the first recorded wall contacts move from frame 101 to frame 106.
Both captures still resolve all 200 wall bricks, 201 causal nodes, one
prediction generation, and 2,401 inclusive reveal samples. The new causal
order and vector-derived visual packets therefore replace the scalar-solver
oracle through the guarded Physics lane. Core varied-scene Physics CSV bytes
remain unchanged.

`golden-transitions/point-joint-vector-0a84ed99/manifest.json` binds the exact
old and new visual/causal hashes and their first-party Automation producers.
Dependency scans show no first-party DLL dependency. System and third-party
runtime dependencies are listed in the scans and are not bundled.

## Replay format boundary

Physics snapshot version 7 stores three world-space impulse components. Versions
3–6 retain their scalar wire encoding and historical hash interpretation.
Explicit Physics import reconstructs the scalar impulse on the restored anchor
error axis, with the historical X-axis fallback at coincident anchors.

Historical presentation and checkpoint inspection remain supported. App rejects
authoritative continuation from an older solver version during artifact
selection, before topology reconstruction or any live-state mutation. A new
solver cannot reproduce an older solver's exact saved hashes merely by migrating
its cache. Current-version saves retain exact restore/continuation verification.
The persisted-v6 regression covers scalar decoding, hash reconstruction,
presentation loading, and this early rejection boundary.
