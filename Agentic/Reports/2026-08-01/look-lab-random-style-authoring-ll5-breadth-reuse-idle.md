# Look Lab Random Style Authoring — LL5 Breadth, Reuse, And Idle Evidence

Date: 2026-08-01
Plan: `Agentic/Plans/TODO/look-lab-random-style-authoring.md`
Phase: LL5
Result: PASS

## Outcome

Look Lab version 1 produces a broad, valid, exactly repeatable candidate stream,
and its saved output is a complete standalone style rather than a recipe or
defaults reference. The idle frame path no longer observes Look Lab lifecycle or
status state: App publishes a detached status value only after an authoring,
capture, cancellation, or scene-transition event, and UI retains that value for
frame composition.

The large census also found and repaired a real generator defect. Twenty of the
first 65,536 seeds could produce a high fog start with a span below the parser's
15-percent separation rule. Generator version 1 now lifts only those previously
invalid results to the validator boundary without adding a random draw or
changing any already-valid candidate bytes.

## Deterministic 65,536-Seed Census

Debug and Profile independently produced the same complete census:

| Measurement | Result |
|---|---:|
| Seeds | 65,536 |
| Invalid candidates | 0 |
| Unique candidate fingerprints | 65,536 |
| Ordered fingerprint-stream hash | `3d8c96ba5b80788d` |
| Palette channel envelope, Q1e6 | 0..1,608,887 |
| Luminance envelope, Q1e6 | 4,810..880,780 |
| Per-candidate contrast envelope, Q1e6 | 42,473..465,765 |

Recipe counts, in enum order, were:

```text
4670,4567,4750,4665,4835,4635,4629,4638,4614,4687,4722,4736,4640,4748
```

Feature counts for clouds, volumetric lighting, god rays, bloom, fog, terrain
relief, non-off water, and emissive material rules were:

```text
28174,26198,17058,57032,29941,35153,46760,13919
```

Mode distributions were:

```text
sky      4707,2301,2292,2357,2321,2278,2388,4671,2333,2393,2337,2301,2337,4638,0,2425,7117,2364,4795,2266,4615,2300
terrain  4684,4611,2276,7013,2406,2380,4742,2267,7040,4690,2362,2379,4560,7078,4695,2353
object   14128,9252,9264,9305,4687,9498,4567,4835
water    18776,18791,4722,13932,9315
material 13992,13918,14124,13919,14270,14027,13932,14028,14027,14192,13924,14115,13943,14197
```

The zero count is the intentionally unsupported procedural-sky enum value; the
LL0 contract excludes it. Every supported branch remains reachable in the
existing 4,096-seed branch census.

## Fresh-Process Reuse And Curated Compatibility

`tools/validate_look_lab_reuse.py` launches a producer and consumer as distinct
OS processes. The producer generates seed `5eedf11a11c0ffee` and writes the
complete production schema-v1 style. The consumer does not invoke the generator
or a material catalog; it parses the file, verifies the full cinematic override
mask and all three material rules, and reserializes the same bytes.

Debug and Profile both passed with this exact artifact identity:

```text
sha256 9dbbe38c56938c99563dc8fabce413ac8b82ad4a1cd9c2892b3ffebcfe052f52
```

The compatibility census enumerates `SkullbonezData/styles` and loads all 23
tracked `.style.json` files through the production parser. Debug and Profile
each pass all 13 Look Lab cases and 4,310 assertions.

## Idle Cost

Timing uses the LL0 command shape: Profile DX12, vsync off, fixed step, Replay
off, 180 frames per internal pass, and `perf_1000.scene.json`. Each file contains
two 150-sample steady-state passes after warm-up.

| Evidence | Pass 1 `Frame/Input` mean | Pass 2 mean | Samples | Look Lab marker |
|---|---:|---:|---:|---|
| LL0 baseline | 0.119154 ms | 0.108307 ms | 300 | absent |
| LL5 timing run 1 | 0.090471 ms | 0.084067 ms | 300 | absent |
| LL5 timing run 2 | 0.077508 ms | 0.097155 ms | 300 | absent |

Separate waited runs added `--allocation-guard gameplay`; both exited 0, each
produced 300 samples, and neither emitted a Look Lab marker. Their instrumented
input means are not compared to the uninstrumented LL0 timing floor.

The idle call-path review found and removed two frame-path touches:

- `RunInputPhase` no longer observes a Look Lab lifecycle packet each frame.
  App already owns every scene transition and explicitly clears Look Lab before
  the scene can change.
- `RenderOperatorUiPhase` no longer polls `LookLabController::Status`. App maps
  status into a detached UI value after each possible status transition, and
  UI retains only that value.

When F10/F11 are idle, no Runtime call reaches the generator, bundle writer,
standalone style writer, capture completion, Scene style application, material
publication, shader compilation, or scene-load path. The two guarded exit-zero
runs cover allocation policy; source structure and absent markers cover the
other prohibited work.

## Ownership And Comment Review

All seven current-structure inventories pass. The two edited triggered bodies
retain their existing cohesive owners with fresh exact digests: Runtime's input
turn and operator-UI composition phase. Reachability passes against refreshed
Debug, Profile, and Automation objects. Dependency and project ownership,
build-configuration consistency, aggregate ownership, extraction scars, wide
signatures, glossary ownership, and formatting are clean.

The `comment-style-audit` touched-file checklist is the exact 12-file source/tool
set in this phase's diff: 12 checked, 0 deferred, 0 unchecked. Headers and nearby
comments now explain the generator boundary repair, detached UI cache, explicit
scene-transition clearing, large-census invariant, and fresh-process tool
contract. The stale claim that Look Lab still performed generation-based
lifecycle observation was removed. No glossary wording remains for owner review.

## Validation

- Debug and Profile solution builds: PASS
- Automation build for reachability evidence: PASS
- Debug/Profile `--test-case=*Look Lab*`: PASS, 13 cases / 4,310 assertions each
- Debug/Profile fresh-process reuse tool: PASS, identical SHA-256
- Two matching-condition idle timing runs: PASS, all four means below LL0
- Two gameplay-allocation-guard idle runs: PASS, exit 0
- 23 curated style parser census: PASS in both configurations
- Seven ownership inventories: PASS
- Dependency/project ownership, build configuration, formatting, Related paths,
  and `git diff --check`: PASS
- `tools\validate_fast.bat`: PASS in 384.9 seconds
- `tools\validate_full.bat`: PASS in 605.3 seconds
