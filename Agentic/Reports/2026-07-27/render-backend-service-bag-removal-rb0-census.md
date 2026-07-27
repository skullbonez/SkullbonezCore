# Render Backend Service Bag Removal RB0 — Consumer Census

Date: 2026-07-27
Branch: `nightrunner-26th-JUL-26`
Plan: `render-backend-service-bag-removal` RB0

## Outcome

Every construction, transport, and consumption site of
`RuntimeRenderBackendView` is classified. No consumer has an unknown
requirement.

Successful startup constructs the eleven-pointer view from one
`RenderBackendDX12` owner. The view is then copied into `Run`, copied again into
`RuntimeRenderer`, projected into a second seven-pointer
`BackendEpochOwners`, and transported through `RuntimeFramePresentationView`.
Most consumers use two or fewer capabilities.

Only `RuntimeRenderer` and its `RenderResourceLifecycle` need the broad render
epoch. Together they use device, frame, graph, resource, texture, geometry,
diagnostics, and optional ray-tracing owners. This is the already-ratified
renderer authority boundary, but its nested `BackendEpochOwners` is still a
service bag and must not survive RB1. Store the concrete renderer-owned borrows
directly and expose narrow operations to other runtime consumers.

## Construction And Transport Sites

| Site | Current action | Dereferenced here | Existing concrete owner | RB1 implication |
|---|---|---|---|---|
| `InitRenderBackend` (`Runtime/App/Init.cpp:132-183`) | Creates `RenderBackendDX12`, assigns all eleven fields, resets the view on startup failure | All eleven getters are called during publication | Local `std::unique_ptr<RenderBackendDX12>` retained by process bootstrap | This is the sole composition point; bind concrete borrows from the backend here |
| WinMain startup (`Init.cpp:459-492`) | Holds the view beside the backend owner and gives `renderFrame` to `Window` resize | `renderFrame` | The same process-owned `RenderBackendDX12` | Window can receive `Dx12FrameOwner*` directly; the view is unnecessary |
| `RunApp` (`Init.cpp:185`) | Passes the whole view by value into `Run` | None | Caller still owns `RenderBackendDX12` | Remove the transport parameter |
| `Run::Run` (`Runtime/App/Run.cpp:276`) | Stores the whole view, passes it to `RuntimeRenderer`, and passes development UI capability to ImGui startup | `developmentUiRenderer`; all other fields are transported | Startup `RenderBackendDX12` | Bind renderer owners to `RuntimeRenderer`; bind the development owner only to ImGui startup |
| `RuntimeRenderer::RuntimeRenderer` (`Runtime/Render/RuntimeRenderer.cpp:1871`) | Passes the whole view into `RenderResourceLifecycle` | None directly | `RenderResourceLifecycle` becomes the retained renderer borrow owner | Constructor takes only the concrete lifecycle owners |
| `RenderResourceLifecycle::RenderResourceLifecycle` (`Runtime/Render/RenderResourceLifecycle.cpp:44`) | Projects the view into `BackendEpochOwners` and constructs lifecycle helpers | `renderDevice`, `renderFrame`, `renderGraph`, `renderResources`, `renderTextures`, `renderGeometry`, `renderDiagnostics`, `raytracing` | `RuntimeRenderer`/lifecycle is the ratified render authority owner | Delete `BackendEpochOwners`; retain explicit typed members or owner-bound helpers |
| `RuntimeFramePresentationView` (`Runtime/RuntimeFrameViews.h:157-174`) | Transports a mutable reference through every frame phase | None | `Run` currently stores the view | Remove this member in RB2; frame consumers take their real owner subset |
| `BuildFramePresentationView` (`Runtime/App/RunFrame.cpp:279`) | Inserts the stored view into the frame view | None | `Run` | Delete when the frame member is removed |

There are no test-only constructions and no other source/test occurrences.

## Consumption Matrix

The sets below include hidden use through `RendererName()` (diagnostics) and
`RequireBackbufferCapture()` (capture).

