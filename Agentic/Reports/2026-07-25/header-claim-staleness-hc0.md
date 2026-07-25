# Header Claim Staleness HC0 Evidence

Date: 2026-07-25

Closure report:
`Agentic/Reports/2026-07-25/header-claim-staleness-remediation-closure.md`

Status: **HC0 complete**

HC0 corrected the 17 registered false-claim sites and one additional phantom
`RunInput` claim discovered by the final word-boundary proof. All edits are
comments; runtime declarations and behavior are unchanged.

| Site | Corrected claim | Live-source proof |
|---|---|---|
| `Runtime/Input/InputController.Bindings.h:4` | `InputFrameExecution` consumes the keyboard binding table. | `Runtime/App/InputFrameExecution.cpp:306` calls `TakeInputKeyboardBindings`. |
| `Runtime/Input/InputController.Bindings.h:9` | `InputFrameExecution` dispatches each action to its named owner. | `Runtime/App/InputFrameExecution.cpp:306-381` builds phase lists; its action switch starts at line 519. |
| `Runtime/Interaction/OperatorCommandApplier.h:104` | `ApplySceneFixedStepUICommand` owns fixed-step reset. | `Runtime/Interaction/OperatorCommandApplier.cpp:241-252` toggles fixed-step state and resets the simulation accumulator. |
| `Runtime/Interaction/OperatorCommandApplier.h:136` | `InputFrame` records accepted friction requests. | `Runtime/App/InputFrame.cpp:455-460,1163-1167` records one transition per accepted request. |
| `Runtime/Interaction/OperatorCommandApplier.h:142` | `InputFrame` records accepted presentation commands. | `Runtime/App/InputFrame.cpp:367-423,1100-1127` maps result flags to UI-sourced transitions. |
| `Runtime/Interaction/OperatorCommandApplier.h:158` | `InputFrame` records accepted cinematic tuning commands. | `Runtime/App/InputFrame.cpp:464-475,1314-1318` maps the result flags to transitions. |
| `Runtime/Interaction/OperatorCommandApplier.h:196` | `InputRouter` applies camera-mode normalization/pointer transitions and `InputController` records the action. | `Runtime/App/InputRouter.Interactions.cpp:434-615` owns that complete transition. |
| `Runtime/Editor/EditorTools.h:172` | `InputFrame` records accepted editor commands; `InputRouter` owns camera/cursor transitions. | `Runtime/App/InputFrame.cpp:782-976` applies the mode and records UI actions through `InputController`. |
| `Runtime/Editor/EditorTools.h:348` | `InputController` owns keybinding data; editor tools own cold save/screenshot effects. | `Runtime/App/InputRouter.Interactions.cpp:778` dispatches the action to `HandleEditorSaveHotkey`; `Runtime/Editor/EditorTools.cpp:432` owns the effects. |
| `Runtime/Scene/SceneRuntimeCoordinator.h:97` | `InputFrame` records accepted scene commands while `SceneController` preserves request order. | `Runtime/App/InputFrame.cpp:478-510,1319-1326` records results; `Runtime/Scene/SceneRequestExecution.cpp:67-159` consumes the fixed batch in order. |
| `Runtime/Input/Input.cpp:63` | `InputFrameExecution` and window/focus paths publish native cursor policy. | `Runtime/App/InputFrameExecution.cpp:336` and `Runtime/App/Window.cpp:354-376` call the cursor API. |
| `Runtime/Editor/EditorTools.cpp:8` | `EditorObjectPlacement` commits the clamped scale. | `Runtime/Editor/EditorObjectPlacement.cpp:179-197` performs preflight and consumes `placementScale`. |
| `Runtime/Interaction/OperatorCommandApplier.cpp:509` | `InputFrame` preserves the accepted cinematic selection transition. | `Runtime/App/InputFrame.cpp:1299-1311` applies the selection and records `SelectCinematicScene`. |
| `Runtime/Interaction/OperatorCommandApplier.cpp:599` | `InputFrame` routes accepted tornado UI actions through `InputController`; the helper owns gameplay mutation. | `Runtime/App/InputFrame.cpp:1073-1083` applies then records; `OperatorCommandApplier.cpp:597` mutates `TornadoGameplay`. |
| `UI/UICommands.h:361` | `InputFrame` translates the replay-memory UI request. | `Runtime/App/InputFrame.cpp:1113-1124` builds `ReplayMemoryPolicyRequest` and calls `ReplayRuntime`. |
| `Runtime/Render/RuntimeRenderResources.h:7` | `RuntimeRenderer` owns ordered backend teardown after a successful GPU drain. | `Runtime/Render/RuntimeRenderer.cpp:2078-2171` drains, then releases consumer passes before producer resources. |
| `Runtime/Scene/SceneController.h:325` | `SceneRequestExecution.cpp` consumes the fixed pending batch. | `Runtime/Scene/SceneRequestExecution.cpp:50-159` calls `TakePendingRequests` and executes the ordered batch. |
| `Runtime/Editor/EditorOverlayTools.h:8` | Calling owners apply side effects; overlay helpers only refresh preview state and append geometry. | `Runtime/Editor/EditorOverlayTools.cpp` implements the two borrowed-context helpers without retained owner state. |

## Proofs

```text
rg -n '\bRunInput\b' SkullbonezSource
PASS: zero phantom RunInput owner rows

rg -n 'lifecycle extraction C1|Run still owns' SkullbonezSource
PASS: zero superseded owner rows
```

The original unbounded `rg 'RunInput'` proof also matched the real
`RunInputPhase` symbol. HC0 tightened the plan proof to the phantom-owner word
boundary so real function names are not false positives.

No repository validation was required because HC0 changed comments and
documentation only.
