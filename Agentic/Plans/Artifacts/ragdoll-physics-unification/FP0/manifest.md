# Ragdoll Physics Unification FP0 Executable

- Phase: `FP0 - Physics correctness prerequisites`
- Source commit parent: `4df765245438cc44724e04f9663702f6e843eb69`
- Configuration: `Debug|x64`
- Built at: `2026-08-22 21:19:59 +10:00`
- Executable: `SKULLBONEZ_CORE-Debug.exe`
- Executable size: `13,931,520 bytes`
- Executable SHA-256: `cdefc1b53c3de37c0d75fdd9a423b61aac8df368b45919f8a312cf6dc73cc053`
- Physics baseline: `TestOutput/baselines/physics_regression_varied.csv`
- Physics baseline SHA-256: `debf57f744774d4e7c1eb5cc61f05ba6e41dc6dc997ad20db6c91b02b0958c32`
- Baseline transition: none; the accepted FP0 output matched the existing 44,401-line golden byte-for-byte.
- Validation: `tools/validate_physics.bat` passed; `tools/validate_fast.bat --preflight-only` passed; `tools/validate_replay_allocation_policy.bat` passed its strict two-generation interaction; post-change SpatialGrid passed 31/31 cases and 9,235 assertions; Debug/Profile Bounds passed 6/6 cases and 34 assertions each.
- Mapped inherited evidence: replay visual controls passed 18/18 before the owner-controlled reveal-0 `header.futureNodeCount` mismatch. The allocation guard reported zero gameplay or reserve-policy violations and the enclosing two-generation prediction interaction passed after replay capture normalized equal warm-start keys.

This executable is the exact final Debug build retained for regression bisection
between accepted ragdoll-physics-unification phases. The phase commit containing
this manifest binds the otherwise uncommitted source diff to the binary digest.
