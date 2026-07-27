# Terrain Index Safety T2

Date: 2026-07-27
Plan: `terrain-legacy-and-contact-seed-remediation`
Phase: T2
Result: PASS

## Ruling

`Terrain::LocatePolygon` remains in place. Its only production caller,
`PhysicsDebugVisualizer::EmitTerrainContactProbe`, needs the exact three
vertices of the selected terrain triangle. `PhysicsTerrainView` exposes the
guarded height and plane, but not those vertices. Moving the caller would
therefore change the diagnostic drawing and is outside this byte-exact phase.

## Local Bound Proof

For a heightfield with `postsPerSide = N`, `quadsPerSide = N - 1`. The new
cell guard admits only world-X and world-Z cells in `[0, N - 2]`. The four
named post indices are then:

- previous row: `x * N + z`
- previous-row next: `x * N + z + 1`
- target: `(x + 1) * N + z`
- target next: `(x + 1) * N + z + 1`

Their closed range is `[0, N² - 1]`. An `Invariant:` comment records that
derivation at the reads, and a lane-F backing-size guard checks it against the
actual `m_postData` vector before any `operator[]`.

The cell guard also rejects invalid grid geometry and reports the query,
derived cells, and `quadsPerSide`. A 4-by-4 heightfield probe at its exact
upper X edge terminates in Debug with:

`FATAL[Terrain]: Terrain polygon cell out of range: x=3.000 z=0.000 worldXCell=3 worldZCell=0 quadsPerSide=3.`

## Visual Equivalence

The triangle split, relative-coordinate arithmetic, winding, and all four
post reads are unchanged; only the former expressions have named indices and
pre-read guards. The debug visualizer caller is unchanged, so an in-bounds
query draws the same polygon.

## Validation

- `tools\validate_format.bat`: PASS.
- `tools\validate_tests.bat`: PASS, 418/418 tests and 2,410,193 assertions.
- Direct Debug fatal child: PASS; non-zero exit at the local cell guard with
  the expected cell and bound diagnostics.
- Focused terrain/support oracle: PASS, four cases and 47 assertions.
- `tools\validate_physics.bat`: PASS; the 44,401-row physics CSV remains
  byte-exact and Profile/Debug builds succeed.

No baseline, golden, config, replay, SkullScope, visual, or DX12 artifact moved.
