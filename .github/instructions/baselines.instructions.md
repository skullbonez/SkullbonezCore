---
applyTo: "TestOutput/baselines/**"
---

# Baseline Guidance

Baseline changes are behavior changes unless they are outside this folder and
documentation-only.

Visual baseline updates require DX12 renderer validation and intentional diff
review. Physics CSV and SkullScope query baselines must be regenerated from the
final Debug executable and followed by the mapped Physics gate. An active
Physics plan may update any governed golden through the approved update lane:
the exact candidate SHA-256 and a new append-only old/new runtime bundle are
mandatory, while a separate owner prompt is not. Never refresh a golden merely
to make a failing gate pass. See `../../Agentic/Plans/Artifacts/README.md`.
