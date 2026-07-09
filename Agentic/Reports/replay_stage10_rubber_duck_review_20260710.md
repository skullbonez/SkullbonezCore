# Replay Stage 10 Rubber-Duck Review

Date: 2026-07-10
Branch: nightrunner-9th-july

Expected outcome: finish Stage 10 of
`Agentic/Plans/TODO/replay-visuals-prediction-and-memory.md` by inventorying
`Runtime/Replay`, deleting obsolete legacy replay visualizer paths, trimming
stale budget telemetry, right-sizing replay ribbon reserves, and proving the
result with the Stage 10 validation gates before deleting the completed plan.

Findings
- Blocking: none.
- Non-blocking: `RunEditorTracer::BuildReplayRibbonVertices()` still expands
  each replay segment into six repeated payload vertices for the existing
  `DrawTransientColoredTriangles()` DX12 path. The Stage 10 change right-sizes
  that staging from 22.75 MiB to 16.66 MiB and keeps the shader-expanded segment
  contract validated by the submitted-geometry probe, but a literal
  SRV/control-point-only backend API would be a separate renderer change.
- Non-blocking: the new 24,000-segment replay ribbon cap has 2,432 segments
  (11.3%) headroom over the wall-200 determinism probe's 21,568 submitted
  segments. That is validated for the known heavy replay trajectory scene, but
  it is not a full corpus sweep of every future replay scenario.

Missing evidence
- None for the plan's required gates. `validate_fast`, `validate_replay_scrub`,
  `validate_full`, and `validate_perf` all passed with logs under
  `Agentic/Reports/`.

Next step
- Update `Agentic/SessionState.md`, remove the completed plan row and TODO plan
  file per `MASTER-PLAN.md`, then commit and push the completed Stage 10 work.
