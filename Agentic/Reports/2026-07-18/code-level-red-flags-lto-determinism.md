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

The initial `tools\validate_perf.bat` pair passed before and after the change
on the same machine. That gate measures ordinary `Profile`, whose physics
project was already non-WPO, so it remains the mapped regression guard against
an accidental broader build-policy or runtime change:

| Lane | Before average | After average | Delta | Result |
|---|---:|---:|---:|---|
| DX12 frame | 0.6614 ms | 0.6618 ms | +0.06% | noise; gate passed |
| Physics bench frame | 0.3642 ms | 0.3693 ms | +1.40% | noise; gate passed |

Both runs also passed the allocation guard, selected-ball structural probe,
absolute budgets, baseline comparisons, and the measurement-only scale matrix.
Run times were 93.925s before and 91.617s after.

Independent C6 review correctly reopened C5 because ordinary `Profile` did not
exercise the changed `Profile-WPO` item metadata. The remediation performed a
controlled optimized A/B on the same final source. The before build temporarily
set only the three solver-critical items back to WPO, then clean rebuilt the
core project warning-free. The after build restored the committed non-WPO item
metadata and clean rebuilt warning-free twice. The temporary metadata was not
committed and the final project diff is unchanged from the C5 implementation.

| Optimized lane | All three TUs WPO | Three-TU boundary | Delta |
|---|---:|---:|---:|
| Whole frame average | 0.3678 ms | 0.3662 ms | -0.44% |
| Whole frame p99 | 0.6778 ms | 0.7090 ms | +0.0312 ms |
| `Frame/Physics` average | 0.0552 ms | 0.0583 ms | +0.0031 ms |
| `Frame/Physics` p99 | 0.1091 ms | 0.1183 ms | +0.0092 ms |

Both captures contain 2,340 frames. The small physics movement remains only
7.8% of the ratified 0.75 ms average budget, while whole-frame average improved;
this is accepted as measurement noise rather than a meaningful product cost.

The two clean current-boundary rebuilds then ran the same `Profile-WPO`
1200-scene-frame replay-hash oracle. Each presentation and solver artifact has
2,402 non-comment rows and zero non-comment differences. The normalized
presentation SHA-256 is
`75f6f5bfd665404b641e83aea5d7994998bd352fe1f3b51623e8684faedac6a6` in
both runs; the normalized solver SHA-256 is
`647d45237a34836c2985600a30034594747e35b31b6317a4425e8f3d04d2ede6` in
both runs. Raw artifact hashes differ only because the two comment headers spell
the same scene path with `/` versus `\`; every machine-consumed CSV row is
byte-identical. This directly exercises the optimized configuration changed by
C5 and closes the independent review's evidence gap.

The recorded commands were:

```bat
MSBuild.exe SKULLBONEZ_CORE.vcxproj /t:Rebuild /p:Configuration=Profile-WPO /p:Platform=x64 /m:1
Profile-WPO\SKULLBONEZ_CORE.exe --vsync off --fixed-step --no-contact-audio --scene SkullbonezData/scenes/physics_bench_varied.scene.json
Profile-WPO\SKULLBONEZ_CORE.exe --vsync off --fixed-step --no-contact-audio --replay on --replay-hashes TestOutput\validation\agent_logs\red_flags_c5_profile_wpo_oracle_N.csv --scene SkullbonezData/scenes/physics_bench_varied.scene.json
```

The temporary all-WPO rebuild took 32.907s. The two final-boundary clean
rebuilds took 34.409s and 34.301s. All three completed with zero warnings and
zero errors. The benchmark CSV SHA-256 values were
`9335A1B800465D5366C6F1359EB7BDDC257CD53F166826460928FBBC55BA1715`
before and
`F6F9494B6A0204E2CC9DF0A1490F4A2FBC95ECDC50CD2137AABEBCF0B26C9EE2`
after; these are performance captures, not determinism artifacts, so their
contents are compared through the metrics above.

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
- `TestOutput/validation/agent_logs/red_flags_c5_profile_wpo_perf_before_build.log`
- `TestOutput/validation/agent_logs/red_flags_c5_profile_wpo_perf_before.csv`
- `TestOutput/validation/agent_logs/red_flags_c5_profile_wpo_before_perf.json`
- `TestOutput/validation/agent_logs/red_flags_c5_profile_wpo_oracle_rebuild_1.log`
- `TestOutput/validation/agent_logs/red_flags_c5_profile_wpo_oracle_rebuild_2.log`
- `TestOutput/validation/agent_logs/red_flags_c5_profile_wpo_oracle_1.csv`
- `TestOutput/validation/agent_logs/red_flags_c5_profile_wpo_oracle_1.solver.csv`
- `TestOutput/validation/agent_logs/red_flags_c5_profile_wpo_oracle_2.csv`
- `TestOutput/validation/agent_logs/red_flags_c5_profile_wpo_oracle_2.solver.csv`
- `TestOutput/validation/agent_logs/red_flags_c5_profile_wpo_perf_after.csv`
- `TestOutput/validation/agent_logs/red_flags_c5_profile_wpo_after_perf.json`
