/*
File: SkullbonezSource/UI/OperatorEditorExchange.cpp
Purpose:
  Implements bounded operator-editor command validation and arbitration.

Summary:
  Legacy one-frame fields are normalized into fixed domain queues. The merge
  order is fixed, exact active-surface/injected duplicates collapse to one
  request, and a same-action/different-payload conflict reports a recoverable
  result before any runtime owner sees ambiguous intent.

Glossary:
  Action identity: The domain enum value that names one owner-side operation.

Invariants:
  - Validation completes before a command consumes queue capacity.
  - A queue contains at most one payload for each action identity.
  - Arbitration never partially projects a failed merge into owner commands.
  - Float payloads must be finite before they enter a queue.

Related:
  - SkullbonezSource/UI/OperatorEditorExchange.h
  - SkullbonezSource/UI/UICommands.h
  - SkullbonezTests/TestOwnerRequestQueues.cpp
  - Agentic/Reference/engine-glossary.md
*/
#include "OperatorEditorExchange.h"

#include "../Core/SbDiagnosticStore.h"
#include "UICommands.h"
#include "UITabEditor.h"

#include <cmath>
#include <cstring>

namespace SkullbonezCore::UI
{
namespace
{
constexpr const char* OWNER = "UI/OperatorEditorExchange";

template <typename Command, uint32_t Capacity, typename SameIdentity, typename SamePayload>
SkullbonezCore::Core::SbResult SubmitBounded( SkullbonezCore::Core::SbDiagnosticStore& diagnostics,
                                              OperatorEditorCommandQueue<Command, Capacity>& queue, const Command& command,
                                              SameIdentity sameIdentity, SamePayload samePayload, bool* duplicate )
{

    if ( duplicate )
    {
        *duplicate = false;
    }

    for ( uint32_t index = 0u; index < queue.count; ++index )
    {

        if ( !sameIdentity( queue.commands[index], command ) )
        {
            continue;
        }

        if ( !samePayload( queue.commands[index], command ) )
        {
            return diagnostics.Failure( OWNER,
                                        "Conflicting payloads targeted the same operator-editor action in one frame" );
        }

        if ( duplicate )
        {
            *duplicate = true;
        }

        return SkullbonezCore::Core::SbResult::Success();
    }

    if ( queue.count >= Capacity )
    {
        return diagnostics.Failure( OWNER, "Operator-editor command queue exhausted its fixed capacity" );
    }

    queue.commands[queue.count++] = command;
    return SkullbonezCore::Core::SbResult::Success();
}

bool SameSceneIdentity( const OperatorEditorSceneCommand& left, const OperatorEditorSceneCommand& right )
{
    return left.type == right.type;
}

bool SameScenePayload( const OperatorEditorSceneCommand& left, const OperatorEditorSceneCommand& right )
{
    return left.sceneIndex == right.sceneIndex && std::strcmp( left.sceneName, right.sceneName ) == 0;
}

bool SamePropertyIdentity( const OperatorEditorPropertyCommand& left, const OperatorEditorPropertyCommand& right )
{
    return left.type == right.type;
}

bool SamePropertyPayload( const OperatorEditorPropertyCommand& left, const OperatorEditorPropertyCommand& right )
{
    return left.value == right.value && left.integerValue == right.integerValue && left.phase == right.phase;
}

bool IsPhysicsDebugOverlayValue( uint32_t value )
{

    // Invariant: shared editor queues carry only the UI command vocabulary.
    // Runtime performs the later UI-overlay-to-Physics-flag translation.

    switch ( static_cast<UIPhysicsDebugOverlay>( value ) )
    {
    case UIPhysicsDebugOverlay::Axes:
    case UIPhysicsDebugOverlay::Contacts:
    case UIPhysicsDebugOverlay::Sleep:
    case UIPhysicsDebugOverlay::Pipeline:
        return true;
    case UIPhysicsDebugOverlay::None:
        return false;
    }

    return false;
}

bool SameRenderingIdentity( const OperatorEditorRenderingCommand& left, const OperatorEditorRenderingCommand& right )
{
    return left.type == right.type;
}

bool SameRenderingPayload( const OperatorEditorRenderingCommand& left, const OperatorEditorRenderingCommand& right )
{
    return left.parameter == right.parameter && left.value == right.value && left.phase == right.phase;
}

bool SameDiagnosticsIdentity( const OperatorEditorDiagnosticsCommand& left, const OperatorEditorDiagnosticsCommand& right )
{
    return left.type == right.type;
}

bool SameDiagnosticsPayload( const OperatorEditorDiagnosticsCommand& left, const OperatorEditorDiagnosticsCommand& right )
{
    return left.flag == right.flag && left.integerValue == right.integerValue && left.value == right.value &&
           left.phase == right.phase;
}

bool SameReplayIdentity( const OperatorEditorReplayCommand& left, const OperatorEditorReplayCommand& right )
{
    return left.type == right.type;
}

bool SameReplayPayload( const OperatorEditorReplayCommand& left, const OperatorEditorReplayCommand& right )
{
    return left.presetIndex == right.presetIndex && left.retentionSeconds == right.retentionSeconds &&
           left.budgetMiB == right.budgetMiB && left.rowIndex == right.rowIndex && left.value == right.value &&
           left.enabled == right.enabled;
}

bool SameToolIdentity( const OperatorEditorToolCommand& left, const OperatorEditorToolCommand& right )
{

    if ( left.type != right.type )
    {
        return false;
    }

    // Entity flag actions are independent per durable scene object. Selection
    // remains one action identity so a surface and injected producer cannot
    // select two objects in the same turn without a Lane-R conflict.
    return ( left.type != OperatorEditorToolCommandType::SetEntityVisible &&
             left.type != OperatorEditorToolCommandType::SetEntityLocked ) ||
           left.sceneObjectId == right.sceneObjectId;
}

bool SameToolPayload( const OperatorEditorToolCommand& left, const OperatorEditorToolCommand& right )
{
    return left.sceneObjectId == right.sceneObjectId && left.value == right.value && left.enabled == right.enabled;
}

template <typename Queue, typename Submit>
SkullbonezCore::Core::SbResult MergeQueue( SkullbonezCore::Core::SbDiagnosticStore& diagnostics, Queue& target,
                                           const Queue& source, Submit submit, uint32_t& accepted, uint32_t& duplicates )
{

    if ( source.count > Queue::capacity )
    {
        return diagnostics.Failure( OWNER, "Operator-editor command count exceeded queue capacity" );
    }

    for ( uint32_t index = 0u; index < source.count; ++index )
    {
        bool duplicate = false;
        const SkullbonezCore::Core::SbResult result = submit( target, source.commands[index], &duplicate );

        if ( !result.Ok() )
        {
            return result;
        }

        if ( duplicate )
        {
            ++duplicates;
        }
        else
        {
            ++accepted;
        }
    }

    return SkullbonezCore::Core::SbResult::Success();
}

void HashBytes( uint64_t& hash, const void* bytes, size_t count ) noexcept
{
    const auto* data = static_cast<const uint8_t*>( bytes );

    for ( size_t index = 0u; index < count; ++index )
    {
        hash ^= data[index];
        hash *= 1099511628211ull;
    }
}

template <typename Value> void HashValue( uint64_t& hash, const Value& value ) noexcept
{
    HashBytes( hash, &value, sizeof( value ) );
}
} // namespace

SkullbonezCore::Core::SbResult SubmitOperatorEditorCommand( SkullbonezCore::Core::SbDiagnosticStore& diagnostics,
                                                            OperatorEditorSceneCommandQueue& queue,
                                                            const OperatorEditorSceneCommand& command, bool* duplicate )
{

    if ( command.type != OperatorEditorSceneCommandType::ResetCurrentScene &&
         command.type != OperatorEditorSceneCommandType::ResetSceneDefaults &&
         command.type != OperatorEditorSceneCommandType::RequestDemoScene &&
         command.type != OperatorEditorSceneCommandType::SetCurrentSceneIndex &&
         command.type != OperatorEditorSceneCommandType::SaveCurrentScene &&
         command.type != OperatorEditorSceneCommandType::CreateScene )
    {
        return diagnostics.Failure( OWNER, "Scene command has an unknown action type" );
    }

    if ( command.type == OperatorEditorSceneCommandType::SetCurrentSceneIndex && command.sceneIndex < 0 )
    {
        return diagnostics.Failure( OWNER, "Scene index command requires a non-negative index" );
    }

    if ( command.type == OperatorEditorSceneCommandType::CreateScene &&
         ( command.sceneName[0] == '\0' || std::memchr( command.sceneName, '\0', sizeof( command.sceneName ) ) == nullptr ) )
    {
        return diagnostics.Failure( OWNER, "Create-scene command requires a bounded non-empty name" );
    }

    return SubmitBounded( diagnostics, queue, command, SameSceneIdentity, SameScenePayload, duplicate );
}

SkullbonezCore::Core::SbResult SubmitOperatorEditorCommand( SkullbonezCore::Core::SbDiagnosticStore& diagnostics,
                                                            OperatorEditorPropertyCommandQueue& queue,
                                                            const OperatorEditorPropertyCommand& command, bool* duplicate )
{

    switch ( command.type )
    {
    case OperatorEditorPropertyCommandType::SetTimeScale:
    case OperatorEditorPropertyCommandType::ToggleFixedStep:
    case OperatorEditorPropertyCommandType::SetModelCount:
    case OperatorEditorPropertyCommandType::SetSeed:
    case OperatorEditorPropertyCommandType::SetSolverBallCount:
    case OperatorEditorPropertyCommandType::SetSolverBoxCount:
    case OperatorEditorPropertyCommandType::SetWorldGravity:
    case OperatorEditorPropertyCommandType::SetWorldFluidHeight:
    case OperatorEditorPropertyCommandType::SetWorldFluidDensity:
    case OperatorEditorPropertyCommandType::TogglePhysicsSleepPolicy:
    case OperatorEditorPropertyCommandType::SetTerrainFriction:
    case OperatorEditorPropertyCommandType::SetObjectFriction:
    case OperatorEditorPropertyCommandType::SetRollingFriction:
    case OperatorEditorPropertyCommandType::ToggleTornado:
    case OperatorEditorPropertyCommandType::SetTornadoRadius:
    case OperatorEditorPropertyCommandType::SetTornadoHeight:
    case OperatorEditorPropertyCommandType::SetTornadoInward:
    case OperatorEditorPropertyCommandType::SetTornadoSwirl:
    case OperatorEditorPropertyCommandType::SetTornadoLift:
        break;
    default:
        return diagnostics.Failure( OWNER, "Property command has an unknown action type" );
    }

    if ( command.phase != OperatorEditorEditPhase::Preview && command.phase != OperatorEditorEditPhase::Commit )
    {
        return diagnostics.Failure( OWNER, "Property command has an unknown edit phase" );
    }

    if ( !std::isfinite( command.value ) )
    {
        return diagnostics.Failure( OWNER, "Property command requires a finite value" );
    }

    if ( command.type == OperatorEditorPropertyCommandType::SetTimeScale && command.value <= 0.0f )
    {
        return diagnostics.Failure( OWNER, "Time-scale command requires a positive value" );
    }

    if ( command.type == OperatorEditorPropertyCommandType::SetSeed && command.integerValue <= 0 )
    {
        return diagnostics.Failure( OWNER, "Seed command requires a positive integer" );
    }

    if ( ( command.type == OperatorEditorPropertyCommandType::SetModelCount ||
           command.type == OperatorEditorPropertyCommandType::SetSolverBallCount ||
           command.type == OperatorEditorPropertyCommandType::SetSolverBoxCount ) &&
         command.integerValue < 0 )
    {
        return diagnostics.Failure( OWNER, "Population command requires a non-negative integer" );
    }

    return SubmitBounded( diagnostics, queue, command, SamePropertyIdentity, SamePropertyPayload, duplicate );
}

SkullbonezCore::Core::SbResult SubmitOperatorEditorCommand( SkullbonezCore::Core::SbDiagnosticStore& diagnostics,
                                                            OperatorEditorRenderingCommandQueue& queue,
                                                            const OperatorEditorRenderingCommand& command, bool* duplicate )
{

    switch ( command.type )
    {
    case OperatorEditorRenderingCommandType::ToggleVsync:
    case OperatorEditorRenderingCommandType::ToggleShadows:
    case OperatorEditorRenderingCommandType::ToggleTerrainHidden:
    case OperatorEditorRenderingCommandType::ToggleWaterHidden:
    case OperatorEditorRenderingCommandType::ToggleWaterFreeze:
    case OperatorEditorRenderingCommandType::ToggleWaterFlat:
    case OperatorEditorRenderingCommandType::CycleWaterReflection:
    case OperatorEditorRenderingCommandType::ToggleCinematicRendering:
    case OperatorEditorRenderingCommandType::SaveOrdinaryDefaults:
    case OperatorEditorRenderingCommandType::SaveSkyDefaults:
        break;
    case OperatorEditorRenderingCommandType::ToggleCinematicFeature:

        if ( command.parameter < 0 || command.parameter >= static_cast<int>( UICinematicFeature::Count ) )
        {
            return diagnostics.Failure( OWNER, "Rendering feature index is out of range" );
        }

        break;
    case OperatorEditorRenderingCommandType::SetOrdinaryParameter:

        if ( command.parameter < 0 || command.parameter >= static_cast<int>( UIRenderParam::Count ) )
        {
            return diagnostics.Failure( OWNER, "Ordinary render parameter is out of range" );
        }

        break;
    case OperatorEditorRenderingCommandType::SetCinematicParameter:

        if ( command.parameter < 0 || command.parameter >= static_cast<int>( UICinematicParam::Count ) )
        {
            return diagnostics.Failure( OWNER, "Cinematic render parameter is out of range" );
        }

        break;
    default:
        return diagnostics.Failure( OWNER, "Rendering command has an unknown action type" );
    }

    if ( !std::isfinite( command.value ) )
    {
        return diagnostics.Failure( OWNER, "Rendering command requires a finite value" );
    }

    if ( command.phase != OperatorEditorEditPhase::Preview && command.phase != OperatorEditorEditPhase::Commit )
    {
        return diagnostics.Failure( OWNER, "Rendering command has an unknown edit phase" );
    }

    return SubmitBounded( diagnostics, queue, command, SameRenderingIdentity, SameRenderingPayload, duplicate );
}

SkullbonezCore::Core::SbResult SubmitOperatorEditorCommand( SkullbonezCore::Core::SbDiagnosticStore& diagnostics,
                                                            OperatorEditorDiagnosticsCommandQueue& queue,
                                                            const OperatorEditorDiagnosticsCommand& command,
                                                            bool* duplicate )
{

    switch ( command.type )
    {
    case OperatorEditorDiagnosticsCommandType::ToggleCollisionVisualizer:
    case OperatorEditorDiagnosticsCommandType::TogglePhysicsDebugTransparent:
    case OperatorEditorDiagnosticsCommandType::ToggleBroadphaseOverlay:
    case OperatorEditorDiagnosticsCommandType::ToggleTerrainContactProbe:
    case OperatorEditorDiagnosticsCommandType::ToggleTornadoVisualShell:
    case OperatorEditorDiagnosticsCommandType::ToggleTornadoFieldVectors:
    case OperatorEditorDiagnosticsCommandType::ToggleRayCastVisualization:
    case OperatorEditorDiagnosticsCommandType::StepPhysicsPipelinePrevious:
    case OperatorEditorDiagnosticsCommandType::StepPhysicsPipelineNext:
        break;
    case OperatorEditorDiagnosticsCommandType::TogglePhysicsDebugFlag:

        if ( !IsPhysicsDebugOverlayValue( command.flag ) )
        {
            return diagnostics.Failure( OWNER, "Physics debug overlay command requires one recognized UI overlay value" );
        }

        break;
    case OperatorEditorDiagnosticsCommandType::SetPhysicsDebugAlpha:
    case OperatorEditorDiagnosticsCommandType::SetPhysicsContactLinger:
    case OperatorEditorDiagnosticsCommandType::SetRayCastImpulseStrength:
    case OperatorEditorDiagnosticsCommandType::SetLauncherProjectileSpeed:

        if ( !std::isfinite( command.value ) )
        {
            return diagnostics.Failure( OWNER, "Diagnostics value must be finite" );
        }

        break;
    case OperatorEditorDiagnosticsCommandType::SetWorkerThreads:

        if ( command.integerValue < -1 )
        {
            return diagnostics.Failure( OWNER, "Worker count must be auto, disabled, or positive" );
        }

        break;
    default:
        return diagnostics.Failure( OWNER, "Diagnostics command has an unknown action type" );
    }

    if ( command.phase != OperatorEditorEditPhase::Preview && command.phase != OperatorEditorEditPhase::Commit )
    {
        return diagnostics.Failure( OWNER, "Diagnostics command has an unknown edit phase" );
    }

    return SubmitBounded( diagnostics, queue, command, SameDiagnosticsIdentity, SameDiagnosticsPayload, duplicate );
}

SkullbonezCore::Core::SbResult SubmitOperatorEditorCommand( SkullbonezCore::Core::SbDiagnosticStore& diagnostics,
                                                            OperatorEditorReplayCommandQueue& queue,
                                                            const OperatorEditorReplayCommand& command, bool* duplicate )
{

    switch ( command.type )
    {
    case OperatorEditorReplayCommandType::SetMemoryPolicy:
    {
        const bool presetValid = command.presetIndex >= -1;
        const bool retentionValid = command.retentionSeconds == -1 || command.retentionSeconds > 0;
        const bool budgetValid = command.budgetMiB == -1 || command.budgetMiB > 0;
        const bool requestsValue = command.presetIndex >= 0 || command.retentionSeconds > 0 || command.budgetMiB > 0;

        if ( !presetValid || !retentionValid || !budgetValid || !requestsValue )
        {
            return diagnostics
                .Failure( OWNER, "Replay memory command requires at least one valid preset, retention, or budget value" );
        }

        break;
    }
    case OperatorEditorReplayCommandType::SetRevealSpeed:

        if ( !std::isfinite( command.value ) || command.value < 0.25f || command.value > 4.0f )
        {
            return diagnostics.Failure( OWNER, "Replay reveal speed must be within 0.25x..4x" );
        }

        break;
    case OperatorEditorReplayCommandType::Scrub:

        if ( !std::isfinite( command.value ) || command.value < 0.0f || command.value > 1.0f )
        {
            return diagnostics.Failure( OWNER, "Replay scrub position must be normalized" );
        }

        break;
    case OperatorEditorReplayCommandType::SetPredictionHorizon:

        if ( !std::isfinite( command.value ) || command.value < 1.0f || command.value > 20.0f )
        {
            return diagnostics.Failure( OWNER, "Replay prediction horizon must be 1..20 seconds" );
        }

        break;
    case OperatorEditorReplayCommandType::SelectCauseRow:

        if ( command.rowIndex < 0 )
        {
            return diagnostics.Failure( OWNER, "Replay cause row must be non-negative" );
        }

        break;
    case OperatorEditorReplayCommandType::SetRecordingEnabled:
    case OperatorEditorReplayCommandType::JumpToStart:
    case OperatorEditorReplayCommandType::JumpToEnd:
    case OperatorEditorReplayCommandType::TogglePlayPause:
    case OperatorEditorReplayCommandType::StepBackward:
    case OperatorEditorReplayCommandType::StepForward:
    case OperatorEditorReplayCommandType::TogglePrediction:
    case OperatorEditorReplayCommandType::RestoreBranch:
    case OperatorEditorReplayCommandType::Save:
    case OperatorEditorReplayCommandType::Load:
    case OperatorEditorReplayCommandType::ReturnToLive:
        break;
    default:
        return diagnostics.Failure( OWNER, "Replay command has an unknown action type" );
    }

    return SubmitBounded( diagnostics, queue, command, SameReplayIdentity, SameReplayPayload, duplicate );
}

SkullbonezCore::Core::SbResult SubmitOperatorEditorCommand( SkullbonezCore::Core::SbDiagnosticStore& diagnostics,
                                                            OperatorEditorToolCommandQueue& queue,
                                                            const OperatorEditorToolCommand& command, bool* duplicate )
{

    switch ( command.type )
    {
    case OperatorEditorToolCommandType::ToggleEditorMode:
    case OperatorEditorToolCommandType::TogglePlacementMode:
    case OperatorEditorToolCommandType::Undo:
    case OperatorEditorToolCommandType::Redo:
    case OperatorEditorToolCommandType::ToggleCrossScenePause:
    case OperatorEditorToolCommandType::StepPausedScene:
    case OperatorEditorToolCommandType::DeleteSelection:
    case OperatorEditorToolCommandType::DuplicateSelection:
        return SubmitBounded( diagnostics, queue, command, SameToolIdentity, SameToolPayload, duplicate );
    case OperatorEditorToolCommandType::SelectSceneObject:
    case OperatorEditorToolCommandType::SetEntityVisible:
    case OperatorEditorToolCommandType::SetEntityLocked:

        if ( command.sceneObjectId == 0u )
        {
            return diagnostics.Failure( OWNER, "Hierarchy command requires a stable scene object id" );
        }

        return SubmitBounded( diagnostics, queue, command, SameToolIdentity, SameToolPayload, duplicate );
    case OperatorEditorToolCommandType::SetPlacementObjectType:

        if ( command.value < 0 || command.value >= EditorTab::OBJECT_TYPE_COUNT )
        {
            return diagnostics.Failure( OWNER, "Placement command has an invalid object type" );
        }

        return SubmitBounded( diagnostics, queue, command, SameToolIdentity, SameToolPayload, duplicate );
    case OperatorEditorToolCommandType::SetPlaceStatic:
    case OperatorEditorToolCommandType::ToggleTerrainAlign:
        return SubmitBounded( diagnostics, queue, command, SameToolIdentity, SameToolPayload, duplicate );
    default:
        return diagnostics.Failure( OWNER, "Tool command has an unknown action type" );
    }
}

SkullbonezCore::Core::SbResult NormalizeLegacyOperatorEditorCommands( SkullbonezCore::Core::SbDiagnosticStore& diagnostics,
                                                                      InGameUICommands& commands )
{

    // Invariant: normalization drains legacy one-frame fields exactly once.
    // Typed queues then become the only arbitration input for either surface.
    OperatorEditorCommandQueues normalized = commands.operatorEditor;
    SkullbonezCore::Core::SbResult result = SkullbonezCore::Core::SbResult::Success();

    if ( commands.scene.resetScene )
    {
        result = SubmitOperatorEditorCommand( diagnostics, normalized.scene,
                                              OperatorEditorSceneCommand { OperatorEditorSceneCommandType::ResetCurrentScene,
                                                                           -1 } );
    }

    if ( result.Ok() && commands.scene.resetSceneDefaults )
    {
        result = SubmitOperatorEditorCommand( diagnostics, normalized.scene,
                                              OperatorEditorSceneCommand { OperatorEditorSceneCommandType::
                                                                               ResetSceneDefaults,
                                                                           -1 } );
    }

    if ( result.Ok() && commands.scene.requestDemoScene )
    {
        result = SubmitOperatorEditorCommand( diagnostics, normalized.scene,
                                              OperatorEditorSceneCommand { OperatorEditorSceneCommandType::RequestDemoScene,
                                                                           -1 } );
    }

    if ( result.Ok() && commands.scene.saveSceneDefaults )
    {
        result = SubmitOperatorEditorCommand( diagnostics, normalized.scene,
                                              OperatorEditorSceneCommand { OperatorEditorSceneCommandType::SaveCurrentScene,
                                                                           -1 } );
    }

    if ( result.Ok() && commands.scene.createScene )
    {
        OperatorEditorSceneCommand create;
        create.type = OperatorEditorSceneCommandType::CreateScene;
        strncpy_s( create.sceneName, commands.scene.requestedSceneName, _TRUNCATE );
        result = SubmitOperatorEditorCommand( diagnostics, normalized.scene, create );
    }

    if ( result.Ok() && commands.scene.requestedSceneIndex >= 0 )
    {
        result = SubmitOperatorEditorCommand( diagnostics, normalized.scene,
                                              OperatorEditorSceneCommand { OperatorEditorSceneCommandType::
                                                                               SetCurrentSceneIndex,
                                                                           commands.scene.requestedSceneIndex } );
    }

    const auto normalizeProperty = [&]( bool requested,
                                       OperatorEditorPropertyCommandType type, float value = 0.0f, int integerValue = 0 )
    {

        if ( result.Ok() && requested )
        {
            result = SubmitOperatorEditorCommand( diagnostics, normalized.property,
                                                  OperatorEditorPropertyCommand { type, value, integerValue,
                                                                                  OperatorEditorEditPhase::Commit } );
        }
    };

    normalizeProperty( commands.sceneOptions.requestedTimeScale > 0.0f, OperatorEditorPropertyCommandType::SetTimeScale,
                       commands.sceneOptions.requestedTimeScale );

    normalizeProperty( commands.sceneOptions.toggleFixedStep, OperatorEditorPropertyCommandType::ToggleFixedStep );
    normalizeProperty( commands.sceneOptions.requestedModelCount >= 0, OperatorEditorPropertyCommandType::SetModelCount,
                       0.0f, commands.sceneOptions.requestedModelCount );

    normalizeProperty( commands.run.requestedSeed > 0, OperatorEditorPropertyCommandType::SetSeed, 0.0f,
                       commands.run.requestedSeed );

    normalizeProperty( commands.run.requestedSolverBallCount >= 0, OperatorEditorPropertyCommandType::SetSolverBallCount,
                       0.0f, commands.run.requestedSolverBallCount );

    normalizeProperty( commands.run.requestedSolverBoxCount >= 0, OperatorEditorPropertyCommandType::SetSolverBoxCount, 0.0f,
                       commands.run.requestedSolverBoxCount );

    normalizeProperty( commands.water.requestWorldGravity, OperatorEditorPropertyCommandType::SetWorldGravity,
                       commands.water.requestedWorldGravity );

    normalizeProperty( commands.water.requestWorldFluidHeight, OperatorEditorPropertyCommandType::SetWorldFluidHeight,
                       commands.water.requestedWorldFluidHeight );

    normalizeProperty( commands.water.requestWorldFluidDensity, OperatorEditorPropertyCommandType::SetWorldFluidDensity,
                       commands.water.requestedWorldFluidDensity );

    normalizeProperty( commands.physics.togglePhysicsSleepPolicy,
                       OperatorEditorPropertyCommandType::TogglePhysicsSleepPolicy );

    normalizeProperty( commands.physics.requestTerrainFrictionCoeff, OperatorEditorPropertyCommandType::SetTerrainFriction,
                       commands.physics.requestedTerrainFrictionCoeff );

    normalizeProperty( commands.physics.requestObjectFrictionCoeff, OperatorEditorPropertyCommandType::SetObjectFriction,
                       commands.physics.requestedObjectFrictionCoeff );

    normalizeProperty( commands.physics.requestRollingFrictionCoeff, OperatorEditorPropertyCommandType::SetRollingFriction,
                       commands.physics.requestedRollingFrictionCoeff );

    normalizeProperty( commands.physics.toggleTornado, OperatorEditorPropertyCommandType::ToggleTornado );
    normalizeProperty( commands.physics.requestTornadoRadius, OperatorEditorPropertyCommandType::SetTornadoRadius,
                       commands.physics.requestedTornadoRadius );

    normalizeProperty( commands.physics.requestTornadoHeight, OperatorEditorPropertyCommandType::SetTornadoHeight,
                       commands.physics.requestedTornadoHeight );

    normalizeProperty( commands.physics.requestTornadoInward, OperatorEditorPropertyCommandType::SetTornadoInward,
                       commands.physics.requestedTornadoInward );

    normalizeProperty( commands.physics.requestTornadoSwirl, OperatorEditorPropertyCommandType::SetTornadoSwirl,
                       commands.physics.requestedTornadoSwirl );

    normalizeProperty( commands.physics.requestTornadoLift, OperatorEditorPropertyCommandType::SetTornadoLift,
                       commands.physics.requestedTornadoLift );

    if ( result.Ok() && commands.renderer.toggleVsync )
    {
        result = SubmitOperatorEditorCommand( diagnostics, normalized.rendering,
                                              OperatorEditorRenderingCommand {
                                                  OperatorEditorRenderingCommandType::ToggleVsync } );
    }

    const auto normalizeRendering = [&]( bool requested,
                                        OperatorEditorRenderingCommandType type, int parameter = -1, float value = 0.0f )
    {

        if ( result.Ok() && requested )
        {
            result = SubmitOperatorEditorCommand( diagnostics, normalized.rendering,
                                                  OperatorEditorRenderingCommand { type, parameter, value,
                                                                                   OperatorEditorEditPhase::Commit } );
        }
    };

    normalizeRendering( commands.sceneOptions.toggleShadows, OperatorEditorRenderingCommandType::ToggleShadows );
    normalizeRendering( commands.renderTuning.toggleShadows, OperatorEditorRenderingCommandType::ToggleShadows );
    normalizeRendering( commands.sceneOptions.toggleTerrainHidden, OperatorEditorRenderingCommandType::ToggleTerrainHidden );

    normalizeRendering( commands.sceneOptions.toggleWaterHidden, OperatorEditorRenderingCommandType::ToggleWaterHidden );

    normalizeRendering( commands.sceneOptions.toggleWaterFreeze, OperatorEditorRenderingCommandType::ToggleWaterFreeze );

    normalizeRendering( commands.sceneOptions.toggleWaterFlat, OperatorEditorRenderingCommandType::ToggleWaterFlat );
    normalizeRendering( commands.water.toggleWaterReflection, OperatorEditorRenderingCommandType::CycleWaterReflection );

    normalizeRendering( commands.cinematic.toggleRendering, OperatorEditorRenderingCommandType::ToggleCinematicRendering );

    normalizeRendering( commands.renderTuning.saveDefaults, OperatorEditorRenderingCommandType::SaveOrdinaryDefaults );
    normalizeRendering( commands.cinematic.saveSkyDefaults, OperatorEditorRenderingCommandType::SaveSkyDefaults );
    normalizeRendering( commands.cinematic.requestedFeature != UICinematicFeature::None,
                        OperatorEditorRenderingCommandType::ToggleCinematicFeature,
                        static_cast<int>( commands.cinematic.requestedFeature ) );

    normalizeRendering( commands.renderTuning.requestedParam != UIRenderParam::None,
                        OperatorEditorRenderingCommandType::SetOrdinaryParameter,
                        static_cast<int>( commands.renderTuning.requestedParam ), commands.renderTuning.requestedValue );

    normalizeRendering( commands.cinematic.requestedParam != UICinematicParam::None,
                        OperatorEditorRenderingCommandType::SetCinematicParameter,
                        static_cast<int>( commands.cinematic.requestedParam ), commands.cinematic.requestedValue );

    const auto normalizeDiagnostics = [&]( bool requested, OperatorEditorDiagnosticsCommandType type, uint32_t flag = 0u,
                                           int integerValue = 0, float value = 0.0f )
    {

        if ( result.Ok() && requested )
        {
            result = SubmitOperatorEditorCommand( diagnostics, normalized.diagnostics,
                                                  OperatorEditorDiagnosticsCommand { type, flag, integerValue, value,
                                                                                     OperatorEditorEditPhase::Commit } );
        }
    };

    normalizeDiagnostics( commands.physics.toggleCollisionVisualizer,
                          OperatorEditorDiagnosticsCommandType::ToggleCollisionVisualizer );

    normalizeDiagnostics( commands.physics.togglePhysicsDebugTransparent,
                          OperatorEditorDiagnosticsCommandType::TogglePhysicsDebugTransparent );

    normalizeDiagnostics( commands.physics.toggleBroadphaseOverlay,
                          OperatorEditorDiagnosticsCommandType::ToggleBroadphaseOverlay );

    normalizeDiagnostics( commands.physics.toggleTerrainContactProbe,
                          OperatorEditorDiagnosticsCommandType::ToggleTerrainContactProbe );

    normalizeDiagnostics( commands.physics.toggleTornadoVisualShell,
                          OperatorEditorDiagnosticsCommandType::ToggleTornadoVisualShell );

    normalizeDiagnostics( commands.physics.toggleTornadoFieldVectors,
                          OperatorEditorDiagnosticsCommandType::ToggleTornadoFieldVectors );

    normalizeDiagnostics( commands.physics.toggleRayCastVisualization,
                          OperatorEditorDiagnosticsCommandType::ToggleRayCastVisualization );

    normalizeDiagnostics( commands.physics.physicsDebugOverlayToToggle != UIPhysicsDebugOverlay::None,
                          OperatorEditorDiagnosticsCommandType::TogglePhysicsDebugFlag,
                          static_cast<uint32_t>( commands.physics.physicsDebugOverlayToToggle ) );

    normalizeDiagnostics( commands.physics.stepPhysicsPipelinePrevious,
                          OperatorEditorDiagnosticsCommandType::StepPhysicsPipelinePrevious );

    normalizeDiagnostics( commands.physics.stepPhysicsPipelineNext,
                          OperatorEditorDiagnosticsCommandType::StepPhysicsPipelineNext );

    normalizeDiagnostics( commands.physics.requestedPhysicsDebugAlpha >= 0.0f,
                          OperatorEditorDiagnosticsCommandType::SetPhysicsDebugAlpha, 0u, 0,
                          commands.physics.requestedPhysicsDebugAlpha );

    normalizeDiagnostics( commands.physics.requestedPhysicsDebugContactLinger >= 0.0f,
                          OperatorEditorDiagnosticsCommandType::SetPhysicsContactLinger, 0u, 0,
                          commands.physics.requestedPhysicsDebugContactLinger );

    normalizeDiagnostics( commands.physics.requestRayCastImpulseStrength,
                          OperatorEditorDiagnosticsCommandType::SetRayCastImpulseStrength, 0u, 0,
                          commands.physics.requestedRayCastImpulseStrength );

    normalizeDiagnostics( commands.physics.requestLauncherProjectileSpeed,
                          OperatorEditorDiagnosticsCommandType::SetLauncherProjectileSpeed, 0u, 0,
                          commands.physics.requestedLauncherProjectileSpeed );

    normalizeDiagnostics( commands.profiler.requestedWorkerThreads >= -1,
                          OperatorEditorDiagnosticsCommandType::SetWorkerThreads, 0u,
                          commands.profiler.requestedWorkerThreads );

    if ( result.Ok() && commands.replayMemory.requestPolicy )
    {
        result = SubmitOperatorEditorCommand( diagnostics, normalized.replay,
                                              OperatorEditorReplayCommand { OperatorEditorReplayCommandType::SetMemoryPolicy,
                                                                            commands.replayMemory.requestedPresetIndex,
                                                                            commands.replayMemory.requestedRetentionSeconds,
                                                                            commands.replayMemory.requestedBudgetMiB } );
    }

    const auto normalizeTool = [&]( bool requested, OperatorEditorToolCommandType type )
    {

        if ( result.Ok() && requested )
        {
            result = SubmitOperatorEditorCommand( diagnostics, normalized.tools, OperatorEditorToolCommand { type } );
        }
    };

    normalizeTool( commands.editor.toggleEditorMode, OperatorEditorToolCommandType::ToggleEditorMode );
    normalizeTool( commands.editor.togglePlacementMode, OperatorEditorToolCommandType::TogglePlacementMode );
    normalizeTool( commands.editor.requestUndo, OperatorEditorToolCommandType::Undo );
    normalizeTool( commands.editor.requestRedo, OperatorEditorToolCommandType::Redo );
    normalizeTool( commands.scene.toggleCrossScenePause, OperatorEditorToolCommandType::ToggleCrossScenePause );
    normalizeTool( commands.scene.requestSingleStep, OperatorEditorToolCommandType::StepPausedScene );

    if ( result.Ok() && commands.editor.requestedObjectType >= 0 )
    {
        result = SubmitOperatorEditorCommand( diagnostics, normalized.tools,
                                              OperatorEditorToolCommand { OperatorEditorToolCommandType::
                                                                              SetPlacementObjectType,
                                                                          0u, commands.editor.requestedObjectType } );
    }

    if ( result.Ok() && commands.editor.requestPlaceStatic )
    {
        result = SubmitOperatorEditorCommand( diagnostics, normalized.tools,
                                              OperatorEditorToolCommand { OperatorEditorToolCommandType::SetPlaceStatic, 0u,
                                                                          0, commands.editor.requestedPlaceStatic } );
    }

    if ( !result.Ok() )
    {
        return result;
    }

    commands.operatorEditor = normalized;
    commands.scene.resetScene = false;
    commands.scene.resetSceneDefaults = false;
    commands.scene.requestDemoScene = false;
    commands.scene.saveSceneDefaults = false;
    commands.scene.createScene = false;
    commands.scene.requestedSceneName[0] = '\0';
    commands.scene.requestedSceneIndex = -1;
    commands.sceneOptions.requestedTimeScale = -1.0f;
    commands.sceneOptions.toggleFixedStep = false;
    commands.sceneOptions.requestedModelCount = -1;
    commands.run.requestedSeed = -1;
    commands.run.requestedSolverBallCount = -1;
    commands.run.requestedSolverBoxCount = -1;
    commands.water.requestWorldGravity = false;
    commands.water.requestWorldFluidHeight = false;
    commands.water.requestWorldFluidDensity = false;
    commands.physics.togglePhysicsSleepPolicy = false;
    commands.physics.requestTerrainFrictionCoeff = false;
    commands.physics.requestObjectFrictionCoeff = false;
    commands.physics.requestRollingFrictionCoeff = false;
    commands.physics.toggleTornado = false;
    commands.physics.requestTornadoRadius = false;
    commands.physics.requestTornadoHeight = false;
    commands.physics.requestTornadoInward = false;
    commands.physics.requestTornadoSwirl = false;
    commands.physics.requestTornadoLift = false;
    commands.renderer.toggleVsync = false;
    commands.sceneOptions.toggleShadows = false;
    commands.sceneOptions.toggleTerrainHidden = false;
    commands.sceneOptions.toggleWaterHidden = false;
    commands.sceneOptions.toggleWaterFreeze = false;
    commands.sceneOptions.toggleWaterFlat = false;
    commands.water.toggleWaterReflection = false;
    commands.renderTuning.toggleShadows = false;
    commands.renderTuning.saveDefaults = false;
    commands.renderTuning.requestedParam = UIRenderParam::None;
    commands.cinematic.toggleRendering = false;
    commands.cinematic.saveSkyDefaults = false;
    commands.cinematic.requestedFeature = UICinematicFeature::None;
    commands.cinematic.requestedParam = UICinematicParam::None;
    commands.physics.toggleCollisionVisualizer = false;
    commands.physics.togglePhysicsDebugTransparent = false;
    commands.physics.toggleBroadphaseOverlay = false;
    commands.physics.toggleTerrainContactProbe = false;
    commands.physics.toggleTornadoVisualShell = false;
    commands.physics.toggleTornadoFieldVectors = false;
    commands.physics.toggleRayCastVisualization = false;
    commands.physics.physicsDebugOverlayToToggle = UIPhysicsDebugOverlay::None;
    commands.physics.stepPhysicsPipelinePrevious = false;
    commands.physics.stepPhysicsPipelineNext = false;
    commands.physics.requestedPhysicsDebugAlpha = -1.0f;
    commands.physics.requestedPhysicsDebugContactLinger = -1.0f;
    commands.physics.requestRayCastImpulseStrength = false;
    commands.physics.requestLauncherProjectileSpeed = false;
    commands.profiler.requestedWorkerThreads = -2;
    commands.replayMemory.requestPolicy = false;
    commands.editor.toggleEditorMode = false;
    commands.editor.togglePlacementMode = false;
    commands.editor.requestUndo = false;
    commands.editor.requestRedo = false;
    commands.editor.requestedObjectType = -1;
    commands.editor.requestPlaceStatic = false;
    commands.scene.toggleCrossScenePause = false;
    commands.scene.requestSingleStep = false;
    return SkullbonezCore::Core::SbResult::Success();
}

OperatorEditorArbitrationResult ArbitrateOperatorEditorCommands( SkullbonezCore::Core::SbDiagnosticStore& diagnostics,
                                                                 const OperatorEditorCommandQueues& legacy,
                                                                 const OperatorEditorCommandQueues& secondary )
{
    OperatorEditorArbitrationResult result;
    auto mergeAll = [&]( const OperatorEditorCommandQueues& source, uint32_t& accepted )
    {
        SkullbonezCore::Core::SbResult status = MergeQueue( diagnostics, result.commands.scene, source.scene,
                                                            [&diagnostics]( OperatorEditorSceneCommandQueue& queue, const OperatorEditorSceneCommand& command,
                                                                            bool* duplicate )
                                                            { return SubmitOperatorEditorCommand( diagnostics, queue, command, duplicate ); },
                                                            accepted, result.coalescedDuplicateCommands );

        if ( status.Ok() )
        {
            status = MergeQueue( diagnostics, result.commands.property, source.property,
                                 [&diagnostics]( OperatorEditorPropertyCommandQueue& queue, const OperatorEditorPropertyCommand& command,
                                                 bool* duplicate )
                                 { return SubmitOperatorEditorCommand( diagnostics, queue, command, duplicate ); },
                                 accepted, result.coalescedDuplicateCommands );
        }

        if ( status.Ok() )
        {
            status = MergeQueue( diagnostics, result.commands.rendering, source.rendering,
                                 [&diagnostics]( OperatorEditorRenderingCommandQueue& queue, const OperatorEditorRenderingCommand& command,
                                                 bool* duplicate )
                                 { return SubmitOperatorEditorCommand( diagnostics, queue, command, duplicate ); },
                                 accepted, result.coalescedDuplicateCommands );
        }

        if ( status.Ok() )
        {
            status = MergeQueue( diagnostics, result.commands.diagnostics, source.diagnostics,
                                 [&diagnostics]( OperatorEditorDiagnosticsCommandQueue& queue,
                                                 const OperatorEditorDiagnosticsCommand& command, bool* duplicate )
                                 { return SubmitOperatorEditorCommand( diagnostics, queue, command, duplicate ); },
                                 accepted, result.coalescedDuplicateCommands );
        }

        if ( status.Ok() )
        {
            status = MergeQueue( diagnostics, result.commands.replay, source.replay,
                                 [&diagnostics]( OperatorEditorReplayCommandQueue& queue, const OperatorEditorReplayCommand& command,
                                                 bool* duplicate )
                                 { return SubmitOperatorEditorCommand( diagnostics, queue, command, duplicate ); },
                                 accepted, result.coalescedDuplicateCommands );
        }

        if ( status.Ok() )
        {
            status = MergeQueue( diagnostics, result.commands.tools, source.tools,
                                 [&diagnostics]( OperatorEditorToolCommandQueue& queue, const OperatorEditorToolCommand& command,
                                                 bool* duplicate )
                                 { return SubmitOperatorEditorCommand( diagnostics, queue, command, duplicate ); },
                                 accepted, result.coalescedDuplicateCommands );
        }

        return status;
    };

    result.status = mergeAll( legacy, result.acceptedLegacyCommands );

    if ( result.status.Ok() )
    {
        result.status = mergeAll( secondary, result.acceptedSecondaryCommands );
    }

    return result;
}

SkullbonezCore::Core::SbResult ProjectOperatorEditorCommands( SkullbonezCore::Core::SbDiagnosticStore& diagnostics,
                                                              const OperatorEditorCommandQueues& exchange,
                                                              InGameUICommands& commands )
{

    // Invariant: preview commands never escape presentation. Projection emits
    // only committed intent into the established narrow owner packets.
    // Lane R: surfaces are untrusted presentation inputs. Re-run bounded
    // validation before modifying the established owner command packet.
    const OperatorEditorArbitrationResult validated = ArbitrateOperatorEditorCommands( diagnostics, exchange, {} );

    if ( !validated.status.Ok() )
    {
        return validated.status;
    }

    const OperatorEditorCommandQueues& canonical = validated.commands;

    commands.scene.resetScene = false;
    commands.scene.resetSceneDefaults = false;
    commands.scene.requestDemoScene = false;
    commands.scene.saveSceneDefaults = false;
    commands.scene.createScene = false;
    commands.scene.requestedSceneName[0] = '\0';
    commands.scene.requestedSceneIndex = -1;
    commands.sceneOptions.requestedTimeScale = -1.0f;
    commands.sceneOptions.toggleFixedStep = false;
    commands.sceneOptions.requestedModelCount = -1;
    commands.run.requestedSeed = -1;
    commands.run.requestedSolverBallCount = -1;
    commands.run.requestedSolverBoxCount = -1;
    commands.water.requestWorldGravity = false;
    commands.water.requestWorldFluidHeight = false;
    commands.water.requestWorldFluidDensity = false;
    commands.physics.togglePhysicsSleepPolicy = false;
    commands.physics.requestTerrainFrictionCoeff = false;
    commands.physics.requestObjectFrictionCoeff = false;
    commands.physics.requestRollingFrictionCoeff = false;
    commands.physics.toggleTornado = false;
    commands.physics.requestTornadoRadius = false;
    commands.physics.requestTornadoHeight = false;
    commands.physics.requestTornadoInward = false;
    commands.physics.requestTornadoSwirl = false;
    commands.physics.requestTornadoLift = false;
    commands.renderer.toggleVsync = false;
    commands.sceneOptions.toggleShadows = false;
    commands.sceneOptions.toggleTerrainHidden = false;
    commands.sceneOptions.toggleWaterHidden = false;
    commands.sceneOptions.toggleWaterFreeze = false;
    commands.sceneOptions.toggleWaterFlat = false;
    commands.water.toggleWaterReflection = false;
    commands.renderTuning.toggleShadows = false;
    commands.renderTuning.saveDefaults = false;
    commands.renderTuning.requestedParam = UIRenderParam::None;
    commands.cinematic.toggleRendering = false;
    commands.cinematic.saveSkyDefaults = false;
    commands.cinematic.requestedFeature = UICinematicFeature::None;
    commands.cinematic.requestedParam = UICinematicParam::None;
    commands.physics.toggleCollisionVisualizer = false;
    commands.physics.togglePhysicsDebugTransparent = false;
    commands.physics.toggleBroadphaseOverlay = false;
    commands.physics.toggleTerrainContactProbe = false;
    commands.physics.toggleTornadoVisualShell = false;
    commands.physics.toggleTornadoFieldVectors = false;
    commands.physics.toggleRayCastVisualization = false;
    commands.physics.physicsDebugOverlayToToggle = UIPhysicsDebugOverlay::None;
    commands.physics.stepPhysicsPipelinePrevious = false;
    commands.physics.stepPhysicsPipelineNext = false;
    commands.physics.requestedPhysicsDebugAlpha = -1.0f;
    commands.physics.requestedPhysicsDebugContactLinger = -1.0f;
    commands.physics.requestRayCastImpulseStrength = false;
    commands.physics.requestLauncherProjectileSpeed = false;
    commands.profiler.requestedWorkerThreads = -2;
    commands.replayMemory.requestPolicy = false;
    commands.editor.toggleEditorMode = false;
    commands.editor.togglePlacementMode = false;
    commands.editor.requestUndo = false;
    commands.editor.requestRedo = false;
    commands.editor.requestedObjectType = -1;
    commands.editor.enterPlacementMode = false;
    commands.editor.requestPlaceStatic = false;
    commands.editor.requestSelectSceneObject = false;
    commands.editor.requestDeleteSelection = false;
    commands.editor.requestDuplicateSelection = false;
    commands.editor.requestSetEntityVisible = false;
    commands.editor.requestSetEntityLocked = false;
    commands.scene.toggleCrossScenePause = false;
    commands.scene.requestSingleStep = false;

    for ( uint32_t index = 0u; index < canonical.scene.count; ++index )
    {
        const OperatorEditorSceneCommand& command = canonical.scene.commands[index];

        if ( command.type == OperatorEditorSceneCommandType::ResetCurrentScene )
        {
            commands.scene.resetScene = true;
        }
        else if ( command.type == OperatorEditorSceneCommandType::ResetSceneDefaults )
        {
            commands.scene.resetSceneDefaults = true;
        }
        else if ( command.type == OperatorEditorSceneCommandType::RequestDemoScene )
        {
            commands.scene.requestDemoScene = true;
        }
        else if ( command.type == OperatorEditorSceneCommandType::SetCurrentSceneIndex )
        {
            commands.scene.requestedSceneIndex = command.sceneIndex;
        }
        else if ( command.type == OperatorEditorSceneCommandType::SaveCurrentScene )
        {
            commands.scene.saveSceneDefaults = true;
        }
        else if ( command.type == OperatorEditorSceneCommandType::CreateScene )
        {
            commands.scene.createScene = true;
            strncpy_s( commands.scene.requestedSceneName, command.sceneName, _TRUNCATE );
        }
    }

    for ( uint32_t index = 0u; index < canonical.property.count; ++index )
    {
        const OperatorEditorPropertyCommand& command = canonical.property.commands[index];

        // Invariant: preview values are presentation-local. Only the single
        // release/enter commit reaches mutation, replay, reset, or history owners.

        if ( command.phase == OperatorEditorEditPhase::Preview )
        {
            continue;
        }

        switch ( command.type )
        {
        case OperatorEditorPropertyCommandType::SetTimeScale:
            commands.sceneOptions.requestedTimeScale = command.value;
            break;
        case OperatorEditorPropertyCommandType::ToggleFixedStep:
            commands.sceneOptions.toggleFixedStep = true;
            break;
        case OperatorEditorPropertyCommandType::SetModelCount:
            commands.sceneOptions.requestedModelCount = command.integerValue;
            break;
        case OperatorEditorPropertyCommandType::SetSeed:
            commands.run.requestedSeed = command.integerValue;
            break;
        case OperatorEditorPropertyCommandType::SetSolverBallCount:
            commands.run.requestedSolverBallCount = command.integerValue;
            break;
        case OperatorEditorPropertyCommandType::SetSolverBoxCount:
            commands.run.requestedSolverBoxCount = command.integerValue;
            break;
        case OperatorEditorPropertyCommandType::SetWorldGravity:
            commands.water.requestWorldGravity = true;
            commands.water.requestedWorldGravity = command.value;
            break;
        case OperatorEditorPropertyCommandType::SetWorldFluidHeight:
            commands.water.requestWorldFluidHeight = true;
            commands.water.requestedWorldFluidHeight = command.value;
            break;
        case OperatorEditorPropertyCommandType::SetWorldFluidDensity:
            commands.water.requestWorldFluidDensity = true;
            commands.water.requestedWorldFluidDensity = command.value;
            break;
        case OperatorEditorPropertyCommandType::TogglePhysicsSleepPolicy:
            commands.physics.togglePhysicsSleepPolicy = true;
            break;
        case OperatorEditorPropertyCommandType::SetTerrainFriction:
            commands.physics.requestTerrainFrictionCoeff = true;
            commands.physics.requestedTerrainFrictionCoeff = command.value;
            break;
        case OperatorEditorPropertyCommandType::SetObjectFriction:
            commands.physics.requestObjectFrictionCoeff = true;
            commands.physics.requestedObjectFrictionCoeff = command.value;
            break;
        case OperatorEditorPropertyCommandType::SetRollingFriction:
            commands.physics.requestRollingFrictionCoeff = true;
            commands.physics.requestedRollingFrictionCoeff = command.value;
            break;
        case OperatorEditorPropertyCommandType::ToggleTornado:
            commands.physics.toggleTornado = true;
            break;
        case OperatorEditorPropertyCommandType::SetTornadoRadius:
            commands.physics.requestTornadoRadius = true;
            commands.physics.requestedTornadoRadius = command.value;
            break;
        case OperatorEditorPropertyCommandType::SetTornadoHeight:
            commands.physics.requestTornadoHeight = true;
            commands.physics.requestedTornadoHeight = command.value;
            break;
        case OperatorEditorPropertyCommandType::SetTornadoInward:
            commands.physics.requestTornadoInward = true;
            commands.physics.requestedTornadoInward = command.value;
            break;
        case OperatorEditorPropertyCommandType::SetTornadoSwirl:
            commands.physics.requestTornadoSwirl = true;
            commands.physics.requestedTornadoSwirl = command.value;
            break;
        case OperatorEditorPropertyCommandType::SetTornadoLift:
            commands.physics.requestTornadoLift = true;
            commands.physics.requestedTornadoLift = command.value;
            break;
        default:
            break;
        }
    }

    for ( uint32_t index = 0u; index < canonical.rendering.count; ++index )
    {
        const OperatorEditorRenderingCommand& command = canonical.rendering.commands[index];

        if ( command.phase == OperatorEditorEditPhase::Preview )
        {
            continue;
        }

        switch ( command.type )
        {
        case OperatorEditorRenderingCommandType::ToggleVsync:
            commands.renderer.toggleVsync = true;
            break;
        case OperatorEditorRenderingCommandType::ToggleShadows:
            commands.renderTuning.toggleShadows = true;
            break;
        case OperatorEditorRenderingCommandType::ToggleTerrainHidden:
            commands.sceneOptions.toggleTerrainHidden = true;
            break;
        case OperatorEditorRenderingCommandType::ToggleWaterHidden:
            commands.sceneOptions.toggleWaterHidden = true;
            break;
        case OperatorEditorRenderingCommandType::ToggleWaterFreeze:
            commands.sceneOptions.toggleWaterFreeze = true;
            break;
        case OperatorEditorRenderingCommandType::ToggleWaterFlat:
            commands.sceneOptions.toggleWaterFlat = true;
            break;
        case OperatorEditorRenderingCommandType::CycleWaterReflection:
            commands.water.toggleWaterReflection = true;
            break;
        case OperatorEditorRenderingCommandType::ToggleCinematicRendering:
            commands.cinematic.toggleRendering = true;
            break;
        case OperatorEditorRenderingCommandType::ToggleCinematicFeature:
            commands.cinematic.requestedFeature = static_cast<UICinematicFeature>( command.parameter );
            break;
        case OperatorEditorRenderingCommandType::SetOrdinaryParameter:
            commands.renderTuning.requestedParam = static_cast<UIRenderParam>( command.parameter );
            commands.renderTuning.requestedValue = command.value;
            break;
        case OperatorEditorRenderingCommandType::SetCinematicParameter:
            commands.cinematic.requestedParam = static_cast<UICinematicParam>( command.parameter );
            commands.cinematic.requestedValue = command.value;
            break;
        case OperatorEditorRenderingCommandType::SaveOrdinaryDefaults:
            commands.renderTuning.saveDefaults = true;
            break;
        case OperatorEditorRenderingCommandType::SaveSkyDefaults:
            commands.cinematic.saveSkyDefaults = true;
            break;
        default:
            break;
        }
    }

    for ( uint32_t index = 0u; index < canonical.diagnostics.count; ++index )
    {
        const OperatorEditorDiagnosticsCommand& command = canonical.diagnostics.commands[index];

        if ( command.phase == OperatorEditorEditPhase::Preview )
        {
            continue;
        }

        switch ( command.type )
        {
        case OperatorEditorDiagnosticsCommandType::ToggleCollisionVisualizer:
            commands.physics.toggleCollisionVisualizer = true;
            break;
        case OperatorEditorDiagnosticsCommandType::TogglePhysicsDebugTransparent:
            commands.physics.togglePhysicsDebugTransparent = true;
            break;
        case OperatorEditorDiagnosticsCommandType::ToggleBroadphaseOverlay:
            commands.physics.toggleBroadphaseOverlay = true;
            break;
        case OperatorEditorDiagnosticsCommandType::ToggleTerrainContactProbe:
            commands.physics.toggleTerrainContactProbe = true;
            break;
        case OperatorEditorDiagnosticsCommandType::ToggleTornadoVisualShell:
            commands.physics.toggleTornadoVisualShell = true;
            break;
        case OperatorEditorDiagnosticsCommandType::ToggleTornadoFieldVectors:
            commands.physics.toggleTornadoFieldVectors = true;
            break;
        case OperatorEditorDiagnosticsCommandType::ToggleRayCastVisualization:
            commands.physics.toggleRayCastVisualization = true;
            break;
        case OperatorEditorDiagnosticsCommandType::TogglePhysicsDebugFlag:
            commands.physics.physicsDebugOverlayToToggle = static_cast<UIPhysicsDebugOverlay>( command.flag );
            break;
        case OperatorEditorDiagnosticsCommandType::StepPhysicsPipelinePrevious:
            commands.physics.stepPhysicsPipelinePrevious = true;
            break;
        case OperatorEditorDiagnosticsCommandType::StepPhysicsPipelineNext:
            commands.physics.stepPhysicsPipelineNext = true;
            break;
        case OperatorEditorDiagnosticsCommandType::SetPhysicsDebugAlpha:
            commands.physics.requestedPhysicsDebugAlpha = command.value;
            break;
        case OperatorEditorDiagnosticsCommandType::SetPhysicsContactLinger:
            commands.physics.requestedPhysicsDebugContactLinger = command.value;
            break;
        case OperatorEditorDiagnosticsCommandType::SetRayCastImpulseStrength:
            commands.physics.requestRayCastImpulseStrength = true;
            commands.physics.requestedRayCastImpulseStrength = command.value;
            break;
        case OperatorEditorDiagnosticsCommandType::SetLauncherProjectileSpeed:
            commands.physics.requestLauncherProjectileSpeed = true;
            commands.physics.requestedLauncherProjectileSpeed = command.value;
            break;
        case OperatorEditorDiagnosticsCommandType::SetWorkerThreads:
            commands.profiler.requestedWorkerThreads = command.integerValue;
            break;
        default:
            break;
        }
    }

    for ( uint32_t index = 0u; index < canonical.replay.count; ++index )
    {
        const OperatorEditorReplayCommand& command = canonical.replay.commands[index];

        if ( command.type == OperatorEditorReplayCommandType::SetMemoryPolicy )
        {
            commands.replayMemory.requestPolicy = true;
            commands.replayMemory.requestedPresetIndex = command.presetIndex;
            commands.replayMemory.requestedRetentionSeconds = command.retentionSeconds;
            commands.replayMemory.requestedBudgetMiB = command.budgetMiB;
        }
    }

    for ( uint32_t index = 0u; index < canonical.tools.count; ++index )
    {
        const OperatorEditorToolCommand& command = canonical.tools.commands[index];

        switch ( command.type )
        {
        case OperatorEditorToolCommandType::ToggleEditorMode:
            commands.editor.toggleEditorMode = true;
            break;
        case OperatorEditorToolCommandType::TogglePlacementMode:
            commands.editor.togglePlacementMode = true;
            break;
        case OperatorEditorToolCommandType::Undo:
            commands.editor.requestUndo = true;
            break;
        case OperatorEditorToolCommandType::Redo:
            commands.editor.requestRedo = true;
            break;
        case OperatorEditorToolCommandType::ToggleCrossScenePause:
            commands.scene.toggleCrossScenePause = true;
            break;
        case OperatorEditorToolCommandType::StepPausedScene:
            commands.scene.requestSingleStep = true;
            break;
        case OperatorEditorToolCommandType::SelectSceneObject:
            commands.editor.requestSelectSceneObject = true;
            commands.editor.requestedSceneObjectId = command.sceneObjectId;
            break;
        case OperatorEditorToolCommandType::DeleteSelection:
            commands.editor.requestDeleteSelection = true;
            break;
        case OperatorEditorToolCommandType::DuplicateSelection:
            commands.editor.requestDuplicateSelection = true;
            break;
        case OperatorEditorToolCommandType::SetPlacementObjectType:
            commands.editor.requestedObjectType = command.value;
            commands.editor.enterPlacementMode = true;
            break;
        case OperatorEditorToolCommandType::SetPlaceStatic:
            commands.editor.requestPlaceStatic = true;
            commands.editor.requestedPlaceStatic = command.enabled;
            break;
        case OperatorEditorToolCommandType::ToggleTerrainAlign:
            commands.editor.toggleTerrainAlign = true;
            break;
        case OperatorEditorToolCommandType::SetEntityVisible:
            commands.editor.requestSetEntityVisible = true;
            commands.editor.visibilitySceneObjectId = command.sceneObjectId;
            commands.editor.requestedEntityVisible = command.enabled;
            break;
        case OperatorEditorToolCommandType::SetEntityLocked:
            commands.editor.requestSetEntityLocked = true;
            commands.editor.lockSceneObjectId = command.sceneObjectId;
            commands.editor.requestedEntityLocked = command.enabled;
            break;
        default:
            break;
        }
    }

    return SkullbonezCore::Core::SbResult::Success();
}

uint64_t FingerprintOperatorEditorFrameView( const OperatorEditorFrameView& view ) noexcept
{
    uint64_t hash = 1469598103934665603ull;
    const char* sceneName = view.scene.sceneName ? view.scene.sceneName : "";
    HashBytes( hash, sceneName, std::strlen( sceneName ) );
    HashValue( hash, view.scene.currentSceneIndex );
    HashValue( hash, view.scene.sceneCount );
    HashValue( hash, view.scene.currentFrame );
    HashValue( hash, view.scene.modelCount );
    HashValue( hash, view.scene.timeScale );
    HashValue( hash, view.scene.canSaveCurrentScene );
    HashValue( hash, view.scene.dirty );

    for ( int index = 0; index < view.scene.sceneCount && view.scene.sceneOptions; ++index )
    {
        const char* option = view.scene.sceneOptions[index] ? view.scene.sceneOptions[index] : "";
        HashBytes( hash, option, std::strlen( option ) );
    }

    // Invariant: hash semantic fields individually. Object padding is not
    // initialized by the language and therefore cannot be deterministic input.
    HashValue( hash, view.property.worldGravity );
    HashValue( hash, view.property.worldFluidHeight );
    HashValue( hash, view.property.worldFluidDensity );
    HashValue( hash, view.rendering.vsyncEnabled );
    HashValue( hash, view.rendering.shadowsEnabled );
    HashValue( hash, view.rendering.cinematicRendering );
    HashValue( hash, view.rendering.presentationInterpolation );
    HashValue( hash, view.rendering.presentationAlpha );
    HashValue( hash, view.rendering.terrainHidden );
    HashValue( hash, view.rendering.waterHidden );
    HashValue( hash, view.rendering.waterFrozen );
    HashValue( hash, view.rendering.waterFlat );
    HashValue( hash, view.rendering.waterReflectionMode );

    for ( float value : view.rendering.ordinaryParameters )
    {
        HashValue( hash, value );
    }

    for ( float value : view.rendering.cinematicParameters )
    {
        HashValue( hash, value );
    }

    for ( bool value : view.rendering.cinematicFeatures )
    {
        HashValue( hash, value );
    }

    const char* cameraModeLabel = view.viewport.cameraModeLabel ? view.viewport.cameraModeLabel : "";
    const char* gizmoModeLabel = view.viewport.gizmoModeLabel ? view.viewport.gizmoModeLabel : "";
    HashBytes( hash, cameraModeLabel, std::strlen( cameraModeLabel ) );
    HashBytes( hash, gizmoModeLabel, std::strlen( gizmoModeLabel ) );
    HashValue( hash, view.viewport.presentationPinned );
    HashValue( hash, view.replay.memoryPreset );
    HashValue( hash, view.replay.requestedRetentionSeconds );
    HashValue( hash, view.replay.requestedBudgetMiB );
    HashValue( hash, view.replay.presentationRetentionSeconds );
    HashValue( hash, view.replay.solverRetentionSeconds );
    HashValue( hash, view.replay.memoryBudgetClamped );
    HashValue( hash, view.replay.solverWindowReduced );
    HashValue( hash, view.surfaces.legacyVisible );
    HashValue( hash, view.surfaces.secondaryVisible );
    HashValue( hash, view.tools.editorModeEnabled );
    HashValue( hash, view.tools.placementModeEnabled );
    HashValue( hash, view.tools.placeStaticObject );
    HashValue( hash, view.tools.crossScenePauseLocked );
    HashValue( hash, view.tools.fixedStep );
    HashValue( hash, view.tools.autoTerrainAlign );
    HashValue( hash, view.tools.undoDepth );
    HashValue( hash, view.tools.redoDepth );
    HashValue( hash, view.lookLab.seed );
    HashValue( hash, view.lookLab.hasCandidate );
    HashValue( hash, view.lookLab.savePending );
    HashBytes( hash, view.lookLab.detail.data(), std::strlen( view.lookLab.detail.data() ) );
    HashBytes( hash, view.lookLab.bundleDirectory.data(), std::strlen( view.lookLab.bundleDirectory.data() ) );
    const uint32_t hierarchyCount = view.hierarchy.rowCount <= OPERATOR_EDITOR_HIERARCHY_ROW_CAPACITY
                                        ? view.hierarchy.rowCount
                                        : OPERATOR_EDITOR_HIERARCHY_ROW_CAPACITY;

    HashValue( hash, hierarchyCount );
    HashValue( hash, view.hierarchy.totalRowCount );
    HashValue( hash, view.hierarchy.selectedSceneObjectId );
    HashValue( hash, view.hierarchy.truncated );

    for ( uint32_t index = 0u; index < hierarchyCount; ++index )
    {
        const OperatorEditorHierarchyRow& row = view.hierarchy.rows[index];
        const char* label = row.displayName ? row.displayName : "";
        HashBytes( hash, label, std::strlen( label ) );
        HashValue( hash, row.sceneObjectId );
        HashValue( hash, row.groupRootObjectId );
        HashValue( hash, row.groupPartIndex );
        HashValue( hash, row.assetBacked );
        HashValue( hash, row.visible );
        HashValue( hash, row.locked );
        HashValue( hash, row.selected );
    }

    HashValue( hash, view.assets.selectedObjectType );
    HashValue( hash, view.assets.objectTypeCount );
    HashValue( hash, view.assets.registeredLibraryAvailable );
    const auto hashLabel = [&]( const char* label )
    {
        const char* text = label ? label : "";

        HashBytes( hash, text, std::strlen( text ) );
    };

    hashLabel( view.inspector.displayName );
    hashLabel( view.inspector.renderMaterialName );
    hashLabel( view.inspector.contactMaterialName );
    hashLabel( view.inspector.assetName );
    hashLabel( view.inspector.assetInstanceName );
    hashLabel( view.inspector.assetPartName );
    HashValue( hash, view.inspector.selectionState );
    HashValue( hash, view.inspector.sceneObjectId );
    HashValue( hash, view.inspector.selectionCount );
    HashValue( hash, view.inspector.renderMaterialKind );
    HashValue( hash, view.inspector.colliderShapeKind );
    HashValue( hash, view.inspector.behaviorGroupKind );
    HashValue( hash, view.inspector.behaviorPartIndex );

    for ( float value : view.inspector.position )
    {
        HashValue( hash, value );
    }

    for ( float value : view.inspector.orientation )
    {
        HashValue( hash, value );
    }

    for ( float value : view.inspector.linearVelocity )
    {
        HashValue( hash, value );
    }

    for ( float value : view.inspector.angularVelocity )
    {
        HashValue( hash, value );
    }

    for ( float value : view.inspector.baseColor )
    {
        HashValue( hash, value );
    }

    HashValue( hash, view.inspector.mass );
    HashValue( hash, view.inspector.volume );
    HashValue( hash, view.inspector.boundingRadius );
    HashValue( hash, view.inspector.dragCoefficient );
    HashValue( hash, view.inspector.friction );
    HashValue( hash, view.inspector.restitution );
    HashValue( hash, view.inspector.roughness );
    HashValue( hash, view.inspector.metallic );
    HashValue( hash, view.inspector.specular );
    HashValue( hash, view.inspector.visible );
    HashValue( hash, view.inspector.locked );
    HashValue( hash, view.inspector.fixed );
    HashValue( hash, view.inspector.sleeping );
    HashValue( hash, view.inspector.assetBacked );
    HashValue( hash, view.world.modelCount );
    HashValue( hash, view.world.modelCapacity );
    HashValue( hash, view.world.solverBallCount );
    HashValue( hash, view.world.solverBoxCount );
    HashValue( hash, view.world.rngSeed );
    HashValue( hash, view.world.timeScale );
    HashValue( hash, view.world.gravity );
    HashValue( hash, view.world.fluidHeight );
    HashValue( hash, view.world.fluidDensity );
    HashValue( hash, view.world.terrainFriction );
    HashValue( hash, view.world.objectFriction );
    HashValue( hash, view.world.rollingFriction );
    HashValue( hash, view.world.tornadoRadius );
    HashValue( hash, view.world.tornadoHeight );
    HashValue( hash, view.world.tornadoInward );
    HashValue( hash, view.world.tornadoSwirl );
    HashValue( hash, view.world.tornadoLift );
    HashValue( hash, view.world.fixedStep );
    HashValue( hash, view.world.physicsSleepEnabled );
    HashValue( hash, view.world.tornadoEnabled );
    hashLabel( view.diagnostics.rendererName );
    hashLabel( view.diagnostics.physicsPipelineStageName );
    HashValue( hash, view.diagnostics.renderTargetCount );
    HashValue( hash, view.diagnostics.drawCalls );
    HashValue( hash, view.diagnostics.uiDrawCalls );
    HashValue( hash, view.diagnostics.workerThreadCount );
    HashValue( hash, view.diagnostics.physicsDebugFlags );
    HashValue( hash, view.diagnostics.trackedEngineBytes );
    HashValue( hash, view.diagnostics.uploadUsedBytes );
    HashValue( hash, view.diagnostics.replayReserveGrowthEvents );
    HashValue( hash, view.diagnostics.collisionVisualizer );
    HashValue( hash, view.diagnostics.physicsDebugTransparent );
    HashValue( hash, view.diagnostics.broadphaseOverlay );
    const int renderTargetCount = view.diagnostics.renderTargetCount < OPERATOR_EDITOR_RENDER_TARGET_CAPACITY
                                      ? view.diagnostics.renderTargetCount
                                      : OPERATOR_EDITOR_RENDER_TARGET_CAPACITY;

    for ( int targetIndex = 0; targetIndex < renderTargetCount; ++targetIndex )
    {
        const OperatorEditorRenderTargetView& target = view.diagnostics.renderTargets[targetIndex];
        hashLabel( target.label );
        HashValue( hash, target.width );
        HashValue( hash, target.height );
        HashValue( hash, target.available );
        HashValue( hash, target.depth );
        HashValue( hash, target.hdr );
    }

    return hash;
}
} // namespace SkullbonezCore::UI
