# Replay capture and physics sleep investigation

Date: 2026-08-30
Branch: `codex/replay-capture-bugfixes`
Runtime fix commit: `9feb68160 Fix replay scrubbing and playback pacing`

## Outcome

The replay scrub and playback-speed bugs are fixed and committed. The unchanged
recording now plays in 22.517 seconds instead of 122.347 seconds, retains at
least 20 seconds of visual history, scrubs on the presentation track, and does
not change camera mode while scrubbing.

The sleep investigation found a different problem from insufficient friction.
The sleep controller joins every persistent dynamic-to-dynamic contact into one
component and makes one sleep decision for the whole component. A nearly still
brick therefore cannot advance its quiet counter while any connected brick is
moving, unsupported, or sleep-inhibited. Contact graph changes also change the
component root and reset the shared counter. In the wall trace this keeps quiet
bricks awake for roughly 11--13 simulated seconds.

A local experiment that retained tangent friction on unsupported terrain-edge
contacts reduced the peak motion in the two-brick edge case, but made the
representative wall substantially worse. It is deliberately uncommitted and is
not a candidate fix.

## F2, F3, and deterministic continuation

- F2 is `SaveSceneSnapshot`; F3 is `SaveScreenshot`.
- F2 writes durable scene state including body pose, velocity, sleep flag,
  collider, material, and authored scene data.
- F2 is not a lossless physics checkpoint. It does not serialize the persistent
  solver/contact cache, sleep-history counters, or all other transient solver
  ownership needed to resume on the exact same future bits.
- Determinism means that the same complete initial state, build, assets,
  configuration, fixed-step sequence, inputs, floating-point environment, and
  ordering reproduce the same result. A loaded F2 scene may be very close, but
  bit-perfect continuation is not promised.
- The engine's lossless determinism test restores both the body state and the
  solver snapshot. That is the stronger mechanism an exact checkpoint needs.

Relevant source:

- `SkullbonezSource/Runtime/Input/InputController.Bindings.cpp:89`
- `SkullbonezSource/Scene/SceneSnapshotWriter.cpp:261`
- `SkullbonezTests/TestDeterminism.cpp:2262`

## Replay scrub and playback

### Scrub behavior

- Visual replay retention now has a 20-second minimum at 120 Hz.
- Ordinary scrub uses the presentation history, while prediction inspection
  continues to use solver history.
- Scrub selection no longer requests a camera focus/tween. Camera transitions
  remain limited to explicit focus and live-advance actions.
- Solver diagnostics retain their bounded high-body-count policy; they are not
  inflated past the 512 MiB policy solely to match visual history.

### Playback pacing and PIX

The PIX capture did not show a slow render workload. Across the inspected
capture, Present cadence had a 16.7246 ms median, every Present used sync
interval 1, and the Present API call had a 0.1292 ms median. Playback submitted
one recorded turn per host frame, so VSync limited the run to about 60 recorded
turns per second even though the recording represented a 120 Hz fixed step.

Recorded playback now disables VSync presentation pacing while preserving one
recorded turn per runtime turn and the same 120 Hz physics step.

### Replay validation and timings

| Operation | Result | Wall time |
|---|---:|---:|
| Exact unchanged recording, before fix | exit 0, report `ok`, 7,239 frames | 122.347 s |
| Exact unchanged recording, after fix | exit 0, report `ok`, 7,239 frames | 22.517 s |
| Focused replay tests | 6 cases, 105 assertions | 1.997 s |
| Dependency graph gate | pass | 3.832 s |
| Profile build | pass | 22.417 s |
| Automation build | pass | 16.364 s |
| `validate_fast` | stopped on pre-existing plan wording | 13.118 s |

The final replay observation remained in Scene camera mode and recorded no
camera transition. The supplied manifest and its sidecars were not changed.

`validate_fast` reaches an unrelated tracked documentation failure in
`Agentic/Plans/WNF/engine-signature-and-context-cohesion.md` at lines 37 and 64
for disallowed historical wording. No unrelated file was changed.

