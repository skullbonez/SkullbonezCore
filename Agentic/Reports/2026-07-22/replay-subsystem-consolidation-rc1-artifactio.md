# Replay Subsystem Consolidation RC1 — Capture / ArtifactIO Split

Date: 2026-07-22

## Result

Capture owns only retained rings, committed-tick sampling, deterministic sample
hashes, and replay-reserve accounting. ArtifactIO now owns both cold
chronological materialization and every Replay hash-log file handle/formatting
operation.

## Ownership Changes

- `ReplayRecorder`, `ReplaySolverRecorder`, and `ReplayEventRecorder` no longer
  publish `Copy*Chronological` commands. `ReplayArtifactSource` is the exact
  const-only cold reader that reconstructs compact presentation/solver deltas
  and unwraps event rings inside the existing governed
  `ReplayV2Artifact.cpp` ArtifactIO translation unit.
- The three capture owners no longer include `<fstream>`, store file streams,
  open paths, format rows, or flush files. Their 185 deleted implementation
  lines were file/materialization authority, not live sampling.
- `ReplayArtifactHashLog` is one concrete ArtifactIO owner for paired
  presentation/solver streams. It derives the solver path, opens/truncates each
  stream independently, preserves the external CSV header/row ABI, and flushes
  at shutdown.
- `ReplayTimeline` sequences completed sample values to the hash-log owner only
  after Capture commits them. The paired solver/presentation fast path still
  performs one store walk and preserves the former sample/hash order.
- Artifact v2/v3/v4 format ownership remains solely in `ReplayV2Artifact`; it
  now asks `ReplayArtifactSource` for chronological values rather than invoking
  serialization-shaped methods on Capture.

No new production public-surface edge appears. `ReplayArtifactHashLog.h` is
consumed only by Replay Timeline; `ReplayArtifactSource.h` is consumed by
ArtifactIO and white-box tests. Product peers still see the RC0 surface.

## Frozen Behavior And Policy

- Hash-log path spelling, header text, column order, six-decimal time, fixed
  hexadecimal width, and reset/flush timing are unchanged.
- Artifact sample order and exact compact-frame reconstruction are unchanged;
  focused ring-wrap and artifact round-trip tests exercise the new owner.
- The reserve inventory remains exactly three owners. An initial standalone
  `ReplayArtifactSource.cpp` triggered six allocation-policy findings because
  it was a new, unregistered growth file. The implementation moved into the
  already governed cold `ReplayV2Artifact.cpp` unit; no allowlist row, owner,
  cap, phase gate, or counter changed.
- The plan's product public surface is unchanged. The test-only include of
  `ReplayArtifactSource.h` is recorded as white-box ArtifactIO coverage.

## Validation

- First focused Profile solution build: blocked after 16.7 s by one missing
  test namespace import; production linked cleanly. Final focused build passes
  in 8.3 s with zero warnings/errors.
- Focused Replay tests pass 10 cases / 180 assertions in 1.52 s.
- Allocation-policy repository scan passes: 409 files, zero allowlist errors;
  the three registered Replay growth owners are unchanged.
- First `tools\validate_tests.bat` attempt stopped in 1.3 s because the new
  source lacked a test project filter-prefix rule. The rule was added to the
  existing Replay prefix inventory. Final gate passes in 3.7 s: 99/99 test
  project/filter items and 343 cases / 68,693 assertions.
- RC1's single permitted `tools\validate_replay_visual_fidelity.bat` invocation
  ran 423.6 s. Launcher shape proves one engine process, one generation, one
  presented cascade, and zero nested scrub runs; Automation builds cleanly and
  typed controls pass 16 cases / 72 assertions. The oracle then stops on the
  pre-existing provenance mismatch: expected config SHA
  `83401df03cb6e212a6a74a38e815fc550d57aa983fc9b792c2c8f4e5c784a3f4`, actual
  `bd0bb719aad7231cf500ca9a61af7d2f017e557b1b18b7de82df7eb93a3b5d93`.
  No second invocation, config edit, or golden edit was made.
- First `tools\validate_full.bat` attempt stopped at the formatting preflight
  in 13.2 s. The one touched header received the repository's targeted comment
  alignment pass. Final broad gate passes in 167.4 s: 725/725 production
  project/filter items, CPU/coverage umbrella, five runtime lanes, zero DX12
  validation errors, accepted images, and byte-exact physics.
- Comment audit: 11 touched source-bearing files inspected, zero deferred or
  unchecked. Dependency-direction and downward-Replay proofs return no rows;
  new ArtifactIO files contain no callback, `void*`, exception, direct heap, or
  owner-registration seam.

Logs are retained under `TestOutput/validation/agent_logs/rc1_*`.

## Remaining Closure Evidence

`ReplayRecorder.cpp` and `ReplayV2Artifact.cpp` remain above the approximate
2,000-line target. RC1 has removed the cross-domain authority: the former is
now cohesive live Capture/compression across three synchronized tracks, while
the latter is cohesive ArtifactIO document encoding/decoding plus its cold
materialization reader. RC6 must either record reviewer-accepted cohesion for
each final survivor or reopen the appropriate split; their current size is not
silently waived.
