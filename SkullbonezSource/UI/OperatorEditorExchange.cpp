/*
File: SkullbonezSource/UI/OperatorEditorExchange.cpp
Purpose:
  Implements bounded operator-editor command validation and arbitration.

Summary:
  Legacy one-frame fields are normalized into four domain queues. The merge
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
    return left.value == right.value;
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
    if ( command.type != OperatorEditorPropertyCommandType::SetTimeScale &&
         command.type != OperatorEditorPropertyCommandType::SetWorldGravity )
    {
        return SkullbonezCore::Core::SbResult::Failure( OWNER, "Property command has an unknown action type" );
    }
    if ( !std::isfinite( command.value ) )
    {
        return SkullbonezCore::Core::SbResult::Failure( OWNER, "Property command requires a finite value" );
    }
    if ( command.type == OperatorEditorPropertyCommandType::SetTimeScale && command.value <= 0.0f )
    {
        return SkullbonezCore::Core::SbResult::Failure( OWNER, "Time-scale command requires a positive value" );
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
    if ( result.ok && commands.sceneOptions.requestedTimeScale > 0.0f )
    {
        result =
            SubmitOperatorEditorCommand( normalized.property,
                                         OperatorEditorPropertyCommand{ OperatorEditorPropertyCommandType::SetTimeScale,
                                                                        commands.sceneOptions.requestedTimeScale } );
    }
    if ( result.ok && commands.water.requestWorldGravity )
    {
        result = SubmitOperatorEditorCommand(
            normalized.property,
            OperatorEditorPropertyCommand{ OperatorEditorPropertyCommandType::SetWorldGravity,
                                           commands.water.requestedWorldGravity } );
    }
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
    commands.scene.saveSceneDefaults = false;
    commands.scene.createScene = false;
    commands.scene.requestedSceneName[0] = '\0';
    commands.scene.requestedSceneIndex = -1;
    commands.sceneOptions.requestedTimeScale = -1.0f;
    commands.water.requestWorldGravity = false;
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
    commands.scene.saveSceneDefaults = false;
    commands.scene.createScene = false;
    commands.scene.requestedSceneName[0] = '\0';
    commands.scene.requestedSceneIndex = -1;
    commands.sceneOptions.requestedTimeScale = -1.0f;
    commands.water.requestWorldGravity = false;
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
        if ( command.type == OperatorEditorPropertyCommandType::SetTimeScale )
        {
            commands.sceneOptions.requestedTimeScale = command.value;
        }
        else if ( command.type == OperatorEditorPropertyCommandType::SetWorldGravity )
        {
            commands.water.requestWorldGravity = true;
            commands.water.requestedWorldGravity = command.value;
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
    return hash;
}
} // namespace SkullbonezCore::UI
