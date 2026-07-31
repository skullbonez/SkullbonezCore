# Build Configuration Parity BP0 Census

Date: 2026-07-30  
Starting branch: `nightrunner-30th-JUL-26`  
Starting tip: `709e534e`  
Scope: the five repository-root first-party Visual Studio projects and the four
shared translation units named by the plan

## Result

The build census found 313 distinct compiled source paths and 1,640
source/project/configuration rows across:

- `SKULLBONEZ_CORE.vcxproj`
- `SKULLBONEZ_TESTS.vcxproj`
- `SKULLBONEZ_MATHS.vcxproj`
- `SKULLBONEZ_PHYSICS.vcxproj`
- `SKULLBONEZ_UI.vcxproj`

Sixty-one source paths are compiled by more than one first-party project. Every
shared path is owned by Core and Tests; no Maths, Physics, or UI source is
duplicated into another first-party project. The exact inventory is reproducible
with:

```powershell
python tools/check_build_config_consistency.py --repo . --format json
```

The canonical post-repair `compile_rows` JSON has SHA-256
`e6dc09ae5fc1bff36e37a19dc9a1427624ba81d758d7807ae72801fea9239808`.
Each row contains the source, project, `Configuration|Platform`, and effective:

- `PreprocessorDefinitions`
- `ExceptionHandling`
- `LanguageStandard`
- `FloatingPointModel`
- `RuntimeLibrary`
- `ForcedIncludeFiles`

The project/configuration values are therefore recorded as data rather than a
prose sample. `tools/build_config_rulings.json` keys all 122 intentional
shared-file differences by exact source and setting fingerprint. The only
current differences are `PreprocessorDefinitions` and `ExceptionHandling`;
language standard, floating-point model, runtime library, and forced includes
match for equal configurations.

## Shared-Source Ownership Table

Fifty-eight shared sources compile in all four Tests configurations and all five
Core configurations. Three bounded exclusions explain the remaining shapes:

| Source | Core configurations | Tests configurations | Reason |
|---|---|---|---|
| `SkullbonezSource/Core/Allocation/DevelopmentToolAllocation.cpp` | Debug, Profile, Automation | Debug, Profile | Development-only allocator is excluded from Release and Profile-WPO |
| `SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorLayoutPolicy.cpp` | Debug, Profile, Automation | Debug, Profile, Profile-WPO, Release | Core excludes the editor implementation from shipping/WPO; Tests retain the value-policy source |
| `SkullbonezSource/Runtime/Replay/ReplayVisualPacketFingerprint.cpp` | Debug, Automation | Debug, Profile, Profile-WPO, Release | Core uses the fingerprint implementation only in diagnostic configurations; Tests exercise it in every target mode |
| All other 58 shared sources | Debug, Profile, Profile-WPO, Automation, Release | Debug, Profile, Profile-WPO, Release | No per-file exclusion |

The 61 exact shared paths are:

