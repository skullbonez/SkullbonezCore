---
applyTo: "TestOutput/baselines/**"
---

# Baseline Guidance

Baseline changes are behavior changes unless they are outside this folder and
documentation-only.

Visual baseline updates require DX12 renderer validation and intentional diff
review. Physics CSV and SkullScope query baselines must be regenerated from the
final Debug executable and followed by `tools\validate_physics.bat`.
