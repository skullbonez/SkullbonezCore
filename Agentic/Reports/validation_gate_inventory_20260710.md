# Validation Gate Inventory — 2026-07-10

Closure: `Agentic/Reports/2026-07-30/validation-gate-integrity-closure.md`
records completion of V0-V2 and the complete V0-V5 campaign.

Owner: repository validation

Branch: `engine-cleanup-10th-july`

## Result

The mandatory ownership graph is now explicit and executable:

```text
agent_validate
  -> validate_full
       -> validate_fast --preflight-only
       -> validate_all_cpu_tests
            -> validate_tests
            -> validate_runtime_interaction_policy
            -> validate_scene_parser_tests
            -> validate_dx12_arch_tests
       -> Debug build
       -> validate_dx12_renderer
       -> validate_physics
```

`validate_full` therefore runs every first-party CPU test target exactly once
before either runtime lane. `agent_validate` is a one-call alias to that same
entry point. Direct `validate_fast` still owns the cheap doctest path for small
changes; its internal `--preflight-only` composition mode prevents the broad
gate from executing the doctest runner twice.

The inventory covers all 32 batch entry points that validate, select, or watch
repository behavior, plus the 18 Python/PowerShell/check helpers they own.
Costs below are warm/incremental estimates unless a measured value is named;
compile-invalidating changes can make build-backed rows materially slower.

## First-Party Test Target Ownership

| Test target | Project and produced executable | Configurations run | Engine launch | Owning mandatory lane | Other callers | Cost evidence |
|---|---|---|---|---|---|---|
| Main doctest suite | `SKULLBONEZ_TESTS.vcxproj` -> `Profile/SKULLBONEZ_TESTS.exe` | Profile | No | `validate_tests` under `validate_all_cpu_tests` | Direct `validate_fast` | Final run: 78/78 cases, 1,883/1,883 assertions |
| Runtime interaction policy | `Agentic/Tests/RuntimeInteractionPolicyTests/RuntimeInteractionPolicyTests.vcxproj` -> `x64/{Debug,Release}/RuntimeInteractionPolicyTests.exe` | Debug and Release | No | `validate_runtime_interaction_policy` under `validate_all_cpu_tests` | `validate_select` | Two console launches; all named tests passed in both configs |
| Scene/style parser contracts | `Agentic/Tests/SceneParserUnitTests/SceneParserUnitTests.vcxproj` -> `x64/Debug/SceneParserUnitTests.exe` | Debug | No | `validate_scene_parser_tests` under `validate_all_cpu_tests` | Direct script | Three contract cases passed |
| DX12 architecture contracts | `Agentic/Tests/Dx12ArchUnitTests/Dx12ArchUnitTests.vcxproj` -> `x64/Debug/Dx12ArchUnitTests.exe` | Debug | No device or engine | `validate_dx12_arch_tests` under `validate_all_cpu_tests` | `validate_select` | One console launch; death-path diagnostic is expected and the suite passed |

The three standalone projects are intentionally absent from
`SKULLBONEZ_CORE.sln`; the CPU umbrella is now their owning mandatory lane.
Neither standalone parser nor interaction project has a tracked `.filters`
file, so there is no filters file to reconcile. Both now compile the same
bounded `FatalError.cpp` + `Log.cpp` pair already used by
`Dx12ArchUnitTests`, rather than replacing the production Lane-F contract with
a test-only fatal implementation.

## Batch Entry-Point Inventory

### Orchestration and shared preparation

| Script | Owner / behavior | Build or launch | Direct callers | Typical cost |
|---|---|---|---|---|
| `agent_validate.bat` | Stable agent alias for the mandatory broad gate | Delegates once to `validate_full` | `validate_select`, direct | Same as full |
| `validate_all_cpu_tests.bat` | Mandatory CPU test fan-in; fail-fast and preserves the first child exit code | Builds through children; 5 console-test launches | `validate_full`, direct | Final warm run: 27.796s |
| `validate_fast.bat` | Format, metadata, staged-size, Profile build, then doctest; `--preflight-only` defers tests | Profile build; optional doctest launch | `validate_full`, `validate_select`, direct | About 30s documented; warm runs are shorter |
| `validate_full.bat` | Mandatory PR superset | CPU lane, Debug build, DX12 lane, physics lane | `agent_validate`, `validate_select`, direct | 3 engine processes after CPU tests |
| `validate_deep.bat` | Opt-in expensive renderer/deep-physics/perf sweep | Profile + Debug and 14 engine processes with the current suites | `validate_select`, direct | Several minutes / machine dependent |
| `validate_select.bat` | Developer-selected composition | Depends on selected targets | Direct | Sum of selected targets |
| `validate_ready_builds.bat` | Leaves Profile and Debug binaries ready unless caller suppresses it | Profile + Debug builds | Most build-backed gates | Incremental build cost |

