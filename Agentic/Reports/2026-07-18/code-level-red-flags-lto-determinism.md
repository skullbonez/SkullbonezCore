# Code-Level Red Flags C5 — LTO Determinism Boundary

Date: 2026-07-18
Plan: `code-level-red-flags-remediation`, task C5
Branch: `nightrunner-17th-july`

## Decision And Result

The owner-ratified structural lane is implemented. Release and `Profile-WPO`
continue to use WPO/LTCG for the engine, maths, tests, and non-solver physics
translation units. Three leaf arithmetic owners now compile as ordinary native
objects in those configurations:

- `ObjectContactManifold.cpp` — object/object feature selection and manifold
  construction, including the ff6e780e historical knife edge;
- `TerrainContactManifold.cpp` — terrain sweep/manifold feature selection; and
- `PersistentContactSolver.cpp` — warm start, sequential impulses, friction,
  bias, and position correction.

MSBuild item evaluation reports `WholeProgramOptimization=false` for exactly
those three items and `true` for neighboring `Profile-WPO` physics items. The
clean build log confirms the compiler split: the other 28 physics TUs compile
in the `/GL` invocation, while the three named TUs compile in a separate
invocation without `/GL`. The final link still uses `/LTCG`, so optimization is
narrowed at the solver arithmetic boundary rather than disabled product-wide.
Ordinary `Profile` and Debug physics were already non-WPO and remain unchanged.

The certified-envelope contract is updated in
`Agentic/Reference/physics-overview.md`. Adding or removing a contact/solver
arithmetic owner now explicitly requires this boundary decision to be revisited.

## Clean-Rebuild Evidence

Two consecutive sequential full `Profile-WPO|x64` solution rebuilds completed
with zero warnings and zero errors:

| Run | Command | Time | Core SHA-256 | Physics library SHA-256 |
|---|---|---:|---|---|
| 1 | `MSBuild.exe SKULLBONEZ_CORE.sln /t:Rebuild /p:Configuration=Profile-WPO /p:Platform=x64 /m:1` | 54.535s | `0ECF56AF29B72F15C9FB5BC0E4E5D78AFFB45A7629CFD05978C9A72A8419DD62` | `919C1306DB69B8A03D6FECA57BBD646A05788BA6193A5B1A3D3686E216BFD8B0` |
| 2 | same | 55.167s | `4FEDCAC5306DA916A1EF3209E053551AE5F64E617D496E4B7D04EFB4A99C8B6F` | `6B974C73B9CE360C3A344FC206A8CCA26307AF60FE78FABB7927AB92E2FFE5AD` |

The project does not enable reproducible-build metadata, so PE/archive hashes
are recorded as provenance and are not claimed to match. Determinism closure is
the behavior contract: after each clean optimized rebuild,
`tools\validate_physics.bat` passed the standalone API/handle smoke and matched
the committed 44,401-line regression CSV byte-for-byte. No baseline changed.

The first exploratory parallel rebuild demonstrated the same compiler split but
reported one shared-PDB clean warning. It is excluded from the counted evidence;
the two sequential runs above are the warning-free replacements.

## Performance Evidence

`tools\validate_perf.bat` passed before and after the change on the same machine.
The measured configuration is ordinary `Profile`, whose physics project was
already non-WPO, making it the correct guard against an accidental broader
build-policy or runtime change:

| Lane | Before average | After average | Delta | Result |
|---|---:|---:|---:|---|
| DX12 frame | 0.6614 ms | 0.6618 ms | +0.06% | noise; gate passed |
| Physics bench frame | 0.3642 ms | 0.3693 ms | +1.40% | noise; gate passed |

Both runs also passed the allocation guard, selected-ball structural probe,
absolute budgets, baseline comparisons, and the measurement-only scale matrix.
Run times were 93.925s before and 91.617s after.

## Validation-Discovered Governance Repair

The first pre-change performance attempt stopped because N7 had moved cold
automation/report vectors to `InteractionAutomationReportWriter` and
`RuntimeValidationHarness` without moving their allocation-policy provenance.
The allowlist now follows those exact owners, retains only the parser patterns
still present in `InteractionAutomationController`, and removes stale
`SceneAuthoredSetup` patterns. No exception phase, reason, or runtime scope was
broadened. The checker self-test passes and the repository scan reports 376
files, 43 direct-heap findings, 144 dynamic-STL-member findings, 673 STL-growth
findings, and zero allowlist errors.

The mapped tool gate also passed: `tools\validate_fast.bat` completed in
51.645s with formatting clean, 722/722 project/filter items matched, zero staged
file-size violations, warning-free Profile and Debug builds, and the main test
suite passing 284/284 cases and 21,408/21,408 assertions. The final allocation
checker self-test plus repository scan completed in 8.782s.

## Evidence Files

- `TestOutput/validation/agent_logs/red_flags_c5_profile_wpo_rebuild_1.log`
- `TestOutput/validation/agent_logs/red_flags_c5_profile_wpo_rebuild_2.log`
- `TestOutput/validation/agent_logs/red_flags_c5_profile_wpo_hashes_1.txt`
- `TestOutput/validation/agent_logs/red_flags_c5_profile_wpo_hashes_2.txt`
- `TestOutput/validation/agent_logs/red_flags_c5_validate_physics_1.log`
- `TestOutput/validation/agent_logs/red_flags_c5_validate_physics_2.log`
- `TestOutput/validation/agent_logs/red_flags_c5_validate_perf_before_stdout.log`
- `TestOutput/validation/agent_logs/red_flags_c5_validate_perf_after_stdout.log`
- `TestOutput/validation/agent_logs/red_flags_c5_validate_fast.log`
- `TestOutput/validation/agent_logs/red_flags_c5_allocation_policy.log`
