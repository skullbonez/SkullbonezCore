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
    return left.sceneIndex == right.sceneIndex;
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
         command.type != OperatorEditorSceneCommandType::SetCurrentSceneIndex )
    {
        return SkullbonezCore::Core::SbResult::Failure( OWNER, "Scene command has an unknown action type" );
    }
    if ( command.type == OperatorEditorSceneCommandType::SetCurrentSceneIndex && command.sceneIndex < 0 )
    {
        return SkullbonezCore::Core::SbResult::Failure( OWNER, "Scene index command requires a non-negative index" );
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
    if ( !result.ok )
    {
        return result;
    }

    commands.operatorEditor = normalized;
    commands.scene.resetScene = false;
    commands.scene.requestedSceneIndex = -1;
    commands.sceneOptions.requestedTimeScale = -1.0f;
    commands.water.requestWorldGravity = false;
    commands.renderer.toggleVsync = false;
    commands.replayMemory.requestPolicy = false;
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
    commands.scene.requestedSceneIndex = -1;
    commands.sceneOptions.requestedTimeScale = -1.0f;
    commands.water.requestWorldGravity = false;
    commands.renderer.toggleVsync = false;
    commands.replayMemory.requestPolicy = false;

    for ( uint32_t index = 0u; index < canonical.scene.count; ++index )
    {
        const OperatorEditorSceneCommand& command = canonical.scene.commands[index];
        if ( command.type == OperatorEditorSceneCommandType::ResetCurrentScene )
        {
            commands.scene.resetScene = true;
        }
        else if ( command.type == OperatorEditorSceneCommandType::SetCurrentSceneIndex )
        {
            commands.scene.requestedSceneIndex = command.sceneIndex;
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
    return hash;
}
} // namespace SkullbonezCore::UI
