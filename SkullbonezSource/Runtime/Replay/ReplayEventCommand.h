/*
File: SkullbonezSource/Runtime/Replay/ReplayEventCommand.h
Purpose:
  Defines value-only replay event commands emitted by runtime domain owners.

Summary:
  Scene, editor, input, and tool code encode successful mutations into bounded
  commands. Run submits those values through replay composition without exposing
  recorder storage, branch authority, or callbacks to the emitting owner.

Glossary:
  Owner event: Stable wire-coded record of accepted owner work.
  Command batch: Fixed post-action output drained before the input turn ends.

Invariants:
  - Command text is owned inline; no transient formatting pointer crosses the seam.
  - Batches never allocate and report capacity exhaustion to their caller.
  - Domain enum ordinals are never serialized as owner event codes.

Related:
  - ReplayRecorder.h
  - ReplayTimeline.h
*/
#pragma once

#include "ReplayIdentity.h"
#include "../../Maths/Quaternion.h"
#include "../../Maths/Vector3.h"
#include "../../Physics/PhysicsHandles.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace SkullbonezCore
{
namespace Runtime
{
enum class ReplayEventKind : uint16_t
{
    Unknown = 0,
    TimelineStart = 1,

    // Value 2 belonged to the deleted mixed-command payload. Do not reuse it:
    // owner actions have explicit codes and intentionally start a new wire lane.
    OwnerAction = 10,
    BranchRestore = 3,
    WorldOverride = 4,
    LauncherConfig = 5,
    LauncherFire = 6,
    GeneratedSceneConfig = 7,
    EditorPlace = 8,
    EditorTransform = 9
};

// Stable wire values for accepted owner work. These numbers are serialized;
// domain enum ordinals and rejected requests must never be substituted.
enum class ReplayOwnerEventCode : int32_t
{
    SceneLoadBrowserIndex = 1001,
    SceneLoadDemo = 1002,
    SceneReset = 1003,
    SceneCreate = 1004,
    SceneSaveDefaults = 1005,
    CaptureScreenshot = 2001,
    RenderSaveOrdinaryDefaults = 3001,
    RenderSaveCinematicDefaults = 3002,
};

// Value-only event command accepted by replay composition. useNextFrame lets
// the timeline attach its current cursor without exposing timeline ownership.
struct ReplayEventCommand
{
    ReplayFrameIndex frameIndex = 0;
    ReplayEventKind kind = ReplayEventKind::Unknown;
    uint32_t flags = 0;
    int32_t value0 = 0;
    int32_t value1 = 0;
    int32_t value2 = 0;
    int32_t value3 = 0;
    uint64_t data0 = 0;
    char text[128] = {};
    bool useNextFrame = true;
};

struct ReplayEventCommandBatch
{
    static constexpr std::size_t CAPACITY = 32;

    bool Append( const ReplayEventCommand& command ) noexcept
    {

        if ( count >= commands.size() )
        {
            return false;
        }

        commands[count++] = command;
        return true;
    }

    std::array<ReplayEventCommand, CAPACITY> commands = {};
    std::size_t count = 0;
};

namespace ReplayEventCommandOperations
{
ReplayEventCommand BuildCommand( ReplayEventKind kind, ReplayFrameIndex frameIndex, bool useNextFrame, uint32_t flags,
                                 int32_t value0, int32_t value1, int32_t value2, int32_t value3, uint64_t data0,
                                 const char* text );
ReplayEventCommand BuildGeneratedSceneConfig( uint32_t flags, int modelCount, int solverBallCount, int solverBoxCount,
                                              uint32_t rngSeed, int sceneObjectCapacity,
                                              uint32_t generatedObjectTypeOverride );
ReplayEventCommand BuildWorldOverride( float previousGravity, float previousFluidHeight, float previousFluidDensity,
                                       float gravity, float fluidHeight, float fluidDensity );
ReplayEventCommand BuildLauncherConfig( uint32_t changedFlags, float impulseStrength, float projectileSpeed );
ReplayEventCommand BuildLauncherFire( const Math::Vector::Vector3& rayOrigin, const Math::Vector::Vector3& rayDirection,
                                      const Math::Vector::Vector3& cameraUp, bool projectile, float impulseStrength,
                                      float projectileSpeed, int modelCount );
ReplayEventCommand BuildEditorPlace( int objectType, bool fixedObject, bool terrainAlign, int modelCountBefore,
                                     const Math::Vector::Vector3& terrainPoint, const Math::Vector::Vector3& placementScale,
                                     float placementYawRadians );
ReplayEventCommand BuildEditorTransform( int modelIndex, uint32_t changedFlags, Physics::PhysicsSceneObjectId sceneObjectId,
                                         const Math::Vector::Vector3& position,
                                         const Math::Orientation::Quaternion& orientation, int modelCount, int scaleAxis,
                                         float scaleFactor );
} // namespace ReplayEventCommandOperations

} // namespace Runtime
} // namespace SkullbonezCore
