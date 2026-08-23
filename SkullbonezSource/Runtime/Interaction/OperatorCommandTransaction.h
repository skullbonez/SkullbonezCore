/*
File: SkullbonezSource/Runtime/Interaction/OperatorCommandTransaction.h
Purpose:
  Owns one input frame's operator-command order and accepted-action ledger.

Summary:
  OperatorCommandTransaction copies the detached UI command values, advances
  through the only legal owner-mutation order, and retains the accepted-action
  values until InputFrame records them. Concrete runtime owners are borrowed by
  phase calls only and are never retained.
  The transaction is a one-way turnstile around one normalized command packet.
  Each phase sees the final values left by the preceding phase; later commands
  are therefore the explicit winner when two commands touch the same state.

Invariants:
  - The only legal walk is Idle -> DeviceAndMode -> PhysicsControl ->
    RuntimePresentation -> SimulationPolicy -> PhysicsMaterial -> WorldPolicy ->
    CinematicPolicy -> Complete. A skip, repeat, regression, or sentinel phase
    call is Fatal-invariant fatal.
  - Arbitration is encoded by phase and operation order: explicit water mode
    follows reflection cycling; explicit tornado-shell toggle follows tornado
    auto-sync; render/cinematic saves sample later tuning; cinematic mode follows
    the master toggle and precedes feature then parameter tuning.
  - The transaction stores copied command values, acceptance values, and its
    cursor only. No runtime owner pointer or reference survives a phase return.

Related:
  - SkullbonezSource/Runtime/App/InputFrame.cpp
  - SkullbonezSource/Runtime/Scene/SceneCinematicPolicy.h
  - SkullbonezSource/UI/UICommands.h
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "../../UI/UICommands.h"
#include "../../World/WorldEnvironment.h"

#include <cstdint>

namespace SkullbonezCore
{
namespace Assets
{
class AssetSystem;
}
namespace Core
{
class EngineConfig;
struct CinematicRenderConfig;
struct OrdinaryRenderConfig;
} // namespace Core
namespace Environment
{
class WorldEnvironment;
}
namespace Rendering
{
class Dx12RenderDevice;
}
namespace Threading
{
class WorkerPool;
}
namespace UI
{
struct RunSceneBrowserState;
struct RunSceneUIOverrideState;
} // namespace UI
namespace Runtime
{
class RenderDefaultsStore;
class RuntimeRenderer;
class SceneController;
struct SceneSessionState;
class SceneWorld;
struct OverlayDebugState;
struct RunLaunchOptions;
struct OperatorCommandTransactionTestAccess;

class OperatorCommandPhaseCursor
{
  public:
    enum class Phase : uint8_t
    {
        Idle,
        DeviceAndMode,
        PhysicsControl,
        RuntimePresentation,
        SimulationPolicy,
        PhysicsMaterial,
        WorldPolicy,
        CinematicPolicy,
        Complete,
        Count
    };

    static constexpr bool IsLegalTransition( Phase from, Phase to )
    {
        return ( from == Phase::Idle && to == Phase::DeviceAndMode ) ||
               ( from == Phase::DeviceAndMode && to == Phase::PhysicsControl ) ||
               ( from == Phase::PhysicsControl && to == Phase::RuntimePresentation ) ||
               ( from == Phase::RuntimePresentation && to == Phase::SimulationPolicy ) ||
               ( from == Phase::SimulationPolicy && to == Phase::PhysicsMaterial ) ||
               ( from == Phase::PhysicsMaterial && to == Phase::WorldPolicy ) ||
               ( from == Phase::WorldPolicy && to == Phase::CinematicPolicy ) ||
               ( from == Phase::CinematicPolicy && to == Phase::Complete );
    }

    bool TryAdvance( Phase next )
    {
        if ( !IsLegalTransition( m_phase, next ) )
        {
            return false;
        }

        m_phase = next;
        return true;
    }

    Phase Current() const
    {
        return m_phase;
    }

  private:
    Phase m_phase = Phase::Idle;
};

// Concept: one value-only record carries every accepted-action fact beyond its phase.
// Every field has a named InputFrame or replay consumer in the OC0 census.
struct OperatorCommandAcceptanceLedger
{
    bool toggledVsync = false;
    bool cameraModeAccepted = false;
    int cameraModeIndex = -1;

    bool toggledPhysicsSleepPolicy = false;
    bool toggledTornado = false;
    bool toggledTornadoVisualShell = false;
    bool toggledTornadoFieldVectors = false;
    int tornadoApplySettingsActionCount = 0;

    bool toggledTextOnly = false;
    bool toggledFixedStep = false;
    bool toggledTerrainHidden = false;
    bool toggledWaterHidden = false;
    bool toggledWaterFreeze = false;
    bool toggledWaterFlat = false;
    bool toggledSceneShadows = false;
    bool toggledRenderShadows = false;
    bool queuedRenderDefaultsSave = false;
    bool appliedRenderTuning = false;
    bool toggledWaterReflection = false;
    bool setWaterReflectionMode = false;

    bool setTimeScale = false;
    bool setRunSeed = false;
    bool setWorkerThreads = false;
    int frictionApplySettingsActionCount = 0;

    bool worldOverrideAccepted = false;
    Environment::WorldOverrideChange worldOverride;

    bool toggledCinematicRendering = false;
    bool queuedCinematicSkyDefaultsSave = false;
    bool selectedCinematicMode = false;
    bool toggledCinematicFeature = false;
    bool appliedCinematicParam = false;
};

class OperatorCommandTransaction
{
  public:
    explicit OperatorCommandTransaction( const UI::InGameUICommands& commands );
    OperatorCommandTransaction( const OperatorCommandTransaction& ) = delete;
    OperatorCommandTransaction& operator=( const OperatorCommandTransaction& ) = delete;

    void ApplyDeviceAndMode( RuntimeRenderer& renderer, Rendering::Dx12RenderDevice& renderDevice );
    void ApplyPhysicsControl( SceneWorld& world );
    void ApplyRuntimePresentation( OverlayDebugState& debug, SceneSessionState& scene, Core::EngineConfig& config,
                                   RunLaunchOptions& launchOptions, RenderDefaultsStore& renderDefaults, bool graphicsReady,
                                   double simulationSeconds );
    void ApplySimulationPolicy( SceneSessionState& scene, UI::RunSceneUIOverrideState& uiOverrides,
                                Core::EngineConfig& config, Threading::WorkerPool& workerPool );
    void ApplyPhysicsMaterial( Core::EngineConfig& config, SceneWorld& world );
    void ApplyWorldPolicy( Environment::WorldEnvironment& world );
    void ApplyCinematicPolicy( RunLaunchOptions& launchOptions, SceneController& sceneController,
                               UI::RunSceneBrowserState& sceneBrowser, const Assets::AssetSystem& assets,
                               Core::CinematicRenderConfig& activeCinematic,
                               const Core::CinematicRenderConfig& defaultCinematic, RenderDefaultsStore& renderDefaults );
    void Complete();

    OperatorCommandPhaseCursor::Phase Phase() const
    {
        return m_phase.Current();
    }

    const OperatorCommandAcceptanceLedger& Acceptance() const
    {
        return m_acceptance;
    }

  private:
    friend struct OperatorCommandTransactionTestAccess;

    void AdvanceOrFatal( OperatorCommandPhaseCursor::Phase next, const char* operation );
    static void ApplyOrdinaryRenderParam( Core::OrdinaryRenderConfig& ordinary, UI::UIRenderParam param, float rawValue );

    UI::InGameUICommands m_commands;
    OperatorCommandAcceptanceLedger m_acceptance;
    OperatorCommandPhaseCursor m_phase;
};
} // namespace Runtime
} // namespace SkullbonezCore
