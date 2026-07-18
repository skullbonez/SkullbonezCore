/*
File: SkullbonezSource/UI/OperatorEditorExchange.cpp
Purpose:
  Implements bounded operator-editor command validation and arbitration.

Summary:
  Legacy one-frame fields are normalized into five domain queues. The merge
  order is fixed, exact cross-surface duplicates collapse to one request, and a
  same-action/different-payload conflict reports a recoverable result before any
  runtime owner sees ambiguous intent.

Glossary:
  Action identity: The domain enum value that names one owner-side operation.
  Projection: Conversion from the common queue back into established narrow UI
    command structs consumed by concrete runtime owners.
  FNV-1a: Small deterministic hash used only to prove both surfaces consumed the
    same frame values; it is not durable identity or serialization.

Invariants:
  - Validation completes before a command consumes queue capacity.
  - A queue contains at most one payload for each action identity.
  - Arbitration never partially projects a failed merge into owner commands.
  - Float payloads must be finite before they enter a queue.

Related:
  - SkullbonezSource/UI/OperatorEditorExchange.h
  - SkullbonezSource/UI/UICommands.h
  - SkullbonezTests/TestOwnerRequestQueues.cpp
*/
#include "OperatorEditorExchange.h"

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
SkullbonezCore::Core::SbResult SubmitBounded( OperatorEditorCommandQueue<Command, Capacity>& queue,
                                              const Command& command,
                                              SameIdentity sameIdentity,
                                              SamePayload samePayload,
                                              bool* duplicate )
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
            return SkullbonezCore::Core::SbResult::Failure(
                OWNER,
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
        return SkullbonezCore::Core::SbResult::Failure( OWNER,
                                                        "Operator-editor command queue exhausted its fixed capacity" );
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

bool SameRenderingIdentity( const OperatorEditorRenderingCommand& left, const OperatorEditorRenderingCommand& right )
{
    return left.type == right.type;
}

bool SameRenderingPayload( const OperatorEditorRenderingCommand&, const OperatorEditorRenderingCommand& )
{
    return true;
}

bool SameReplayIdentity( const OperatorEditorReplayCommand& left, const OperatorEditorReplayCommand& right )
{
    return left.type == right.type;
}

bool SameReplayPayload( const OperatorEditorReplayCommand& left, const OperatorEditorReplayCommand& right )
{
    return left.presetIndex == right.presetIndex && left.retentionSeconds == right.retentionSeconds &&
           left.budgetMiB == right.budgetMiB;
}

bool SameToolIdentity( const OperatorEditorToolCommand& left, const OperatorEditorToolCommand& right )
{
    if ( left.type != right.type )
    {
        return false;
    }
    // Entity flag actions are independent per durable scene object. Selection
    // remains one action identity so two front ends cannot select two objects
    // in the same turn without producing a Lane-R conflict.
    return ( left.type != OperatorEditorToolCommandType::SetEntityVisible &&
             left.type != OperatorEditorToolCommandType::SetEntityLocked ) ||
           left.sceneObjectId == right.sceneObjectId;
}

bool SameToolPayload( const OperatorEditorToolCommand& left, const OperatorEditorToolCommand& right )
{
    return left.sceneObjectId == right.sceneObjectId && left.value == right.value && left.enabled == right.enabled;
}

template <typename Queue, typename Submit>
SkullbonezCore::Core::SbResult
MergeQueue( Queue& target, const Queue& source, Submit submit, uint32_t& accepted, uint32_t& duplicates )
{
    if ( source.count > Queue::capacity )
    {
        return SkullbonezCore::Core::SbResult::Failure( OWNER,
                                                        "Operator-editor command count exceeded queue capacity" );
    }
    for ( uint32_t index = 0u; index < source.count; ++index )
    {
        bool duplicate = false;
        const SkullbonezCore::Core::SbResult result = submit( target, source.commands[index], &duplicate );
        if ( !result.ok )
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

SkullbonezCore::Core::SbResult SubmitOperatorEditorCommand( OperatorEditorSceneCommandQueue& queue,
                                                            const OperatorEditorSceneCommand& command,
                                                            bool* duplicate )
{
    if ( command.type != OperatorEditorSceneCommandType::ResetCurrentScene &&
         command.type != OperatorEditorSceneCommandType::ResetSceneDefaults &&
         command.type != OperatorEditorSceneCommandType::RequestDemoScene &&
         command.type != OperatorEditorSceneCommandType::SetCurrentSceneIndex &&
         command.type != OperatorEditorSceneCommandType::SaveCurrentScene &&
         command.type != OperatorEditorSceneCommandType::CreateScene )
    {
        return SkullbonezCore::Core::SbResult::Failure( OWNER, "Scene command has an unknown action type" );
    }
    if ( command.type == OperatorEditorSceneCommandType::SetCurrentSceneIndex && command.sceneIndex < 0 )
    {
        return SkullbonezCore::Core::SbResult::Failure( OWNER, "Scene index command requires a non-negative index" );
    }
    if ( command.type == OperatorEditorSceneCommandType::CreateScene &&
         ( command.sceneName[0] == '\0' ||
           std::memchr( command.sceneName, '\0', sizeof( command.sceneName ) ) == nullptr ) )
    {
        return SkullbonezCore::Core::SbResult::Failure( OWNER,
                                                        "Create-scene command requires a bounded non-empty name" );
    }
    return SubmitBounded( queue, command, SameSceneIdentity, SameScenePayload, duplicate );
}

SkullbonezCore::Core::SbResult SubmitOperatorEditorCommand( OperatorEditorPropertyCommandQueue& queue,
                                                            const OperatorEditorPropertyCommand& command,
                                                            bool* duplicate )
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
        return SkullbonezCore::Core::SbResult::Failure( OWNER, "Property command has an unknown action type" );
    }
    if ( command.phase != OperatorEditorEditPhase::Preview && command.phase != OperatorEditorEditPhase::Commit )
    {
        return SkullbonezCore::Core::SbResult::Failure( OWNER, "Property command has an unknown edit phase" );
    }
    if ( !std::isfinite( command.value ) )
    {
        return SkullbonezCore::Core::SbResult::Failure( OWNER, "Property command requires a finite value" );
    }
    if ( command.type == OperatorEditorPropertyCommandType::SetTimeScale && command.value <= 0.0f )
    {
        return SkullbonezCore::Core::SbResult::Failure( OWNER, "Time-scale command requires a positive value" );
    }
    if ( command.type == OperatorEditorPropertyCommandType::SetSeed && command.integerValue <= 0 )
    {
        return SkullbonezCore::Core::SbResult::Failure( OWNER, "Seed command requires a positive integer" );
    }
    if ( ( command.type == OperatorEditorPropertyCommandType::SetModelCount ||
           command.type == OperatorEditorPropertyCommandType::SetSolverBallCount ||
           command.type == OperatorEditorPropertyCommandType::SetSolverBoxCount ) &&
         command.integerValue < 0 )
    {
        return SkullbonezCore::Core::SbResult::Failure( OWNER, "Population command requires a non-negative integer" );
    }
    return SubmitBounded( queue, command, SamePropertyIdentity, SamePropertyPayload, duplicate );
}

SkullbonezCore::Core::SbResult SubmitOperatorEditorCommand( OperatorEditorRenderingCommandQueue& queue,
                                                            const OperatorEditorRenderingCommand& command,
                                                            bool* duplicate )
{
    if ( command.type != OperatorEditorRenderingCommandType::ToggleVsync )
    {
        return SkullbonezCore::Core::SbResult::Failure( OWNER, "Rendering command has an unknown action type" );
    }
    return SubmitBounded( queue, command, SameRenderingIdentity, SameRenderingPayload, duplicate );
}

SkullbonezCore::Core::SbResult SubmitOperatorEditorCommand( OperatorEditorReplayCommandQueue& queue,
                                                            const OperatorEditorReplayCommand& command,
                                                            bool* duplicate )
{
    if ( command.type != OperatorEditorReplayCommandType::SetMemoryPolicy )
    {
        return SkullbonezCore::Core::SbResult::Failure( OWNER, "Replay command has an unknown action type" );
    }
    const bool presetValid = command.presetIndex >= -1;
    const bool retentionValid = command.retentionSeconds == -1 || command.retentionSeconds > 0;
    const bool budgetValid = command.budgetMiB == -1 || command.budgetMiB > 0;
    const bool requestsValue = command.presetIndex >= 0 || command.retentionSeconds > 0 || command.budgetMiB > 0;
    if ( !presetValid || !retentionValid || !budgetValid || !requestsValue )
    {
        return SkullbonezCore::Core::SbResult::Failure(
            OWNER,
            "Replay memory command requires at least one valid preset, retention, or budget value" );
    }
    return SubmitBounded( queue, command, SameReplayIdentity, SameReplayPayload, duplicate );
}

SkullbonezCore::Core::SbResult SubmitOperatorEditorCommand( OperatorEditorToolCommandQueue& queue,
                                                            const OperatorEditorToolCommand& command,
                                                            bool* duplicate )
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
        return SubmitBounded( queue, command, SameToolIdentity, SameToolPayload, duplicate );
    case OperatorEditorToolCommandType::SelectSceneObject:
    case OperatorEditorToolCommandType::SetEntityVisible:
    case OperatorEditorToolCommandType::SetEntityLocked:
        if ( command.sceneObjectId == 0u )
        {
            return SkullbonezCore::Core::SbResult::Failure( OWNER,
                                                            "Hierarchy command requires a stable scene object id" );
        }
        return SubmitBounded( queue, command, SameToolIdentity, SameToolPayload, duplicate );
    case OperatorEditorToolCommandType::SetPlacementObjectType:
        if ( command.value < 0 || command.value >= EditorTab::OBJECT_TYPE_COUNT )
        {
            return SkullbonezCore::Core::SbResult::Failure( OWNER, "Placement command has an invalid object type" );
        }
        return SubmitBounded( queue, command, SameToolIdentity, SameToolPayload, duplicate );
    case OperatorEditorToolCommandType::SetPlaceStatic:
    case OperatorEditorToolCommandType::ToggleTerrainAlign:
        return SubmitBounded( queue, command, SameToolIdentity, SameToolPayload, duplicate );
    default:
        return SkullbonezCore::Core::SbResult::Failure( OWNER, "Tool command has an unknown action type" );
    }
}