## Minimal sleep scenes

Both scenes run for 3,600 fixed frames. They use a small terrain tile near
`(500, 0, 500)` and only two dynamic boxes.

| Scene | Arrangement | Baseline result |
|---|---|---|
| `sleep_test_edge.scene.json` | One face-down brick; one brick starts tilted 25 degrees with an edge near the ground and its face against the stable brick | Leaner sleeps at tick 140 (1.167 simulated seconds); 118 inhibited rows and 3 counter resets; peak energy 339.357 and peak angular speed 2.638 |
| `sleep_test_corner.scene.json` | One face-down brick; a second begins in a shallower corner-biased lean | Both sleep at tick 35 (0.292 simulated seconds); no inhibition or resets |

The first versions placed the pair at the world origin, outside the authored
terrain tile. The ground brick fell, so those traces are retained only as setup
failures and were not used to diagnose sleep.

## Wall evidence

The 6,800-tick wall run represents 56.658 simulated seconds and took about
357 seconds with full diagnostics. All wall bricks eventually slept; the awake
body at the end was the striker.

| Body | First relevant awake tick | Last awake tick | Sleep tick | Inhibited awake rows | Quiet-counter resets |
|---|---:|---:|---:|---:|---:|
| `prediction_wall_brick_r09_c06` | 179 | 1478 | 1479 | 0 | 5 |
| `prediction_wall_brick_r09_c09` | 175 | 610 | 611 | 38 | 1 |
| `prediction_wall_brick_r09_c11` | 175 | 1478 | 1479 | 312 | 5 |

`r06_c04` was the slowest wall brick and remained awake through tick 1609.

At frame 1400, sleep island 47 contains ten bricks. `r09_c06` is already
effectively still (`speed=0.001027`, `omega=0.000465`) but the same island
contains `r09_c07` (`speed=0.688047`, sleep inhibited) and `r09_c11`
(`speed=0.451863`, sleep inhibited). The component is therefore ineligible.
At frame 1415 the contact graph produces island 73 with thirteen bodies;
`r09_c06` remains nearly still at `0.000954`, while two other members still
block the component. The root returns to island 47 by frame 1420. `r09_c06`
and `r09_c11` share their last counter reset at tick 1417 and sleep together at
1479.

The source matches the trace. `PhysicsSleepController` unconditionally unions
both dynamic bodies for every persistent contact, then marks the root
ineligible when any awake member is not quiet, supported, or permitted to
sleep. See `SkullbonezSource/Physics/Stages/PhysicsSleepController.cpp:531`.

Terrain edge and point contacts intentionally inhibit sleep until a stable
resting footprint exists. That protects against freezing a genuinely balanced
or toppling box. It explains the inhibited rows for `r09_c11`, but not why the
already-quiet `r09_c06` must remain awake: that delay comes from connected-
component coupling.

## Rejected local physics experiment

The uncommitted experiment changes the low-speed unsupported terrain-contact
branch in `PersistentContactSolver.cpp` to clear only normal mass while keeping
tangent masses. Its focused unit assertion verifies that an unsupported tilted
edge with lateral velocity receives a friction impulse.

| Run | Baseline | Experiment | Interpretation |
|---|---:|---:|---|
| Minimal edge peak energy | 339.357 | 224.83 | Less initial motion |
| Minimal edge peak angular speed | 2.638 | 1.450 | Less rotation |
| Minimal edge sleep tick | 140 | 173 | Settles later despite less peak motion |
| Minimal corner | sleep tick 35 | byte-identical trace, tick 35 | Branch is irrelevant here |
| Wall at tick 1999 | 210 sleeping, 2 awake | 179 sleeping, 33 awake | Severe regression |

The 2,000-tick experiment wall trace took 143.706 seconds. `r09_c06` and
`r09_c11` were still awake at tick 1999; `r09_c09` remained awake through 1082.
Keeping unsupported-edge friction appears to transmit low-level motion through
the contact network and prolong the topology changes that already block whole-
component sleep.