```text
SkullbonezSource/Assets/AssetSystem.cpp
SkullbonezSource/Core/Allocation/DevelopmentToolAllocation.cpp
SkullbonezSource/Core/Allocation/RuntimeAllocationTracker.cpp
SkullbonezSource/Core/Allocation/RuntimeReserveAllocator.cpp
SkullbonezSource/Core/Config.cpp
SkullbonezSource/Core/FatalError.cpp
SkullbonezSource/Core/LockOrderValidator.cpp
SkullbonezSource/Core/Log.cpp
SkullbonezSource/Core/PlatformProfiler.cpp
SkullbonezSource/Core/Profiler.cpp
SkullbonezSource/Core/SbResult.cpp
SkullbonezSource/Core/WorkerPool.cpp
SkullbonezSource/Gameplay/TornadoField.cpp
SkullbonezSource/Gameplay/TornadoGameplay.cpp
SkullbonezSource/Rendering/DX12/Dx12CachedPsoStore.cpp
SkullbonezSource/Rendering/RenderGpuTimingOwner.cpp
SkullbonezSource/Rendering/RenderInstanceStore.cpp
SkullbonezSource/Runtime/App/ApplicationExitState.cpp
SkullbonezSource/Runtime/Camera/Camera.cpp
SkullbonezSource/Runtime/Camera/CameraCollection.cpp
SkullbonezSource/Runtime/Capture/CaptureController.cpp
SkullbonezSource/Runtime/Capture/CaptureSystem.cpp
SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorLayoutPolicy.cpp
SkullbonezSource/Runtime/Diagnostics/DiagnosticsPhysicsUI.cpp
SkullbonezSource/Runtime/Direction/DemoDirector.cpp
SkullbonezSource/Runtime/Editor/EditorCommandHistory.cpp
SkullbonezSource/Runtime/Input/InputController.Bindings.cpp
SkullbonezSource/Runtime/Input/InputRouter.cpp
SkullbonezSource/Runtime/Interaction/OperatorCommandTransaction.cpp
SkullbonezSource/Runtime/Interaction/RuntimeInteractionController.cpp
SkullbonezSource/Runtime/Planning/ReplayGuideArcs.cpp
SkullbonezSource/Runtime/Planning/ReplayInterceptReadout.cpp
SkullbonezSource/Runtime/Planning/ReplayPlanningOverlayLayout.cpp
SkullbonezSource/Runtime/Planning/ReplayPorkchopPanel.cpp
SkullbonezSource/Runtime/Planning/ReplayTripPlanner.cpp
SkullbonezSource/Runtime/Prediction/ReplayPredictionReserve.cpp
SkullbonezSource/Runtime/Render/RenderDefaultsStore.cpp
SkullbonezSource/Runtime/Render/RenderDefaultsStore.Persistence.cpp
SkullbonezSource/Runtime/Replay/ReplayArtifactHashLog.cpp
SkullbonezSource/Runtime/Replay/ReplayOverlayLayout.cpp
SkullbonezSource/Runtime/Replay/ReplayRecorder.cpp
SkullbonezSource/Runtime/Replay/ReplayTimeline.cpp
SkullbonezSource/Runtime/Replay/ReplayV2Artifact.cpp
SkullbonezSource/Runtime/Replay/ReplayVisualPacketFingerprint.cpp
SkullbonezSource/Runtime/Scene/SceneEntityStore.cpp
SkullbonezSource/Runtime/Scene/SceneNavigationModel.cpp
SkullbonezSource/Runtime/Scene/SceneRequestQueue.cpp
SkullbonezSource/Runtime/Scene/SceneSaveOperations.cpp
SkullbonezSource/Runtime/Scene/SceneSessionState.cpp
SkullbonezSource/Runtime/Simulation/SimulationSystem.cpp
SkullbonezSource/Runtime/Startup/StartupCommandLine.cpp
SkullbonezSource/Runtime/Startup/StartupLaunchResolution.cpp
SkullbonezSource/Runtime/Tools/RuntimeFileWriter.cpp
SkullbonezSource/Scene/AuthoredScene.cpp
SkullbonezSource/Scene/AuthoredSceneParser.cpp
SkullbonezSource/Scene/AuthoredSceneParserAssets.cpp
SkullbonezSource/Scene/AuthoredSceneParserBodies.cpp
SkullbonezSource/Scene/AuthoredSceneParserPresentation.cpp
SkullbonezSource/Scene/AuthoredSceneParserRuntime.cpp
SkullbonezSource/Scene/SceneSnapshotWriter.cpp
SkullbonezSource/World/Terrain.cpp
```

## Starting Divergences

The starting tree had two defects:

1. All four Tests configurations omitted `JSON_NOEXCEPTION` while retaining
   `<ExceptionHandling>Sync</ExceptionHandling>`. Core defined
   `_HAS_EXCEPTIONS=0;JSON_NOEXCEPTION` with exception handling disabled.
   nlohmann therefore selected `throw` in Tests and `std::abort()` in Core.
2. Seven Core per-file `ForcedIncludeFiles` overrides named only
   `DevelopmentToolsCapability.h`. Because they omitted
   `%(ForcedIncludeFiles)`, each dropped the project-owned
   `FloatingPointContract.h` in Debug, Profile, and Automation: 21 effective
   dropped-inheritance rows.

The seven files were the six ImGui translation units and Tracy client:

```text
ThirdPtySource/imgui/imgui.cpp
ThirdPtySource/imgui/imgui_draw.cpp
ThirdPtySource/imgui/imgui_tables.cpp
ThirdPtySource/imgui/imgui_widgets.cpp
ThirdPtySource/imgui/backends/imgui_impl_win32.cpp
ThirdPtySource/imgui/backends/imgui_impl_dx12.cpp
ThirdPtySource/tracy/public/TracyClient.cpp
```

BP1 adds `JSON_NOEXCEPTION` to all Tests configurations. BP3 appends
`%(ForcedIncludeFiles)` to all seven overrides. The post-repair inventory
reports zero dropped inheritance and zero unruled/currentness diagnostics.

## JSON Accessor Classification

| Translation unit | Authored format | Accessor classes | Classification | Evidence |
|---|---|---|---|---|
| `SceneSnapshotWriter.cpp` | scene snapshot output | `operator[]`, array iteration, guarded `get<string>()` | (a) values constructed by the writer | No external JSON parse occurs; the one extraction iterates `assetLibraries` built in the same function and checks `is_string()` |
| `ReplayV2Artifact.cpp` | replay manifest output | `operator[]`, `dump()` | (a) values constructed by the writer | The JSON object is a manifest assembled from typed replay values; artifact loading is binary cursor code, not JSON extraction |
| `DemoDirector.cpp` | `skullbonez.shot.json` | `find`, iterator dereference, `get<string/float/bool/int>`, indexed vectors/phases | (b) external authored data behind validation | `FindMember`, `RequireObject`, `RequireArray`, `ReadStringValue`, `ReadFloatValue`, and `ReadBoolValue` prove presence/type; vector size is exactly three and phase count is bounded before indexing |
| `StartupLaunchResolution.cpp` | `skullbonez.suite.json` | `find`, iterator dereference, `get<string>()`, range iteration | (b) external authored data behind validation | Parse uses `allow_exceptions=false`; root/object, format/string, scenes/array, and every scene/string are checked before extraction |

