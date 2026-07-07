# Progress: Build Layering And Repo Hygiene (plan 04)

Source plan: `fable_plans/04-build-layering-and-repo-hygiene-plan.md`
Status: phase 1 complete on 2026-07-07; phase 2 L1-L4 Maths library extraction complete on 2026-07-08
Last updated: 2026-07-08

## How to work this file

- Do items in order within a phase; phases 1 and 2 are independent starts.
- One checkbox = one verifiable action; tick only with the named evidence
  pasted under the box. `[B]` + reason if blocked twice.
- Comment quality gate applies to touched source files.

## Verified facts (do not re-derive)

- `.gitignore` ignores `Debug/ Release/ Profile/ *.obj *.pdb *.exe`
  etc. and now includes `Agentic/Temp/`. `git ls-files Agentic/Temp`
  returned no tracked tip paths on 2026-07-07. Historical junk still exists
  in commit `065bb64b`: `Agentic/Temp/skullbonez_profile_disasm.txt`
  (81 MB), `Agentic/Temp/ProfilePrediction/WinPixEventRuntime.dll`, and a
  `ProfilePrediction*/` build-output tree. Shrinking the 542 MiB pack still
  requires explicit user approval for a history rewrite and coordinated
  re-clone.
- Solution/toolset: single vcxproj, toolset **v145**, Level4, stdcpp17,
  static CRT (see facts in `01-unit-test-pyramid-progress.md`).
- Maths layering: `SkullbonezSource/Maths/` = GeometricMath, GeometricStructures,
  MathsCommon, Matrix4, Quaternion, RotationMatrix, Vector3. Current outward
  include proof: the five direct Maths headers include `MathsCommon.h`, and
  `GeometricStructures.h` reaches it through `Vector3.h`; no Maths include
  directive pulls `../Core/Common.h`, `<windows.h>`, `Config.h`, `Log.h`, or
  `ZeroMemory`.
- `Core/Common.h` (118 lines after L2) content map - it is included by
  ~everything and still transitively drags `<windows.h>` (line 53, with
  WIN32_LEAN_AND_MEAN), `Config.h` + `Log.h` (lines 85-86), and `<stdexcept>`
  (line 58). It includes `../Maths/MathsCommon.h` (line 61) during the aliasing
  period:
  - Lines 51-66: windows.h + CRT/platform includes + crtdbg - PLATFORM, not
    common.
  - 68-73: TOTAL_CAMERA_COUNT, TOTAL_TEXTURE_COUNT, DEFAULT/MAX_GAME_MODELS -
    scene/render capacities.
  - 75-78: WINDOW_NAME, TITLE_TEXT, DATA_ROOT - runtime/window.
  - 80-82: PHYSICS_FIXED_DT, PHYSICS_MAX_STEPS_PER_FRAME - PHYSICS.
  - `Maths/MathsCommon.h` (54 lines) now owns SKULLBONEZ_INTRINSICS,
    _PI/_2PI/_HALF_PI, fraction constants, and the math/collision tolerances.
  - 85-86: `#include "Config.h"` + `#include "Log.h"` - the global-service
    leak (plan 02 owns deleting Cfg; this plan owns un-nesting the includes).
  - 88-91: ActiveGameModelCapacity(config) - scene capacity policy.
  - 93-97: Log() accessor - sanctioned global (plan 02).
  - 100-118: HashStr + TEXTURE_*/CAMERA_* hash constants - assets/render.