The experiment passed the focused doctest (39 assertions; latest isolated run
0.049 seconds) and a Debug build (0 warnings/errors, 7.919 seconds), but the scene-level result
rejects it. No Physics source or Physics test change is staged or committed.

## Recommended fix direction

Do not tune friction or merely raise the quiet thresholds. The next Physics
change should redesign the sleep-component rule so stable, quiet support groups
can progress independently of transient neighboring contacts, while preserving
deterministic wake propagation when a moving body transfers meaningful impulse.
This needs an explicit support/contact-edge classification and a regression
suite built from the two minimal scenes plus a larger wall/mega-sleep scene.

Acceptance should measure maximum sleep tick and require zero later wakeups over
a fixed observation tail. It should also retain a negative case in which an
edge-balanced box must continue moving rather than being frozen early.

## SkullScope artifacts

All raw NDJSON and SQLite files stayed on disk; none was read wholesale by GPT.

| Trace-generation command | Wall time | NDJSON bytes | SQLite bytes |
|---|---:|---:|---:|
| `Debug\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --shadows off --fixed-step --scene SkullbonezData\scenes\prediction_ragdoll_wall_200.scene.json --frames 6800 --physics-diag TestOutput\sleep_investigation\baseline_wall\prediction_wall_6800.physicsdiag.ndjson` | about 357 s | 2,202,799,902 | 1,094,770,688 |
| `Debug\SKULLBONEZ_CORE.exe --renderer dx12 --automation-hidden-window --vsync off --shadows off --fixed-step --scene SkullbonezData\scenes\sleep_test_edge.scene.json --physics-diag TestOutput\sleep_investigation\baseline_edge_valid\sleep_test_edge.physicsdiag.ndjson` | 20.258 s | 15,056,258 | 6,864,896 |
| `Debug\SKULLBONEZ_CORE.exe --renderer dx12 --automation-hidden-window --vsync off --shadows off --fixed-step --scene SkullbonezData\scenes\sleep_test_corner.scene.json --physics-diag TestOutput\sleep_investigation\baseline_corner_valid\sleep_test_corner.physicsdiag.ndjson` | 20.286 s | 14,617,581 | 6,709,248 |
| `Debug\SKULLBONEZ_CORE.exe --renderer dx12 --automation-hidden-window --vsync off --shadows off --fixed-step --scene SkullbonezData\scenes\sleep_test_edge.scene.json --physics-diag TestOutput\sleep_investigation\prototype_edge\sleep_test_edge.physicsdiag.ndjson` | about 20.3 s | 15,372,250 | 6,975,488 |
| `Debug\SKULLBONEZ_CORE.exe --renderer dx12 --automation-hidden-window --vsync off --shadows off --fixed-step --scene SkullbonezData\scenes\sleep_test_corner.scene.json --physics-diag TestOutput\sleep_investigation\prototype_corner\sleep_test_corner.physicsdiag.ndjson` | about 20.3 s | 14,617,581 | 6,709,248 |
| `Debug\SKULLBONEZ_CORE.exe --renderer dx12 --automation-hidden-window --vsync off --shadows off --fixed-step --scene SkullbonezData\scenes\prediction_ragdoll_wall_200.scene.json --frames 2000 --physics-diag TestOutput\sleep_investigation\prototype_wall\prediction_wall_2000.physicsdiag.ndjson` | 143.706 s | 888,946,482 | 455,237,632 |

Setup-failure traces, not used for findings:

| Trace-generation command | NDJSON bytes | SQLite bytes |
|---|---:|---:|
| `Debug\SKULLBONEZ_CORE.exe --renderer dx12 --automation-hidden-window --vsync off --shadows off --fixed-step --scene SkullbonezData\scenes\sleep_test_edge.scene.json --physics-diag TestOutput\sleep_investigation\baseline_edge\sleep_test_edge.physicsdiag.ndjson` | 15,804,159 | 7,696,384 |
| `Debug\SKULLBONEZ_CORE.exe --renderer dx12 --automation-hidden-window --vsync off --shadows off --fixed-step --scene SkullbonezData\scenes\sleep_test_corner.scene.json --physics-diag TestOutput\sleep_investigation\baseline_corner\sleep_test_corner.physicsdiag.ndjson` | 15,780,790 | 7,593,984 |