### Exact accessor-site census

The source review used this bounded query:

```powershell
rg -n '\.get<|\[[^]]+\]|\.at\(|\.begin\(|\.end\(|Json::parse' `
  SkullbonezSource/Scene/SceneSnapshotWriter.cpp `
  SkullbonezSource/Runtime/Replay/ReplayV2Artifact.cpp `
  SkullbonezSource/Runtime/Direction/DemoDirector.cpp `
  SkullbonezSource/Runtime/Startup/StartupLaunchResolution.cpp
```

Every JSON hit is classified below. Array subscripts on typed engine arrays,
vectors, strings, and replay binary storage are outside the JSON census.

| File and exact lines | Access | Guard/provenance | Class |
|---|---|---|---|
| `SceneSnapshotWriter.cpp:159,163,168-171,176,204,271,277-290,329-390,466-483,494,499,520,528,538,565,568` | JSON `operator[]`, `push_back`, `empty` | Writes or re-reads objects/arrays constructed by this writer | (a) |
| `SceneSnapshotWriter.cpp:474` | `partState["type"]` comparison | `BuildLiveStateJson` constructed `partState` and assigned `type` for every supported collider | (a) |
| `SceneSnapshotWriter.cpp:485-489` | `begin/end`, `get<string>()` | Iterates the writer-created `assetLibraries` array; `is_string()` is checked before extraction | (a) |
| `ReplayV2Artifact.cpp:2379-2409,2413-2414` | manifest `operator[]`, `dump`, byte copy | Manifest is created locally from typed replay values; no external JSON is read | (a) |
| `DemoDirector.cpp:104-107` | `find/end`, iterator dereference | `FindMember` returns null for absence; callers test the pointer | (b) |
| `DemoDirector.cpp:146` | `get<string>()` | `ReadStringValue` first requires `is_string()` | (b) |
| `DemoDirector.cpp:160` | `get<float>()` | `ReadFloatValue` first requires `is_number()` | (b) |
| `DemoDirector.cpp:169,175` | `get<bool/int>()` | `ReadBoolValue` first requires `is_boolean()` or `is_number_integer()` | (b) |
| `DemoDirector.cpp:227-229` | vector JSON `[0..2]` | `ReadVec3Value` first requires an array of exactly three values, then each element reaches `ReadFloatValue` | (b) |
| `DemoDirector.cpp:397` | phases JSON `[i]` | `ReadRoot` first requires an array, bounds its size by `MAX_PHASES`, and loops while `i < size()` | (b) |
| `DemoDirector.cpp:412-424,528-535` | JSON `operator[]`, `push_back` | Shot-list save path constructs output values | (a) |
| `DemoDirector.cpp:493` | `Json::parse` | Uses `allow_exceptions=false`; `is_discarded()` is tested before `ReadRoot` | (b) |
| `StartupLaunchResolution.cpp:641` | `Json::parse` | Uses `allow_exceptions=false`; discarded and non-object roots return command-line Lane R failure | (b) |
| `StartupLaunchResolution.cpp:653-655` | `find/end`, iterator dereference, `get<string>()` | Presence and `is_string()` are checked before the format extraction | (b) |
| `StartupLaunchResolution.cpp:660-675` | `find/end`, array iteration, `get<string>()` | Presence and `is_array()` are checked before iteration; every element requires `is_string()` before extraction | (b) |

Category (c) count is zero. No authored format needs a BP2 parser repair or a
new malformed-input regression. Existing malformed shot-list and suite
validation remains the owner; BP1 changes only nlohmann's unreachable failure
macro selection after those guards.

## Rulings

The 61 shared sources intentionally retain two project-role differences:

- `ExceptionHandling`: Core disables unwinding; Tests keeps `/EHsc` because
  doctest `REQUIRE` needs it. Both define `_HAS_EXCEPTIONS=0` and
  `JSON_NOEXCEPTION`, so shared engine code and nlohmann use production
  no-exception semantics.
- `PreprocessorDefinitions`: Core and Tests retain executable-role macros such
  as `_WINDOWS`/`_CONSOLE`, capture/profiling flags, doctest flags, and
  `SKULLBONEZ_RENDER_FREE_TESTS`.

Every source/setting ruling is exact current evidence in
`tools/build_config_rulings.json`. Changing any project/configuration variant
changes its fingerprint; deleting the divergence makes the ruling stale.
