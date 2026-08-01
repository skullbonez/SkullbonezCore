# Look Lab Random Style Authoring Closure

Date: 2026-08-02
Plan: `Agentic/Plans/TODO/look-lab-random-style-authoring.md`
Result: LL0-LL6 complete; live portfolio 7/14 (50%)

## Closed Behavior

- F10 resolves and applies one deterministic, presentation-only SplitMix64
  candidate without changing camera, simulation, scene ownership, shader source,
  or resource-quality policy. F5 and F6 retain their diagnostic bindings.
- F11 publishes one exact three-artifact bundle through detached Direction,
  Scene, and Capture value boundaries. Style and receipt writes are atomic, PNG
  capture occurs after world/UI drawing and before Present, and partial/cancelled
  transactions publish honest final status.
- The standalone schema now round-trips all 84 cinematic atoms, including the
  four shadow-participation values represented by the grouped participation bit,
  plus the three ordered material rules. All 23 curated styles parse and pass
  through the production cinematic merge.
- Generator fog derivation and validation use bounded integer-Q12 rules before
  conversion. The canonical candidate encoding is exactly 649 bytes, and the
  final 65,536-seed stream is fully valid and unique.
- Publication failures cross one cohesive `LookLabBundlePublication` port.
  Explicit F10/F11 parsing, production cinematic application, mutation controls,
  false sentinels, fresh-process reuse, and huge-finite fog rejection are covered.

The corrected current-source census is 84 atoms, 12 nested shadow values, and
63 live override bits. The four explicit participation values are
`terrainCasts`, `objectsCast`, `terrainReceives`, and `objectsReceive`.

## Determinism And Reuse Evidence

- Candidate fingerprint: `709160cd850d1846`.
- Debug/Profile 65,536-seed ordered stream hash: `3f5d4c4608cca5a2`.
- Serialized false-sentinel hash: `a79343801bda3d50`.
- Focused Debug and Profile runs each passed 16/16 cases and 6,272/6,272
  assertions. The census produced 65,536 valid, 65,536 unique candidates with
  zero invalid or off-grid floats.
- Debug and Profile `SceneSnapshotWriter:*` each passed 1/1 case and 500/500
  assertions.
- A fresh Automation process loaded the saved style with `ok=true`. Its
  `ll6_reload.bmp` and the source `ll6_look_3.bmp` were byte-identical at SHA-256
  `7705824d59f1fd3a689e8a30992907138a10153b27b2f1d6506fc47aef3fc51b`.

## Visible DX12 Evidence

The waited DX12 session scheduled deterministic F10/F11 actions after an
initial direct OS key tap did not reach the guarded input edge. That missed tap
is not counted as evidence. Three captured looks were visibly distinct and had
these SHA-256 values:

1. `b761ba107caef66b14dee360174cfaa331b44b285e97e7d09c52472f13ed31f9`
2. `6489a89efd0c68062b9a61e1b7b5d71cff4fd3b7ec1e131298f3f421ad2135f7`
3. `7705824d59f1fd3a689e8a30992907138a10153b27b2f1d6506fc47aef3fc51b`

The final bundle was
`LookLab/2026-08-02_04-52-20_seed_dfbaefa1847f807d/`, using recipe
`studio_high_key` and seed `dfbaefa1847f807d`. The complete style and receipt
record all four shadow-participation values as true. Artifact hashes were:

- `look.style.json`: `a185263af109dde7fc35f3089166b1a8063c35c8e6fb01ae4cb0a689e6e0e179`
- `look.txt`: `e2ae50666ec1004bd1cf0693e4194d867b3feb7ebe8f49a217add9edee09b967`
- `look.png`: `2f7ae61143561dc2d63e8166e7140f16c94faefcfc826ee804877ef355d372e1`

Generated Look Lab and `TestOutput` artifacts remain ignored and were not
staged. No tracked visual, Physics, DX12, or Replay baseline changed.

## Governance And Review

All seven current-source inventories passed: build configuration, compiled
reachability (79 rows), invariant aggregates (1,203 rows), extraction scars
(1/1 ruled), wide signatures (32/32 ruled), function complexity (40/40 ruled),
and glossary terms. Dependency validation reported zero findings.

The first performance gate exposed three previously unrecorded explicit-action
allocation sites. Narrow cold-path allowlist rows now describe Capture's
request-gated PNG encoder, the controller's exactly-three-rule detached
publication, and the generator's exactly-649-byte canonical encoder. The direct
allocation checker then passed with zero allowlist errors. These rows do not add
or expand a retained post-gameplay growth privilege, `RuntimeReserveAllocator`
registration, cap, counter, or Replay inventory entry.

The touched-source comment audit inspected 14/14 files with zero deferrals. It
corrected stale shadow-policy language and found no unresolved local glossary or
`Related:` claim. This was a touched-file audit, so no subsystem checklist was
required.

Independent read-only review first identified coverage, integer-domain,
publication-boundary, production-apply, shadow-participation, and allocation-
policy risks. After the repairs and targeted evidence above, the reviewer
returned **CLEAN** for source, tests, visible/reload evidence, ownership, failure
atomicity, input conflicts, idle behavior, test sensitivity, and all three
allocation-policy rows.

## Final Validation

| Gate | Result | Elapsed |
|---|---|---:|
| `validate_format.bat` | Pass; 586 source files, 326 headers, zero unresolved `Related:` paths | 49.36 s |
| `validate_tests.bat` | Pass; 478 cases, 2,429,463 assertions | 47.79 s |
| `validate_fast.bat` | Pass; all 9 stages | 398.58 s |
| `validate_dependency_graph.bat` | Pass; zero findings | 3.36 s |
| `validate_physics.bat` | Pass | 27.59 s |
| `validate_dx12_renderer.bat` | Pass | 61.11 s |
| `run_graphics_stress.bat 1` | Pass; bounded timeout stop | 61.07 s |
| `validate_perf.bat` | Pass after the precise cold-path policy rows; no baseline refresh | 68.35 s |
| `validate_full.bat` | Pass; integrated coverage, automation, DX12, Physics, and ready-build proof | 627.8 s |

Debug, Profile, and Automation final builds also passed. The final allocation
policy scan reported 476 files, 37 direct-heap findings, 85 dynamic-STL-member
findings, 654 STL-growth findings, and zero allowlist errors. No baseline was
refreshed or replaced.