Two earlier hidden-window launches omitted `--automation-hidden-window`, reached
a zero-sized DX12 client area, and stopped in about 1.09 seconds before writing
a usable trace.

### SkullScope query record

Path keys below expand as follows:

- `W`: `TestOutput\sleep_investigation\baseline_wall\prediction_wall_6800.physicsdiag.ndjson`
- `PW`: `TestOutput\sleep_investigation\prototype_wall\prediction_wall_2000.physicsdiag.ndjson`
- `E`: `TestOutput\sleep_investigation\baseline_edge_valid\sleep_test_edge.physicsdiag.ndjson`
- `C`: `TestOutput\sleep_investigation\baseline_corner_valid\sleep_test_corner.physicsdiag.ndjson`
- `PE`: `TestOutput\sleep_investigation\prototype_edge\sleep_test_edge.physicsdiag.ndjson`
- `PC`: `TestOutput\sleep_investigation\prototype_corner\sleep_test_corner.physicsdiag.ndjson`
- `IE`: `TestOutput\sleep_investigation\baseline_edge\sleep_test_edge.physicsdiag.ndjson`
- `IC`: `TestOutput\sleep_investigation\baseline_corner\sleep_test_corner.physicsdiag.ndjson`

Every `tools\physics_query.bat` invocation is listed below. Character and byte
counts are equal for these ASCII JSON/help responses.