- `RunInput.cpp` (3,580 lines) function inventory groups cleanly:
  - Input routing/commands: BuildRuntimeInputSnapshot(:564),
    RouteRuntimePointerInput(:613), ExecuteRuntimeInteractionCommand(:1009),
    TakeInput(:1945-3542, ~1,600 lines!), DrainRuntimeCommands(:3543).
  - Interaction transitions: EnterInteractionForCameraMode(:788) …
    ApplyRuntimeInteractionTransitionCleanup(:947),
    PublishRuntimeInteractionEvent(:992).
  - Attached camera (self-contained cluster): :1201-1707 (~500 lines,
    ResetAttachedCamera … TickAttachedCamera).
  - Camera modes/cursor: :1081-1201 + :1708-1944 (ApplyCameraMode,
    CycleCameraMode, cursor ownership, fly mode) + MoveCamera(:3658).
  - Replay/editor gesture helpers: :719-786.
  - Odd one out: StepPhysicsPipelineStage(:535) — physics, not input.
- `tools\validate_fast.bat` steps: format -> project filters -> staged file
  sizes -> runtime boundaries -> build Profile -> unit tests -> ready builds.
  Python helpers live in `tools/` (`check_runtime_boundaries.py` pattern:
  rules + self-tests + budgets).

## Phase 1 — repo hygiene (one sitting; == FILL-001/FILL-004 in the overnight protocol; skip any box already done by an overnight run)

- [x] H1. Append to `.gitignore`:
  ```
  # Agent scratch space — never commit
  Agentic/Temp/
  ```

  Evidence: `.gitignore:52` contains `Agentic/Temp/` under the agent scratch
  artifact ignore block.

- [x] H2. Untrack (tip only — NO history rewrite):
  `git rm -r --cached Agentic/Temp` then commit with a note listing the six
  paths and their sizes. Evidence: `git ls-files Agentic/Temp` returns empty;
  `git status` shows the dir as untracked/ignored.

  Evidence: `git ls-files Agentic/Temp` returned empty on 2026-07-07. No tip
  untracking command was needed because the branch already had no tracked
  `Agentic/Temp` paths when this phase was resumed.

- [x] H3. Add `tools/check_staged_file_sizes.py`: fail if any staged/added
  tracked file exceeds 5 MB outside allowlist
  (`TestOutput/baselines/`, `SkullbonezData/`). Imitate the rule+self-test
  structure of `tools/check_runtime_boundaries.py`. Wire it as a step in
  `validate_fast.bat` (after project filters). Evidence: `validate_fast`
  passes; self-test proves a synthetic 6 MB file fails.

  Evidence: `tools/check_staged_file_sizes.py` added with synthetic clean/fail
  cases; `tools/validate_fast.bat` now runs it after project filters. Final
  evidence is recorded in
  `Agentic/Reports/2026-07-07/fable-04-phase-1-repo-hygiene.md`.

- [x] H4. Record (do NOT act): pack size 542 MiB; shrinking requires
  git-filter-repo of `Agentic/Temp` blobs + coordinated re-clone — decision
  owner: user. Add this as a line in `Agentic/SessionState.md` open items.
  Commit phase (gate: `validate_fast`).

  Evidence: `Agentic/SessionState.md` current work items now records the pack
  size cleanup decision as user-owned, with no history rewrite performed.

## Phase 2 — SkullbonezMaths.lib extraction

- [x] L1. Break the one upward dep: open `Maths/GeometricMath.h`, find what
  it uses from `World/Terrain.h` (`rg -n "Terrain" SkullbonezSource/Maths/GeometricMath.*`).
  Expected shape: a terrain-collision helper that belongs on the World side.
  Move that function (decl+def) to the World/Terrain side (or a new
  `World/TerrainMath.h`) and update callers
  (`rg -n "<moved function>" SkullbonezSource`). Evidence: no `World/` include
  remains under `Maths/`; Profile build 0/0. Gate: `validate_fast`. Commit.

  Evidence (2026-07-08): `GeometricMath.h` only needed `Geometry::Triangle`,
  `Geometry::Plane`, and `Geometry::Ray`, all provided by
  `Maths/GeometricStructures.h`; no terrain type or terrain member access was
  present in the declaration. Replaced the `../World/Terrain.h` include with
  `GeometricStructures.h` and added a header glossary note naming the layering
  boundary. `rg -n "\.\./World|World/|World\\" SkullbonezSource\Maths`
  returned no matches. Touched-file comment audit: 1 source-bearing file
  inspected, 0 deferred.

  Validation (2026-07-08): `tools\validate_fast.bat` passed; log:
  `Agentic\Reports\2026-07-08\logs\fable-04-l1-validate-fast.log`
  (log timestamp span 00:00:55). Key result lines: `PASS: Build
  Debug|x64 succeeded.`, `PASS: Profile and Debug binaries are ready.`, and
  `VALIDATE_FAST: ALL PASSED`.
