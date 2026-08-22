# Ragdoll Physics Unification FP1 Executable

- Phase: `FP1 - Deterministic motion eligibility and replay state`
- Source commit parent: `5e0f78279ae49352eca74f40de1fe3ce7cfe5631`
- Configuration: `Debug|x64`
- Built at: `2026-08-22 23:16:35 +10:00`
- Executable: `SKULLBONEZ_CORE-Debug.exe`
- Executable size: `13,949,440 bytes`
- Executable SHA-256: `612461c8dbd48eb8823468a8b06d1fb3f576b5610b6e48610b6b5a870ae7888a`
- Physics baseline: `TestOutput/baselines/physics_regression_varied.csv`
- Physics baseline SHA-256: `debf57f744774d4e7c1eb5cc61f05ba6e41dc6dc997ad20db6c91b02b0958c32`
- Baseline transition: none; the accepted FP1 output matched the existing 44,401-line golden byte-for-byte.
- Validation: `tools/validate_physics.bat` passed; `tools/validate_fast.bat --preflight-only` passed after complete current Debug/Profile rebuilds and strict reachability; Profile `SKULLBONEZ_TESTS` passed 708/708 cases and 2,544,123 assertions.
- Focused evidence: Debug/Profile motion eligibility passed 4/4 cases and 53 assertions; real replay restore continuation passed 1/1 case and 14 assertions; exact capacity and runtime-contract controls passed 6,134 and 341 assertions respectively.
- Review: independent strict FP1 source review passed after five source blockers and five missing-evidence findings were repaired; touched source/tool comment audit completed 29/29 with zero deferred.

This executable is the exact final Debug build retained for regression bisection
between accepted ragdoll-physics-unification phases. The phase commit containing
this manifest binds the otherwise uncommitted source diff to the binary digest.