### CPU, metadata, and build gates

| Script | Owner / behavior | Project/executable | Direct callers | Typical cost |
|---|---|---|---|---|
| `validate_build.bat` | Solution build primitive | `SKULLBONEZ_CORE.sln`, selected config | Most build-backed gates | Incremental seconds; clean build longer |
| `validate_format.bat` | clang-format verification | No executable | fast, concepts, renderer, UI/stress, select | Seconds |
| `validate_project_filters.bat` | Production project/filter ownership and path checks | `validate_project_filters.py` | fast, deep, select | Seconds |
| `validate_tests.bat` | Main doctest project/filter/build/run owner | `SKULLBONEZ_TESTS.exe` | CPU umbrella; direct fast path | Build + one console launch |
| `validate_runtime_interaction_policy.bat` | Interaction capture/ownership policy | standalone Debug + Release executables | CPU umbrella, select | Build + two console launches |
| `validate_scene_parser_tests.bat` | Scene/style authoring contract | standalone Debug executable | CPU umbrella | Build + one console launch |
| `validate_dx12_arch_tests.bat` | Descriptor/render-graph CPU policy | standalone Debug executable | CPU umbrella, select | Build + one console launch |
| `validate_shaders.bat` | Shader manifest/stage/resource contract | `validate_shaders.py`; no engine | select, direct | Seconds plus ready builds unless suppressed |

### Runtime and targeted gates

| Script | Owner / behavior | Engine/project launch | Direct callers | Typical cost |
|---|---|---|---|---|
| `validate_concepts.bat` | Smoke/core/full concept-scene tiers | Profile engine; one process per scene in the selected tier | select, direct | Tier dependent |
| `validate_demo_stress.bat` | Generated-demo interaction crash sweep | One Profile engine process | select, UI watcher | Seconds plus build |
| `validate_dx12_renderer.bat` | DX12 InfoQueue and screenshot baselines | One Profile engine suite process | full, deep, retired alias, select | About 2 min documented |
| `validate_interaction_clicks.bat` | Inspect and replay click probes | Two Profile engine processes | Direct | Two finite scripts |
| `validate_perf.bat` | Allocation policy and perf budgets | Three Profile engine processes | deep, select | About 1 min documented |
| `validate_physics.bat` | Standalone API smoke plus core deterministic CSV | Two Debug engine processes | full, select | Two finite launches |
| `validate_physics_deep.bat` | Core, bullet, shooting, known-issue, and query sweep | Nine direct Debug launches plus query generation | deep, select | About 45s+ documented |
| `validate_physics_query.bat` | SkullScope trace/query baseline | Debug engine via Python checker | select, direct | Build + one trace run |
| `validate_renderers.bat` | Retired compatibility alias | Delegates to DX12 renderer | Direct | Same as renderer |
| `validate_replay_scrub.bat` | Scrub/restore/prediction determinism probes | Multiple Debug processes through two Python checkers | Direct | Multi-run, machine dependent |
| `validate_replay_v2_artifact.bat` | Replay save/load/query artifact | Debug engine through Python checker | Direct | Multi-step, machine dependent |
| `validate_scene_loads.bat` | Boot every tracked scene with timeout | One Profile engine process per scene | Direct | Scene-count dependent |
| `validate_ui.bat` | DX12 UI screenshots and blur checks | One Profile engine suite process | select | Suite dependent |
| `validate_ui_stress.bat` | Deterministic UI crash sweep | One Profile engine process | select, UI watcher | About 10s documented |
| `run_graphics_stress.bat` | DX12 churn/fuzz/memory telemetry | One timed Profile engine process | Direct | Bounded minutes or overnight |
| `watch_ui_stress.bat` | Finite/repeating UI or demo stress owner | Repeats selected stress script | Direct | Iteration dependent |
| `watch_demo_stress.bat` | Demo convenience alias | Delegates to UI watcher in demo mode | Direct | Iteration dependent |

## Helper Ownership