- [x] L2. Give Maths its own minimal prelude so it stops needing Common.h:
  create `Maths/MathsCommon.h` containing ONLY: the CRT math includes
  (<cmath>, <cfloat>), `_PI/_2PI/_HALF_PI/FOUR_OVER_THREE/ONE_OVER_THREE`,
  `TOLERANCE/ONE_PLUS_TOLERANCE/ZERO_TAKE_TOLERANCE/NO_COLLISION`, and
  `SKULLBONEZ_INTRINSICS`. Common.h keeps the same names by including
  MathsCommon.h (aliasing period — no call-site churn). Switch the five Maths
  headers from `../Core/Common.h` to `MathsCommon.h`. Evidence: preprocessing
  a Maths TU no longer pulls <windows.h>/Config.h/Log.h
  (`cl /P /nologo SkullbonezSource/Maths/Vector3.cpp` grep the .i for
  `windows.h` = 0 hits — or simply confirm no compile error after removing
  the include). Gate: `validate_fast`. Commit.

  Evidence (2026-07-08): Added `SkullbonezSource/Maths/MathsCommon.h`
  containing only `<cmath>`, `<cfloat>`, `SKULLBONEZ_INTRINSICS`, the angle and
  fraction constants, and the math/collision tolerances. `Core/Common.h`
  includes `../Maths/MathsCommon.h` to preserve alias-period names. Switched
  `Vector3.h`, `RotationMatrix.h`, `Matrix4.h`, `GeometricMath.h`, and
  `Quaternion.h` from `../Core/Common.h` to `MathsCommon.h`; `GeometricStructures.h`
  reaches the prelude through `Vector3.h`. Added explicit `<stdexcept>` includes
  in `GeometricMath.cpp` and `Vector3.cpp`, and replaced the remaining Maths
  `ZeroMemory` platform dependency with `Plane plane{};`. This returned no
  matches:
  ```bat
  rg -n '^#include\s+"\.\./Core/Common\.h"|^#include\s+<windows\.h>|^#include\s+"Config\.h"|^#include\s+"Log\.h"|ZeroMemory' SkullbonezSource\Maths
  ```
  Touched-file comment audit:
  10 source-bearing files inspected, 0 deferred.

  Validation (2026-07-08): `tools\validate_fast.bat` passed in
  `00:00:54.3133481`; log:
  `Agentic\Reports\2026-07-08\logs\fable-04-l2-validate-fast.log`. Key result
  lines: `PASS: Project filter validation passed.`, `PASS: Runtime boundary
  validation passed.`, `PASS: Build Profile|x64 succeeded.`, `PASS: Build
  Debug|x64 succeeded.`, `PASS: Profile and Debug binaries are ready.`, and
  `VALIDATE_FAST: ALL PASSED`. Because the slice touches `Core/Common.h`,
  `tools\validate_full.bat` also passed in `00:00:46.7472032`; log:
  `Agentic\Reports\2026-07-08\logs\fable-04-l2-validate-full.log`. Key result
  lines: `DX12 validation errors: 0`, `PASS: DX12 screenshots match committed
  baselines.`, `PASS: physics_regression_solver.csv (20001 lines, byte-exact
  match)`, and `VALIDATE_FULL: DEFAULT GATE PASSED`.