| Consumer / operation | Minimum required set | Null handling today | Existing owner / narrower endpoint |
|---|---|---|---|
| `RuntimeRenderBackendView::RendererName` (`RuntimeRenderHost.cpp:42`) | diagnostics | Returns `"unknown"` | `Dx12Diagnostics::GetRendererName()` or a renderer value projection |
| `RuntimeRenderBackendView::RequireBackbufferCapture` (`RuntimeRenderHost.cpp:30`) | backbuffer capture | Lane-F fatal | Direct required `Dx12BackbufferCapture&` |
| `Run::Initialise` (`Run.cpp:504`) | device, frame, graph, resources, textures, geometry, diagnostics | Asserts all seven; later scene-load calls use device/frame/resources/diagnostics | Renderer readiness operation plus narrow scene-load/presentation operands |
| `Run::RunSceneLoadOnly` (`Run.cpp:717`) | device, frame, resources, diagnostics | Passed as nullable pointers to existing scene-load/presentation operations | Narrow renderer name, drain/resource, and presentation operations |
| `Run::BeginFrameTurn` (`RunFrame.cpp:241`) | device, frame, graph, resources, textures, geometry, diagnostics | Lane-F fatal if any is absent | `RuntimeRenderer::RequireReady()` plus `BeginProfilerFrame()` |
| `Run::PrepareRenderPhase` (`RunFrame.cpp:463`) | frame, diagnostics | Both are dereferenced | Renderer operation owns both |
| `Run::RenderWorldPhase` (`RunFrame.cpp:506`) | graph, diagnostics | Graph fatal if absent; diagnostics dereferenced | `RuntimeRenderer::BeginFrameGraph()` without an external graph operand |
| `Run::RunPostDrawDiagnosticsPhase` (`RunFrame.cpp:641`) | backbuffer capture | Both capture paths call the fatal accessor | Required capture reference passed to the two capture operations |
| `Run::PresentFramePhase` (`RunFrame.cpp:696`) | frame, diagnostics | Both are dereferenced | `RuntimeRenderer::Present()` |
| `Run::TickScreenshots` (`RunFrame.cpp:1036`) | backbuffer capture, diagnostics, frame, resources, device | Capture fatal; remaining pointers cross optional scene-advance/debug paths | Required capture reference plus renderer/scene-load narrow operations |
| `Run::TickAutoCycle` (`RunFrame.cpp:1168`) | backbuffer capture, diagnostics | Capture fatal; diagnostics is Debug logging | Required capture reference plus renderer-name/diagnostics projection |
| `Run::TickSceneAdvance` (`RunFrame.cpp:1219`) | diagnostics, frame, resources, device | Passed as nullable pointers to scene-load/presentation operations | Same narrow scene-load/presentation operations as other load paths |
| `Run::Render` (`Runtime/App/RunRender.cpp:44`) | frame, graph, resources, textures, geometry, diagnostics | Returns without rendering unless all six exist | `RuntimeRenderer::RenderFrameEntry()` already owns this readiness decision |
| `ApplyRuntimeUIFrameCommands` (`Runtime/App/InputFrame.cpp:619`) | device, frame | Device is nullable for VSync/graphics-ready policy; frame crosses three scene mutations | Renderer VSync operation and direct scene-load frame borrow |
| `ProcessInputFrame` (`Runtime/App/InputFrameExecution.cpp:106`) | device, frame, resources, diagnostics, backbuffer capture, shader development | Backbuffer absence is fatal after a queued capture; shader absence prints unavailable; diagnostics/device/frame/resources are repeatedly null-tested or forwarded | Capture drain takes required capture; shader action takes optional development capability; other work asks renderer/scene-load endpoints |
| `ApplyUIStressAction` (`Runtime/Capture/RuntimeStressController.cpp:138`) | device | Silently skips device VSync when null | Renderer VSync operation |
| `RunUIStressActions` (`RuntimeStressController.cpp:890`) | device, frame | Device update is conditional; frame is forwarded only inside disabled runtime-churn branches | Renderer VSync operation and direct frame borrow if churn is enabled |
| `RuntimeValidationHarness::ExecuteGraphicsStressFrame` (`RuntimeStressController.cpp:1020`) | device, frame, resources, textures, diagnostics | Texture churn returns early when textures are absent; device/frame/resources are forwarded; diagnostics supplies the renderer name | Narrow scene-load/presentation operations and a texture-churn operation |
| `RenderResourceLifecycle` methods | device, frame, graph, resources, textures, geometry, diagnostics, optional ray tracing | Required resources are dereferenced; release tolerates absent frame/geometry; DXR setup no-ops unless diagnostics support and ray owner are present | Explicit lifecycle members owned by `RuntimeRenderer` |
| `RuntimeRenderer` frame/release methods | frame, graph, resources, textures, geometry, diagnostics, optional ray tracing | Frame readiness returns false; malformed graph/release wiring can fatal; ray tracing falls back to raster | This is the legitimate render authority boundary |

