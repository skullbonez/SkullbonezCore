# Upload Arena Measurements

Date: 2026-07-12
Plan: `upload-arena-overflow-policy` A1/A2 evidence
Arena: 33,554,432 bytes per in-flight frame (32 MiB), two frames

## Instrumentation

`RenderMemoryStats` now reports the highest one-frame arena waterline, current
bytes, cold flushes, steady caller drops, and one-frame high-water by constants,
dynamic vertices, instance data, texture rows, and debug/prediction overlays.
The Memory tab and graphics-stress stdout surface the same owner rows.

Category totals count payload bytes; the arena waterline also includes alignment
padding. Texture rows are primarily cold scene/backend load work. The other
categories describe steady draw submissions.

## Workloads

| Workload | Peak / category evidence | Headroom and result |
|---|---|---|
| Normal scene: `replay_prediction_simple.scene.json`, 180 fixed frames | Arena 32,550,784; texture rows 29,597,568; dynamic vertices 2,952,912; constants 4,608; instances 1,792; overlay 0 | 1,003,648 bytes (2.99%) total cold-load headroom; 0 flushes, 0 drops; exit 0 in 4.08s |
| Largest legitimate prediction: 200-ragdoll wall, 3-second deterministic probe | Overlay 12,511,392; dynamic 2,952,912; instances 81,024; constants 5,632 | 21,043,040 bytes (62.72%) overlay headroom; 0 flushes, 0 drops; two deterministic runs passed in 59.82s |
| Deliberate truncation: same wall, 10-second prediction | Exactly 162,000 vertices / 27,000 segments / 8,424,000 payload bytes; two-pass overlay peak 16,946,496 | 16,607,936 bytes (49.49%) overlay headroom; path-visible report `ok=true`; 0 flushes, 0 backend drops; exit 0 in 66.71s |
| One-minute graphics stress | At frame 10,800: current arena use 2,241,488; dynamic peak 2,952,912; instances 52,480; constants 8,704; overlay 8,448; cold texture peak 29,597,568 | 0 flushes, 0 drops; descriptor churn PASS; 60.92s crash-free PID-scoped run |

The worst cold consumer is texture upload during load. It reaches 29,597,568
payload bytes but remains eligible for the explicit cold flush/retry safety net.
The worst steady consumer is the deliberately capped 10-second prediction
overlay at 16,946,496 bytes across its depth-hint and visible ribbon draws.

## Ribbon Budget Decision

The frozen 3-second wall probe's previously measured legitimate maximum is
21,568 segments. A 25% margin is 26,960, rounded to an owner-configured ceiling
of 27,000 segments (162,000 vertices). Ordinary paths receive 24,000 segment
slots and retained/causal priority evidence receives 3,000. At the ceiling one
draw uploads 8,424,000 bytes; both ribbon passes consume 16,848,000 payload
bytes before small alignment/constant overhead.

The 10-second proof remained visible and stable at exactly the ceiling. Its
interaction report recorded 2,636,807 cumulative per-render-frame omissions:
2,178 incoming-child, 112,354 outgoing-child, 2,119,675 retained-trail, and
402,600 causal-marker segments. These are lane-specific diagnostic counts, not
unique trajectory records; repeated steady frames intentionally accumulate the
same omitted presentation work so a persistent cap is visible in the Memory
tab and automation report.

## Evidence Paths

- Normal: `TestOutput/validation/upload_arena_normal_runtime_events.log`
- Legitimate heavy probe:
  `TestOutput/validation/upload_arena_heavy_prediction_probe_final.log` and
  `TestOutput/validation/upload_arena_heavy_runtime_events_final.log`
- Truncation:
  `TestOutput/validation/upload_arena_heavy_10s_report_final.json` and
  `TestOutput/validation/upload_arena_heavy_10s_runtime_events_final.log`
- Stress: `TestOutput/graphics_stress/latest_stdout.txt` and
  `TestOutput/validation/nightrunner_12th_upload_arena_graphics_stress_final.log`