- [x] L3. Create `SKULLBONEZ_MATHS.vcxproj` (StaticLibrary, same
  toolset/W4/stdcpp17/CRT — copy ItemDefinitionGroups from the main vcxproj),
  ItemGroup = the 11 Maths files; add to sln; remove those files from
  SKULLBONEZ_CORE.vcxproj ItemGroups and add a ProjectReference. Update BOTH
  .vcxproj.filters files (project-filter gate). Evidence:
  `msbuild SKULLBONEZ_CORE.sln /p:Configuration=Profile /p:Platform=x64`
  clean; `tools\validate_full.bat` green (build-system change = broad scope).
  Commit.

  Evidence (2026-07-08): Added `SKULLBONEZ_MATHS.vcxproj` as a static library
  and registered it in `SKULLBONEZ_CORE.sln`. The library owns the 12 current
  Maths files (5 `.cpp`, 7 headers; the plan's older 11-file count predated
  `MathsCommon.h`). Removed those Maths source/header items from
  `SKULLBONEZ_CORE.vcxproj` and `SKULLBONEZ_CORE.vcxproj.filters`, then added a
  native `ProjectReference` from the app project to `SKULLBONEZ_MATHS`.
  Updated `tools\validate_project_filters.py` so default production validation
  checks the app plus extracted libraries as one source-ownership set while
  preserving explicit `--project` partial checks for auxiliary projects.
  Focused checks passed:
  ```bat
  python tools\validate_project_filters.py --repo .
  python tools\validate_project_filters.py --repo . --project SKULLBONEZ_MATHS.vcxproj --filters SKULLBONEZ_MATHS.vcxproj.filters --partial-project --json-out TestOutput\validation\project_filters\maths_summary.json
  ```
  Profile build evidence passed in `00:00:18.44`; log:
  `Agentic\Reports\2026-07-08\logs\fable-04-l3-l4-profile-build.log`.
  Key result lines: `SKULLBONEZ_MATHS.vcxproj -> ...\Profile\SKULLBONEZ_MATHS.lib`,
  `SKULLBONEZ_CORE.vcxproj -> ...\Profile\SKULLBONEZ_CORE.exe`, and `PASS:
  Build Profile|x64 succeeded.` Touched-file comment audit inspected
  `tools\validate_project_filters.py`; 1 source-bearing tool file checked,
  0 deferred.

- [x] L4. Point SKULLBONEZ_TESTS (plan 01) at the lib via ProjectReference,
  removing its compiled-in Maths sources. Gate: `validate_tests`. Commit.

  Evidence (2026-07-08): Removed the five Maths `.cpp` compile-in entries and
  matching filter entries from `SKULLBONEZ_TESTS`, then added a native
  `ProjectReference` to `SKULLBONEZ_MATHS`. `tools\validate_tests.bat` passed
  in `00:00:01.8538541`; log:
  `Agentic\Reports\2026-07-08\logs\fable-04-l4-validate-tests.log`. Key result
  lines: `PASS: Project filter validation passed.`,
  `SKULLBONEZ_MATHS.vcxproj -> ...\Profile\SKULLBONEZ_MATHS.lib`,
  `SKULLBONEZ_TESTS.vcxproj -> ...\Profile\SKULLBONEZ_TESTS.exe`,
  `[doctest] test cases: 44 | 44 passed | 0 failed | 0 skipped`, and
  `VALIDATE_TESTS: ALL PASSED`.

  Commit-gate validation (2026-07-08): `tools\validate_fast.bat` passed in
  `00:00:49.2272698`; log:
  `Agentic\Reports\2026-07-08\logs\fable-04-l3-l4-validate-fast.log`. Key
  result lines: `PASS: Project filter validation passed.`, `PASS: Runtime
  boundary validation passed.`, `PASS: Build Profile|x64 succeeded.`, `PASS:
  Build Debug|x64 succeeded.`, `PASS: Profile and Debug binaries are ready.`,
  and `VALIDATE_FAST: ALL PASSED`. `tools\validate_full.bat` passed in
  `00:00:45.8862696`; log:
  `Agentic\Reports\2026-07-08\logs\fable-04-l3-l4-validate-full.log`. Key
  result lines: `DX12 validation errors: 0`, `PASS: DX12 screenshots match
  committed baselines.`, `PASS: physics_regression_solver.csv (20001 lines,
  byte-exact match)`, and `VALIDATE_FULL: DEFAULT GATE PASSED`.