| Helper | Owning entry point |
|---|---|
| `check_staged_file_sizes.py` | `validate_fast` |
| `validate_project_filters.py` | `validate_project_filters`, plus the test-project check in `validate_tests` |
| `validate_shaders.py` | `validate_shaders` |
| `validate_concepts.py` | `validate_concepts` |
| `validate_scene_loads.py` | `validate_scene_loads` |
| `check_dx12_validation.bat`, `check_dx12_baselines.py` | `validate_dx12_renderer` (InfoQueue helper also used by UI/stress) |
| `check_allocation_policy.py`, `check_perf_budgets.py` | `validate_perf` |
| `check_physics_regression.py` | `validate_physics`, `validate_physics_deep` |
| `check_physics_known_issue_regression.py`, `check_shooting_reaction.py` | `validate_physics_deep` |
| `check_physics_query_regression.py` | `validate_physics_query`, `validate_physics_deep` |
| `check_replay_scrub_regression.py`, `check_replay_prediction_determinism.py` | `validate_replay_scrub` |
| `check_replay_v2_artifact.py` | `validate_replay_v2_artifact` |
| `check_ui_blur.py` | `validate_ui` |
| `run_graphics_stress.ps1` | `run_graphics_stress.bat` |

## V1 Fail-Fast Evidence

A hermetic child-script directory under ignored `TestOutput/validation` supplied
one deliberate nonzero result per named target. The production umbrella was
run unchanged with `SKULLBONEZ_CPU_TEST_HARNESS=1` and
`SKULLBONEZ_CPU_TEST_SCRIPT_DIR` pointing at that directory. Later targets were
reported `NOT RUN`, and the child result was preserved exactly:

| Injected target | Child exit | Umbrella exit | Later targets ran? |
|---|---:|---:|---|
| `validate_tests.bat` | 11 | 11 | No |
| `validate_runtime_interaction_policy.bat` | 22 | 22 | No |
| `validate_scene_parser_tests.bat` | 33 | 33 | No |
| `validate_dx12_arch_tests.bat` | 44 | 44 | N/A; last target |

The success-path harness returned 0 and printed four `PASS` rows. Temporary
harness scripts were removed after the check.

## V2 Broad-Gate Evidence

Byte-identical copies of `validate_full.bat`, `agent_validate.bat`, and
`validate_all_cpu_tests.bat` were run in the same ignored harness beside a
no-build preflight stub and the failing child. SHA-256 equality was checked
before execution. Both broad entry points returned the injected child code 11;
neither output reached `Phase 2: Build Debug`, `Phase 3: DX12`, `Phase 4:
Physics`, or an engine executable. This proves failure propagation and
pre-runtime ordering without launching the engine while parallel source work
was active.

The direct production command was then run:

```bat
cmd.exe /d /c tools\validate_all_cpu_tests.bat
```

It initially exposed two orphan-suite link defects: the interaction and scene
parser projects compiled code that now calls `SbFatal` but did not link its
implementation. Adding the bounded production `FatalError.cpp` + `Log.cpp`
pair fixed both without changing test behavior.

The now-runnable scene parser suite then exposed a genuine Lane-R regression:
`Material authoring rejects malformed options` crashed with access violation
`0xC0000005`. Exact debugger command:

```bat
"C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe" -logo TestOutput\validation\scene_parser_cdb.log -c "sxe av; g; .ecxr; kn; q" Agentic\Tests\SceneParserUnitTests\x64\Debug\SceneParserUnitTests.exe
```

The stack showed `ParseMaterialModeValue` called from `ApplyObjectMaterial`.
`Fail()` recorded a missing `mode` error but, after the exception-removal
conversion, execution continued and dereferenced the null JSON member. The
minimal Lane-R return now preserves the existing error and prevents the null
access.

Final result (`TestOutput/validation/validate_all_cpu_tests_20260710_final.log`):

```text
[doctest] test cases: 78 | 78 passed | 0 failed | 0 skipped
[doctest] assertions: 1883 | 1883 passed | 0 failed
PASS: runtime interaction policy tests passed.  (Debug and Release)
PASS: all scene parser unit tests passed.
PASS: DX12 architecture unit tests passed.
VALIDATE_ALL_CPU_TESTS: ALL PASSED
CPU_UMBRELLA_RESULT exit=0 elapsed_seconds=27.796
```

The coordinator then ran the production `tools\validate_full.bat` composition.
It passed preflight, the four-target CPU umbrella, the Debug handoff, DX12 with
zero InfoQueue errors and all three screenshot comparisons inside threshold,
and the 20,001-line byte-exact physics baseline. The new
`validate_select all-cpu-tests` selector also passed all four CPU owners.

## Remaining Plan-Owned Work

- V3 still owns Windows pull-request CI and honest self-hosted DX12 runtime CI.
- V4 still owns AddressSanitizer and static analysis.
- V5 is complete: root/setup/Agentic/tools documentation, the file-to-gate map,
  the same-commit umbrella-registration rule, and selector discoverability now
  agree on the CPU-first broad gate and its two runtime lanes/three processes.
- `validate_deep` remains an opt-in expensive sweep, not the mandatory merge
  entry point; it does not currently call the CPU umbrella.