SkullbonezCore::Core::SbResult NormalizeLegacyOperatorEditorCommands( InGameUICommands& commands )
{
    OperatorEditorCommandQueues normalized = commands.operatorEditor;
    SkullbonezCore::Core::SbResult result = SkullbonezCore::Core::SbResult::Success();
    if ( commands.scene.resetScene )
    {
        result = SubmitOperatorEditorCommand(
            normalized.scene,
            OperatorEditorSceneCommand{ OperatorEditorSceneCommandType::ResetCurrentScene, -1 } );
    }
    if ( result.ok && commands.scene.resetSceneDefaults )
    {
        result = SubmitOperatorEditorCommand(
            normalized.scene,
            OperatorEditorSceneCommand{ OperatorEditorSceneCommandType::ResetSceneDefaults, -1 } );
    }
    if ( result.ok && commands.scene.requestDemoScene )
    {
        result = SubmitOperatorEditorCommand(
            normalized.scene,
            OperatorEditorSceneCommand{ OperatorEditorSceneCommandType::RequestDemoScene, -1 } );
    }
    if ( result.ok && commands.scene.saveSceneDefaults )
    {
        result = SubmitOperatorEditorCommand(
            normalized.scene,
            OperatorEditorSceneCommand{ OperatorEditorSceneCommandType::SaveCurrentScene, -1 } );
    }
    if ( result.ok && commands.scene.createScene )
    {
        OperatorEditorSceneCommand create;
        create.type = OperatorEditorSceneCommandType::CreateScene;
        strncpy_s( create.sceneName, commands.scene.requestedSceneName, _TRUNCATE );
        result = SubmitOperatorEditorCommand( normalized.scene, create );
    }
    if ( result.ok && commands.scene.requestedSceneIndex >= 0 )
    {
        result = SubmitOperatorEditorCommand(
            normalized.scene,
            OperatorEditorSceneCommand{ OperatorEditorSceneCommandType::SetCurrentSceneIndex,
                                        commands.scene.requestedSceneIndex } );
    }
    const auto normalizeProperty =
        [&]( bool requested, OperatorEditorPropertyCommandType type, float value = 0.0f, int integerValue = 0 )
    {
        if ( result.ok && requested )
        {
            result = SubmitOperatorEditorCommand(
                normalized.property,
                OperatorEditorPropertyCommand{ type, value, integerValue, OperatorEditorEditPhase::Commit } );
        }
    };
    normalizeProperty( commands.sceneOptions.requestedTimeScale > 0.0f,
                       OperatorEditorPropertyCommandType::SetTimeScale,
                       commands.sceneOptions.requestedTimeScale );
    normalizeProperty( commands.sceneOptions.toggleFixedStep, OperatorEditorPropertyCommandType::ToggleFixedStep );
    normalizeProperty( commands.sceneOptions.requestedModelCount >= 0,
                       OperatorEditorPropertyCommandType::SetModelCount,
                       0.0f,
                       commands.sceneOptions.requestedModelCount );
    normalizeProperty( commands.run.requestedSeed > 0,
                       OperatorEditorPropertyCommandType::SetSeed,
                       0.0f,
                       commands.run.requestedSeed );
    normalizeProperty( commands.run.requestedSolverBallCount >= 0,
                       OperatorEditorPropertyCommandType::SetSolverBallCount,
                       0.0f,
                       commands.run.requestedSolverBallCount );
    normalizeProperty( commands.run.requestedSolverBoxCount >= 0,
                       OperatorEditorPropertyCommandType::SetSolverBoxCount,
                       0.0f,
                       commands.run.requestedSolverBoxCount );
    normalizeProperty( commands.water.requestWorldGravity,
                       OperatorEditorPropertyCommandType::SetWorldGravity,
                       commands.water.requestedWorldGravity );
    normalizeProperty( commands.water.requestWorldFluidHeight,
                       OperatorEditorPropertyCommandType::SetWorldFluidHeight,
                       commands.water.requestedWorldFluidHeight );
    normalizeProperty( commands.water.requestWorldFluidDensity,
                       OperatorEditorPropertyCommandType::SetWorldFluidDensity,
                       commands.water.requestedWorldFluidDensity );
    normalizeProperty( commands.physics.togglePhysicsSleepPolicy,
                       OperatorEditorPropertyCommandType::TogglePhysicsSleepPolicy );
    normalizeProperty( commands.physics.requestTerrainFrictionCoeff,
                       OperatorEditorPropertyCommandType::SetTerrainFriction,
                       commands.physics.requestedTerrainFrictionCoeff );
    normalizeProperty( commands.physics.requestObjectFrictionCoeff,
                       OperatorEditorPropertyCommandType::SetObjectFriction,
                       commands.physics.requestedObjectFrictionCoeff );
    normalizeProperty( commands.physics.requestRollingFrictionCoeff,
                       OperatorEditorPropertyCommandType::SetRollingFriction,
                       commands.physics.requestedRollingFrictionCoeff );
    normalizeProperty( commands.physics.toggleTornado, OperatorEditorPropertyCommandType::ToggleTornado );
    normalizeProperty( commands.physics.requestTornadoRadius,
                       OperatorEditorPropertyCommandType::SetTornadoRadius,
                       commands.physics.requestedTornadoRadius );
    normalizeProperty( commands.physics.requestTornadoHeight,
                       OperatorEditorPropertyCommandType::SetTornadoHeight,
                       commands.physics.requestedTornadoHeight );
    normalizeProperty( commands.physics.requestTornadoInward,
                       OperatorEditorPropertyCommandType::SetTornadoInward,
                       commands.physics.requestedTornadoInward );
    normalizeProperty( commands.physics.requestTornadoSwirl,
                       OperatorEditorPropertyCommandType::SetTornadoSwirl,
                       commands.physics.requestedTornadoSwirl );
    normalizeProperty( commands.physics.requestTornadoLift,
                       OperatorEditorPropertyCommandType::SetTornadoLift,
                       commands.physics.requestedTornadoLift );
    if ( result.ok && commands.renderer.toggleVsync )
    {
        result = SubmitOperatorEditorCommand(
            normalized.rendering,
            OperatorEditorRenderingCommand{ OperatorEditorRenderingCommandType::ToggleVsync } );
    }
    if ( result.ok && commands.replayMemory.requestPolicy )
    {
        result =
            SubmitOperatorEditorCommand( normalized.replay,
                                         OperatorEditorReplayCommand{ OperatorEditorReplayCommandType::SetMemoryPolicy,
                                                                      commands.replayMemory.requestedPresetIndex,
                                                                      commands.replayMemory.requestedRetentionSeconds,
                                                                      commands.replayMemory.requestedBudgetMiB } );
    }
    const auto normalizeTool = [&]( bool requested, OperatorEditorToolCommandType type )
    {
        if ( result.ok && requested )
        {
            result = SubmitOperatorEditorCommand( normalized.tools, OperatorEditorToolCommand{ type } );
        }
    };
    normalizeTool( commands.editor.toggleEditorMode, OperatorEditorToolCommandType::ToggleEditorMode );
    normalizeTool( commands.editor.togglePlacementMode, OperatorEditorToolCommandType::TogglePlacementMode );
    normalizeTool( commands.editor.requestUndo, OperatorEditorToolCommandType::Undo );
    normalizeTool( commands.editor.requestRedo, OperatorEditorToolCommandType::Redo );
    normalizeTool( commands.scene.toggleCrossScenePause, OperatorEditorToolCommandType::ToggleCrossScenePause );
    normalizeTool( commands.scene.requestSingleStep, OperatorEditorToolCommandType::StepPausedScene );
    if ( result.ok && commands.editor.requestedObjectType >= 0 )
    {
        result = SubmitOperatorEditorCommand(
            normalized.tools,
            OperatorEditorToolCommand{ OperatorEditorToolCommandType::SetPlacementObjectType,
                                       0u,
                                       commands.editor.requestedObjectType } );
    }
    if ( result.ok && commands.editor.requestPlaceStatic )
    {
        result = SubmitOperatorEditorCommand( normalized.tools,
                                              OperatorEditorToolCommand{ OperatorEditorToolCommandType::SetPlaceStatic,
                                                                         0u,
                                                                         0,
                                                                         commands.editor.requestedPlaceStatic } );
    }
    if ( !result.ok )
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

OperatorEditorArbitrationResult ArbitrateOperatorEditorCommands( const OperatorEditorCommandQueues& legacy,
                                                                 const OperatorEditorCommandQueues& secondary )
{
    OperatorEditorArbitrationResult result;
    auto mergeAll = [&]( const OperatorEditorCommandQueues& source, uint32_t& accepted )
    {
        SkullbonezCore::Core::SbResult status = MergeQueue(
            result.commands.scene,
            source.scene,
            []( OperatorEditorSceneCommandQueue& queue, const OperatorEditorSceneCommand& command, bool* duplicate )
            { return SubmitOperatorEditorCommand( queue, command, duplicate ); },
            accepted,
            result.coalescedDuplicateCommands );
        if ( status.ok )
        {
            status = MergeQueue(
                result.commands.property,
                source.property,
                []( OperatorEditorPropertyCommandQueue& queue,
                    const OperatorEditorPropertyCommand& command,
                    bool* duplicate ) { return SubmitOperatorEditorCommand( queue, command, duplicate ); },
                accepted,
                result.coalescedDuplicateCommands );
        }
        if ( status.ok )
        {
            status = MergeQueue(
                result.commands.rendering,
                source.rendering,
                []( OperatorEditorRenderingCommandQueue& queue,
                    const OperatorEditorRenderingCommand& command,
                    bool* duplicate ) { return SubmitOperatorEditorCommand( queue, command, duplicate ); },
                accepted,
                result.coalescedDuplicateCommands );
        }
        if ( status.ok )
        {
            status = MergeQueue(
                result.commands.replay,
                source.replay,
                []( OperatorEditorReplayCommandQueue& queue,
                    const OperatorEditorReplayCommand& command,
                    bool* duplicate ) { return SubmitOperatorEditorCommand( queue, command, duplicate ); },
                accepted,
                result.coalescedDuplicateCommands );
        }
        if ( status.ok )
        {
            status = MergeQueue(
                result.commands.tools,
                source.tools,
                []( OperatorEditorToolCommandQueue& queue, const OperatorEditorToolCommand& command, bool* duplicate )
                { return SubmitOperatorEditorCommand( queue, command, duplicate ); },
                accepted,
                result.coalescedDuplicateCommands );
        }
        return status;
    };

    result.status = mergeAll( legacy, result.acceptedLegacyCommands );
    if ( result.status.ok )
    {
        result.status = mergeAll( secondary, result.acceptedSecondaryCommands );
    }
    return result;
}

SkullbonezCore::Core::SbResult ProjectOperatorEditorCommands( const OperatorEditorCommandQueues& exchange,
                                                              InGameUICommands& commands )
{
    // Lane R: surfaces are untrusted presentation inputs. Re-run bounded
    // validation before modifying the established owner command packet.
    const OperatorEditorArbitrationResult validated = ArbitrateOperatorEditorCommands( exchange, {} );
    if ( !validated.status.ok )
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
        if ( canonical.rendering.commands[index].type == OperatorEditorRenderingCommandType::ToggleVsync )
        {
            commands.renderer.toggleVsync = true;
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
    return hash;
}
} // namespace SkullbonezCore::UI
