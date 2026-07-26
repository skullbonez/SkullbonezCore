/*
File: TestOperatorCommandApplier.cpp
Purpose:
  Locks concrete-owner UI command signatures after removing context couriers.

Summary:
  Pins every operator-command family that previously accepted one of the eight
  UI command-context aggregates. The contract test is compile-time focused:
  each helper must borrow the concrete state owners and disposable command
  packet directly.

Mental model:
  UI command values are one-frame requests. OperatorCommandApplier borrows the
  real owners synchronously; no intermediate context object can retain or
  broaden authority.

Invariants:
  - The eight removed context families cannot return through signature drift.
  - Renderer, scene, simulation, config, worker, and gameplay authority remains
    visible at each call site.
  - The test target does not link the full application runtime.

Related:
  - SkullbonezSource/Runtime/Interaction/OperatorCommandApplier.h
  - SkullbonezSource/Runtime/App/InputFrame.cpp
*/
#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Runtime/Interaction/OperatorCommandApplier.h"

#include <type_traits>

namespace
{
using namespace SkullbonezCore;
using namespace SkullbonezCore::Runtime;
using namespace SkullbonezCore::Runtime::RunInternal;

using RenderVsyncCommand =
    bool ( * )( RuntimeRenderer&, Rendering::Dx12RenderDevice*, const UI::UIRendererCommands& );
using SceneFixedStepCommand =
    bool ( * )( SceneSessionState&, SimulationSystem&, const UI::UISceneOptionCommands& );
using RunSimulationCommand =
    RunSimulationUICommandResult ( * )( SceneSessionState&, UI::RunSceneUIOverrideState&, Core::EngineConfig&,
                                        Threading::WorkerPool&, const UI::UISceneOptionCommands&,
                                        const UI::UIRunCommands&, const UI::UIProfilerCommands& );
using RuntimePresentationCommand =
    RuntimePresentationUICommandResult ( * )( OverlayDebugState&, SceneSessionState&, Core::EngineConfig&,
                                              RunLaunchOptions&, RenderDefaultsStore&, bool, double,
                                              const UI::UISceneOptionCommands&, const UI::UIRenderCommands&,
                                              const UI::UIWaterCommands& );
using CinematicRenderingCommand =
    bool ( * )( RunLaunchOptions&, SceneSessionState&, Core::CinematicRenderConfig&,
                const UI::UICinematicCommands& );
using CinematicDefaultsCommand = bool ( * )( RenderDefaultsStore&, const UI::UICinematicCommands& );
using CinematicTuningCommand =
    CinematicTuningUICommandResult ( * )( RunLaunchOptions&, SceneSessionState&, Core::CinematicRenderConfig&,
                                          const UI::UICinematicCommands& );
using PhysicsSleepCommand = bool ( * )( SceneWorld&, const UI::UIPhysicsCommands& );
using PhysicsFrictionCommand =
    PhysicsFrictionUICommandResult ( * )( Core::EngineConfig&, SceneWorld&, const UI::UIPhysicsCommands& );
using TornadoCommand = TornadoUICommandResult ( * )( SceneWorld&, const UI::UIPhysicsCommands& );

constexpr bool HAS_CONCRETE_OPERATOR_COMMAND_SIGNATURES =
    std::is_same_v<decltype( &ApplyRenderVsyncUICommand ), RenderVsyncCommand> &&
    std::is_same_v<decltype( &ApplySceneFixedStepUICommand ), SceneFixedStepCommand> &&
    std::is_same_v<decltype( &ApplyRunSimulationUICommands ), RunSimulationCommand> &&
    std::is_same_v<decltype( &ApplyRuntimePresentationUICommands ), RuntimePresentationCommand> &&
    std::is_same_v<decltype( &ApplyCinematicRenderingToggleUICommand ), CinematicRenderingCommand> &&
    std::is_same_v<decltype( &QueueCinematicSkyDefaultsUICommand ), CinematicDefaultsCommand> &&
    std::is_same_v<decltype( &ApplyCinematicTuningUICommands ), CinematicTuningCommand> &&
    std::is_same_v<decltype( &ApplyPhysicsSleepPolicyUICommand ), PhysicsSleepCommand> &&
    std::is_same_v<decltype( &ApplyPhysicsFrictionUICommands ), PhysicsFrictionCommand> &&
    std::is_same_v<decltype( &ApplyTornadoUICommands ), TornadoCommand>;

static_assert( HAS_CONCRETE_OPERATOR_COMMAND_SIGNATURES );

TEST_CASE( "Operator commands expose concrete owner contracts" )
{
    CHECK( HAS_CONCRETE_OPERATOR_COMMAND_SIGNATURES );
}
} // namespace