## Phase 3 — Common.h split (after L2 proves the aliasing pattern)

- [ ] C1. `Physics/PhysicsTimestep.h`: move PHYSICS_FIXED_DT +
  PHYSICS_MAX_STEPS_PER_FRAME (Common.h includes it during alias period).
  Census first: `rg -ln "PHYSICS_FIXED_DT|PHYSICS_MAX_STEPS_PER_FRAME" SkullbonezSource | wc -l`
  — record count here; switch the top 10 physics/replay users to the new
  header directly. Gate: `validate_physics` (byte-exact proves nothing moved).
- [ ] C2. Scene/render capacities (MAX_GAME_MODELS etc.) →
  `GameObjects/SceneCapacity.h` (coordinate: authoritative-plan-02 PHYS-012
  owns capacity POLICY; this is only the constant's home). Gate:
  `validate_fast`.
- [ ] C3. TEXTURE_*/CAMERA_* hash constants + HashStr → `Assets/AssetKeys.h`
  (HashStr is used by profiler markers too — census first:
  `rg -ln "HashStr" SkullbonezSource`). Gate: `validate_fast`.
- [ ] C4. WINDOW_NAME/TITLE_TEXT/DATA_ROOT → `Runtime/WindowConstants.h`.
  Gate: `validate_fast`.
- [ ] C5. End state: Common.h = platform includes + crtdbg + includes of the
  new domain headers (alias period), with a header comment scheduling each
  alias's deletion. The `#include "Config.h"/"Log.h"` lines move OUT of
  Common.h only when plan 02 phase 4 deletes Cfg() — add a `Why:` comment
  crosslinking. Gate: `validate_full` (Common.h row in the validation map).
  Commit per sub-item.

## Phase 4 — mega-file decomposition (one file per slice; sequence AFTER authoritative-plan-01 clusters land for Run* files)

- [ ] M1. `RunInput.cpp` split by the verified inventory (new files, same
  `Run::` methods — mechanical moves, no behavior): 
  `RunAttachedCamera.cpp` (:1201-1707 cluster), `RunCameraModes.cpp`
  (:1081-1200, :1708-1944, MoveCamera), `RunInteractionTransitions.cpp`
  (:682-1008), leaving RunInput.cpp with input routing + TakeInput +
  DrainRuntimeCommands (still big — TakeInput's ~1,600 lines shrink under
  authoritative RUN-010, not here). Move StepPhysicsPipelineStage(:535) to
  RunFrame.cpp where the other pipeline stages live (verify:
  `rg -n "StepPhysicsPipelineStage" SkullbonezSource`). Update vcxproj +
  filters. Gate: `validate_full`. Commit per extracted file.
- [ ] M2. `TestSceneParser.cpp` (2,956 lines): inventory its sections first
  (`rg -n "^\w.*Parse|^static" SkullbonezSource/Scene/TestSceneParser.cpp`),
  split by schema domain (bodies/assets/groups/water/cameras). Gate:
  `validate_full` (scene load path). [B] until needed — lowest value of the
  set.
- [ ] M3. Convert one migrated `.inl` to a TU as proof of the RUN-027 row
  pattern (candidate: whichever Replay .inl area authoritative-plan-01
  migrates first). Pair with `validate_perf` (hot code loses whole-TU
  inlining). [B] until a RUN cluster completes.

## Closure

- [ ] Z1. Update `fable_plans/04-build-layering-and-repo-hygiene-plan.md`
  status + this file; record final Common.h line count and lib count.