| Query (replace the path key with its path above) | GPT-read output |
|---|---:|
| `tools\physics_query.bat W summary` (initial) | 8,961 chars |
| `tools\physics_query.bat W body --help` | 559 chars |
| `tools\physics_query.bat W events --help` | 609 chars |
| `tools\physics_query.bat W stacks --help` | 392 chars |
| `tools\physics_query.bat W summary` (repeat) | 8,961 chars |
| `tools\physics_query.bat W events --type failed_to_sleep,sleep_inhibited_quiet --limit 20` | 392 chars |
| `tools\physics_query.bat W body prediction_wall_brick_r09_c06 --frames 0:6799 --limit 40` | 32,209 chars |
| `tools\physics_query.bat W body prediction_wall_brick_r09_c09 --frames 0:6799 --limit 40` | truncated batch; full response 32,247 chars, exact delivered count unavailable |
| `tools\physics_query.bat W body prediction_wall_brick_r09_c11 --frames 0:6799 --limit 40` | truncated batch; full response 32,289 chars, exact delivered count unavailable |
| `tools\physics_query.bat W stacks --frames 0:6799 --limit 30` | 10,558 chars |
| `tools\physics_query.bat W sql --help` | 389 chars |
| `tools\physics_query.bat W sql <sqlite-table-list>` | 743 chars |
| `tools\physics_query.bat W sql <bodies-table-schema>` | 3,210 chars |
| `tools\physics_query.bat W sql <top-wall-sleep-statistics>` | 5,775 chars |
| `tools\physics_query.bat W sql <three-target-statistics>` | 856 chars |
| `tools\physics_query.bat W sql <three-target-counter-resets>` | 641 chars |
| `tools\physics_query.bat W sql <three-target-sampled-frames>` | 11,829 chars |
| `tools\physics_query.bat W contacts --help` | 710 chars |
| `tools\physics_query.bat W solver --help` | 631 chars |
| `tools\physics_query.bat W pipeline --help` | 469 chars |
| `tools\physics_query.bat W contacts --body prediction_wall_brick_r09_c11 --frames 1180:1480 --top slip --limit 40` | 20,211 chars |
| `tools\physics_query.bat W contacts --body prediction_wall_brick_r09_c06 --frames 1180:1480 --top slip --limit 40` | truncated batch; full response 20,004 chars, exact delivered count unavailable |
| `tools\physics_query.bat W solver --frames 1180:1480 --include-convergence --limit 40` | truncated batch; full response 28,189 chars, exact delivered count unavailable |
| `tools\physics_query.bat W pipeline --frames 1380:1480 --limit 40` | 12,417 chars |
| `tools\physics_query.bat W contacts --body prediction_wall_brick_r09_c11 --frames 1400:1420 --top slip --limit 10` | 4,997 chars |
| `tools\physics_query.bat W contacts --body prediction_wall_brick_r09_c06 --frames 1400:1420 --top slip --limit 10` | 5,255 chars |
| `tools\physics_query.bat W sql <contacts-table-schema>` | 2,210 chars |
| `tools\physics_query.bat W sql <target-position-and-motion-samples>` | 5,687 chars |
| `tools\physics_query.bat W frame 1999` | 11,888 chars |
| `tools\physics_query.bat W island --help` | 462 chars |
| `tools\physics_query.bat W body prediction_wall_brick_r09_c11 --frames 1400:1420 --limit 5` | 4,798 chars |
| `tools\physics_query.bat W island 47 --frame 1400 --limit 20` | 2,379 chars |
| `tools\physics_query.bat W island 73 --frame 1415 --limit 20` | 2,903 chars |
| `tools\physics_query.bat W island 47 --frame 1420 --limit 20` | 2,380 chars |
| `tools\physics_query.bat PW summary` | 9,031 chars; initial response size not surfaced, deterministic size-only repeat captured no JSON for GPT |
| `tools\physics_query.bat PW events --type failed_to_sleep,sleep_inhibited_quiet --limit 20` | 6,893 chars |
| `tools\physics_query.bat PW sql <top-wall-sleep-statistics>` | 5,124 chars |
| `tools\physics_query.bat PW sql <three-target-statistics>` | 859 chars |
| `tools\physics_query.bat W compare PW --frames 0:1999` | 1,330 chars |
| `tools\physics_query.bat E summary` | 2,614 chars |
| `tools\physics_query.bat E events --limit 20` | 394 chars |
| `tools\physics_query.bat E sql <two-body-sleep-statistics>` | 914 chars |
| `tools\physics_query.bat E sql <counter-resets>` | 528 chars |
| `tools\physics_query.bat C summary` | 2,621 chars |
| `tools\physics_query.bat C events --limit 20` | 402 chars |
| `tools\physics_query.bat C sql <two-body-sleep-statistics>` | 918 chars |
| `tools\physics_query.bat C sql <counter-resets>` | 539 chars |
| `tools\physics_query.bat IE summary` | 2,651 chars |
| `tools\physics_query.bat IC summary` | 2,682 chars |
| `tools\physics_query.bat PE summary` | 2,591 chars |
| `tools\physics_query.bat PE events --limit 20` | 384 chars |
| `tools\physics_query.bat PE sql <two-body-sleep-statistics>` | 903 chars |
| `tools\physics_query.bat E compare PE` | 1,136 chars |
| `tools\physics_query.bat PC summary` | 2,611 chars |
| `tools\physics_query.bat PC events --limit 20` | 392 chars |
| `tools\physics_query.bat PC sql <two-body-sleep-statistics>` | 908 chars |
| `tools\physics_query.bat C compare PC` | 1,091 chars |
| `tools\physics_query.bat E compare --help` | 485 chars |
| `tools\physics_query.bat --help` | full response 1,841 chars; captured only to measure, 0 query-output chars shown to GPT |

The five deterministic size-only repeats for the four truncated rows and the
prototype-wall summary captured their JSON in the shell and printed only the
length; GPT read 0 JSON characters from those repeats.

Known GPT-read SkullScope output totals 202,451 characters. Because two earlier
multi-query tool responses were truncated before the host exposed each
delivered character count, the exact total lies between 202,451 and 326,052
characters. Truncation status: **yes**, limited to those exploratory batches;
all final island queries were bounded and untruncated. Raw trace and SQLite
bytes are not included in the GPT-read total.
