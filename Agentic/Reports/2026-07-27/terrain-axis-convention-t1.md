# Terrain Axis Convention — T1

Date: 2026-07-27
Branch: `nightrunner-26th-JUL-26`
Plan phase: T1

## Outcome

Terrain storage is now described directly as world-X-major:

- cache construction uses `worldXCell` rows and `worldZCell` columns;
- `LocatePolygon` computes the same named cells and `targetQuad`;
- the local split vector is `negativeLocalZ` over `localX` from the target
  quad's bottom-right post;
- `PhysicsTerrainView::HeightAndPlaneAt` uses the same cell names and cache
  order;
- every `quadric`, `orthagonal`, and `co-ordinate` spelling is gone;
- the former implicit float-to-bool branch is `localX == 0.0f`.

`QueryCollisionDataUnchecked` already delegates to the now-correctly-named
Physics view, so no duplicate axis convention remains there. Arithmetic and
evaluation order are unchanged.

## Validation

- Four focused terrain/support cases: 4/4 and 47/47 assertions.
- `tools\validate_physics.bat`: passes; both 44,401-row runs are byte-exact.
- `tools\validate_physics_deep.bat`: passes; all broad artifacts and known
  signatures match.
- SkullScope `physics_query_varied.json`: exact match.
- `tools\validate_format.bat`: passes.

No baseline, golden, configuration, or engine behavior changed.