No consumer outside `RuntimeRenderer`/`RenderResourceLifecycle` needs more than
five owners. No external consumer needs the complete eleven-pointer set.

## Backbuffer Capture Call Sites

`RequireBackbufferCapture()` has four lexical calls in three operations:

| Call | Use | Current absent behavior | Required-operand result |
|---|---|---|---|
| `RunPostDrawDiagnosticsPhase` (`RunFrame.cpp:647`) | Pending live-style capture | Lane-F fatal | `SavePendingLiveStyleCapture` receives `Dx12BackbufferCapture&` directly |
| `RunPostDrawDiagnosticsPhase` (`RunFrame.cpp:668`) | Post-render interaction automation capture | Lane-F fatal | `TickInteractionAutomationAfterRender` receives the same required reference |
| `TickScreenshots` (`RunFrame.cpp:1053`) | Screenshot request/automation | Lane-F fatal | `CaptureController::TickScreenshots` receives the required reference |
| `TickAutoCycle` (`RunFrame.cpp:1182`) | Auto-cycle capture | Lane-F fatal | `CaptureController::TickAutoCycle` receives the required reference |

Successful `InitRenderBackend` always publishes `BackbufferCapture()`. No
reachable production capture path supports its absence. It is a required
startup capability, not an optional one.

## Optional Capability Decisions

| Capability | Composer publication | Presence decision today | Behavior when absent |
|---|---|---|---|
| Ray tracing | Successful startup always publishes `&RenderBackendDX12::Raytracing()` | Hardware support is published by `Dx12Diagnostics`; scene setup checks diagnostics + pointer, and reflection checks them again per frame with render policy | Scene DXR setup is a no-op and reflection uses raster |
| Shader development | Successful startup always publishes `&RenderBackendDX12::ShaderDevelopment()` | F9 checks the pointer at action time | Prints “hot reload unavailable” and continues |
| Development UI renderer | Published only in development-tools builds | `ImGuiEditorOwner::Start` checks the pointer once during `Run` construction | Returns a recoverable startup failure; `Run` requests owned application exit |

The ray-tracing owner pointer is not the hardware feature decision in
production; diagnostics capability publication is. RB2 should bind the owner
once and represent the supported/unsupported feature decision once at renderer
composition. Shader development and development UI can be bound as explicit
optional startup operations without entering frame views.

## Binding RB1 Handoff

1. Delete `Run::m_renderBackendView`; do not replace it with another aggregate.
2. Delete `RenderResourceLifecycle::BackendEpochOwners`; bind its concrete
   required owners directly.
3. Keep broad render authority only inside `RuntimeRenderer`/lifecycle.
4. Replace external field reads with narrow renderer, scene-load, capture, or
   development operations.
5. No endpoint may exceed 12 parameters; split by operation rather than
   rebuilding a service bag.
