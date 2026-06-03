# Broadphase Collision Plan

## Goal

Reduce the physics candidate-pair cost without changing solver behavior or physics CSV output.

The current fallback path checks too many object pairs. Add a simple uniform spatial grid so only objects in the same or neighboring cells become narrowphase candidates.

## Proposed Shape

- Add `SkullbonezSpatialGrid` as a small physics-side helper.
- Insert dynamic collision objects each fixed physics step.
- Query overlapping or neighboring cells to build candidate pairs.
- Keep pair ordering deterministic before handing candidates to the solver.
- Add a lightweight broadphase visualizer so the grid can be inspected during physics demos.

## First Implementation

1. Use object bounds to compute occupied grid cells.
2. Generate candidate pairs from each occupied cell.
3. Remove duplicate pairs before narrowphase.
4. Fall back to the old all-pairs path if the grid is disabled or empty.
5. Validate with `Agentic\Plans\agent-loop\run_perf_demo_visible.bat`.

## Open Questions

- Cell size may need tuning against the current physics scenes.
- Caching is not designed yet. Start by rebuilding the grid every fixed step; revisit if `GridBuild`, `CandidatePairs`, or `Physics` markers stay hot.
- Static/sleeping objects might be cacheable, but the first pass can treat all objects as dynamic.
- Duplicate-pair removal can be simple at first, even if it means sorting too many generated pairs.
- Visualizer should not run in perf capture unless explicitly enabled.

## Done When

- Physics regression CSV remains byte-exact.
- OpenGL perf capture shows lower physics/frame cost than the all-pairs path.
- Broadphase visualizer makes the active grid cells visible during the regression demo.
