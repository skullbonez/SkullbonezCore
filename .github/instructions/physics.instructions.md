---
applyTo: "SkullbonezSource/SkullbonezRigidBody*,SkullbonezSource/SkullbonezGameModelCollection*,SkullbonezSource/SkullbonezBoundingSphere*,SkullbonezSource/SkullbonezDynamicsObject*,SkullbonezSource/SkullbonezWorldEnvironment*,SkullbonezSource/SkullbonezSpatialGrid*,TestOutput/baselines/*.csv,TestOutput/baselines/physics_query*.json"
---

# Physics Guidance

Physics must remain deterministic. Solver, collision, broadphase, physics
config, physics CSV, or SkullScope baseline changes require
`tools\validate_physics.bat` at the PR/commit gate.

Physics-plan golden transitions use the standing noninteractive automated lane.
Supply the exact candidate SHA-256 and an append-only manifest retaining both
old and new launch payloads; no per-transition owner phrase is required. This
does not authorize unexplained or gate-clearing refreshes. See the root
`AGENTS.md` Physics Golden Governance section.

Use SkullScope query summaries for model analysis instead of pasting whole raw
CSV, NDJSON, or SQLite diagnostics. Report query cost when SkullScope is used.
