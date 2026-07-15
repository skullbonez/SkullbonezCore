/*
File: SkullbonezSource/Runtime/Init.cpp
Purpose:
  Bootstraps the Windows process, parses command-line options, and starts the run loop.

Summary:
  Init.cpp bootstraps the Windows process, parses command-line options, and
  starts the run loop. As an implementation unit, keep edits anchored on local
  owner boundaries and call direction and on the glossary/invariants below.

Glossary:
  DX11/OpenGL: Retired runtime renderer choices. The parser names them only to
  explain why old command lines are rejected.
  COM (Component Object Model): Windows interface lifetime model used by DX12
  and platform APIs through reference-counted objects.
  SDF (Signed Distance Field): Texture representation used for crisp scalable
  text rendering.
  Standalone physics smoke: Early-exit validation mode that exercises public
    physics API construction without runtime/window/renderer ownership.
  Runtime handle smoke: Early-exit validation mode that uses runtime
    SceneController construction but proves returned physics handles stay
    aligned with body, collider, constraint, and render mirrors.
  Lane R result: Recoverable CLI/startup failure that returns a process exit
    code with owner/message diagnostics instead of using a fatal exception.

Invariants:
  - DX12 is the only runtime renderer; retired renderer flags are parsed only
    to produce clear failures for old command lines.
  - Startup options are resolved before Run owns subsystems so validation
    launches are deterministic from their CLI.
  - Early-exit smoke modes must return before worker, window, renderer, or Run
    startup if their evidence claims subsystem isolation.

Related:
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "../Core/Common.h"
#include "WindowConstants.h"
#include "../Core/Log.h"
#include "Audio/ContactAudioService.h"
#include "Run.h"
#include "Allocation/RuntimeAllocationTracker.h"
#include "../Rendering/Text.h"
#include "Window.h"
#include "Input.h"
#include "../Core/Timer.h"
#include "../Rendering/DX12/RenderBackendDX12.h"
#include "RunLaunchOptions.Renderer.h"
#include "Startup/StartupCommandLine.h"
#include "Startup/StartupCrashLogging.h"
#include "Startup/StartupLaunchResolution.h"
#include "Scene/SceneController.h"
#include "../Physics/ColliderStore.h"
#include "../Physics/PhysicsBodyStore.h"
#include "../Physics/PhysicsApi.h"
#include "../Rendering/RenderInstanceStore.h"
#include "../World/WorldEnvironment.h"
#include "../Core/PlatformProfiler.h"
#include "../Core/Profiler.h"
#include "../Core/WorkerPool.h"
#include <cerrno>
#include <float.h>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <cstdint>
#include <exception>
#include <fstream>
#include <stdexcept>
#include <vector>
#include <string>
#include <io.h>
#include <objbase.h>

#pragma warning( push, 0 )
#include "../../ThirdPtySource/nlohmann/json.hpp"
#pragma warning( pop )

#ifdef _DEBUG
#include <dbghelp.h>
#pragma comment( lib, "dbghelp.lib" )
#endif


using namespace SkullbonezCore::Runtime;
using namespace SkullbonezCore::Runtime::Startup;
using namespace SkullbonezCore::Hardware;
using namespace SkullbonezCore::Rendering;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Physics;
using namespace SkullbonezCore::Threading;
namespace RuntimeAllocation = SkullbonezCore::Runtime::Allocation;


namespace
{
using Json = nlohmann::ordered_json;


void ReportStartupFailure( const SkullbonezCore::Core::SbResult& result, const char* title )
{
    const char* safeOwner = result.error.owner && result.error.owner[0] != '\0' ? result.error.owner : "Startup";
    const char* safeMessage =
        result.error.message[0] != '\0' ? result.error.message : "Startup failed without details.";
    char dialogMessage[1024] = {};
    sprintf_s( dialogMessage, sizeof( dialogMessage ), "%s\n\n%s", safeOwner, safeMessage );

    // Lane R: startup cannot rely on the game window or an attached terminal to
    // expose failures. Persist the diagnostic and block on a native error dialog
    // so a normal Explorer/IDE launch can never look like a silent clean exit.
    SkullbonezCore::Core::Log().WriteEventf( "startup_failure owner=\"%s\" message=\"%s\"", safeOwner, safeMessage );
    fprintf( stderr, "FATAL[%s]: %s\n", safeOwner, safeMessage );
    fflush( stderr );
    SkullbonezCore::Core::Log().FlushAll();
    MessageBoxA( nullptr, dialogMessage, title, MB_OK | MB_ICONERROR | MB_SETFOREGROUND );
}

// ---------------------------------------------------------------------------
// Console
// ---------------------------------------------------------------------------

// GUI apps have no console by default — attach to the parent terminal so
// fprintf(stderr/stdout) is visible when launched from cmd/PowerShell.
bool IsStandardHandleRedirected( DWORD standardHandle )
{
    HANDLE handle = GetStdHandle( standardHandle );
    if ( handle == nullptr || handle == INVALID_HANDLE_VALUE )
    {
        return false;
    }

    const DWORD fileType = GetFileType( handle );
    return fileType == FILE_TYPE_PIPE || fileType == FILE_TYPE_DISK;
}

void AttachParentConsole()
{
    const bool stdoutRedirected = IsStandardHandleRedirected( STD_OUTPUT_HANDLE );
    const bool stderrRedirected = IsStandardHandleRedirected( STD_ERROR_HANDLE );

    if ( AttachConsole( ATTACH_PARENT_PROCESS ) )
    {
        FILE* dummy = nullptr;
        if ( !stdoutRedirected )
        {
            freopen_s( &dummy, "CONOUT$", "w", stdout );
        }
        if ( !stderrRedirected )
        {
            freopen_s( &dummy, "CONOUT$", "w", stderr );
        }
    }
}


// ---------------------------------------------------------------------------
// --gen-atlas early exit
// SDF font atlas file generation path: exits before GPU context setup.
// True means the flag was present; outExitCode is 0 on success, 1 on failure.
// ---------------------------------------------------------------------------

bool HandleGenAtlas( const CommandLineView& commandLine, int& outExitCode )
{
    if ( !HasOption( commandLine, "--gen-atlas" ) )
    {
        return false;
    }

    char outPath[MAX_PATH];
    const char* atlasArg = FindOptionValue( commandLine, "--gen-atlas" );
    if ( atlasArg && *atlasArg != '\0' )
    {
        if ( strlen( atlasArg ) >= MAX_PATH )
        {
            fprintf( stderr, "[gen-atlas] Output path is too long.\n" );
            outExitCode = 1;
            return true;
        }
        strcpy_s( outPath, atlasArg );
    }
    else
    {
        strcpy_s( outPath, "SkullbonezData/font_atlas.sdf" );
    }

    fprintf( stdout, "[gen-atlas] Generating SDF font atlas: %s\n", outPath );
    if ( SkullbonezCore::Text::Text2d::GenerateSdfAtlasToFile( "Verdana", outPath ) )
    {
        fprintf( stdout, "[gen-atlas] Done.\n" );
        outExitCode = 0;
    }
    else
    {
        fprintf( stderr, "[gen-atlas] FAILED.\n" );
        outExitCode = 1;
    }
    return true;
}

struct PhysicsRuntimeHandleSmokeResult
{
    bool passed = false;
    bool handlesMatchStores = false;
    bool renderMirrorMatches = false;
    bool jointUsesHandles = false;
    bool colliderRefreshMatches = false;
    bool reorderPreservesHandleState = false;
    bool failedCreationIsAtomic = false;
    bool deletionIsAtomic = false;
    bool mutationUsesStableHandle = false;
    int bodyCount = 0;
    int colliderCount = 0;
    int renderInstanceCount = 0;
    std::size_t pointJointCount = 0;
    PhysicsBodyHandle bodyA;
    std::string errorMessage;
};


PhysicsRuntimeHandleSmokeResult RunPhysicsRuntimeHandleSmokeSample()
{
    // Why: this smoke proves runtime-created bodies keep their returned physics
    // handles aligned with body/collider stores and render snapshots without opening the
    // window or renderer. WinMain runs the normal command-line/config bootstrap
    // before this helper so collection capacity uses the same config snapshot
    // as a regular runtime launch.
    // Lifetime: SceneController owns the validation-only physics and entity
    // stores exactly as it does during a normal scene. The cold smoke keeps the
    // owner off the launcher stack and executes once before steady gameplay.
    auto collection = std::make_unique<SkullbonezCore::Runtime::SceneController>();
    SkullbonezCore::Physics::PhysicsEngine& physics = collection->Physics();
    SkullbonezCore::Runtime::SceneEntityStore& sceneEntities = collection->Entities();
    PhysicsRuntimeHandleSmokeResult result;
    PhysicsBodyHandle createdBodies[2];

    for ( int i = 0; i < 2; ++i )
    {
        SkullbonezCore::Runtime::SceneEntityCreateDesc model;
        char name[32] = {};
        sprintf_s( name, sizeof( name ), "runtime_smoke_%d", i );
        model.SetName( name );
        PhysicsSceneObjectId sceneObjectId;
        sceneObjectId.value = static_cast<uint32_t>( i + 1 );
        model.sceneObjectId = sceneObjectId;
        const SkullbonezCore::Math::CollisionDetection::BoundingSphere shape(
            0.75f,
            SkullbonezCore::Math::Vector::Vector3( 0.0f, 0.0f, 0.0f ) );
        const auto appendResult = collection->TryCreateSceneEntity(
            std::move( model ),
            MakePhysicsBodyCreateDesc(
                sceneObjectId,
                shape,
                SkullbonezCore::Math::Vector::Vector3( static_cast<float>( i ) * 2.0f, 4.0f, 0.0f ),
                SkullbonezCore::Math::Orientation::IDENTITY_QUATERNION,
                SkullbonezCore::Math::Vector::Vector3( 0.0f, 0.0f, 0.0f ),
                SkullbonezCore::Math::Vector::Vector3( 0.0f, 0.0f, 0.0f ),
                SkullbonezCore::Math::Vector::Vector3( 1.0f, 1.0f, 1.0f ),
                2.0f + static_cast<float>( i ),
                0.0f,
                PhysicsBodyMotionKind::Dynamic,
                nullptr,
                name ),
            MakeColliderCreateDesc( shape, 0.0f, HashStr( "default" ) ) );
        if ( !appendResult.status.ok )
        {
            result.errorMessage = appendResult.status.error.message;
            return result;
        }
        createdBodies[i] = appendResult.body;
    }

    const int entityCountBeforeFailure = sceneEntities.Count();
    const int bodyCountBeforeFailure = collection->BodyStore().Count();
    const int colliderCountBeforeFailure = collection->Colliders().Count();
    const int renderCountBeforeFailure = collection->GetRenderInstanceStore().Count();
    const uint32_t descriptorCountBeforeFailure = physics.AuthoredBodyDescriptorCount().value;
    SkullbonezCore::Runtime::SceneEntityCreateDesc duplicateEntity;
    duplicateEntity.sceneObjectId = PhysicsSceneObjectId{ 1u };
    duplicateEntity.SetName( "runtime_smoke_duplicate" );
    const SkullbonezCore::Math::CollisionDetection::BoundingSphere duplicateShape(
        0.5f,
        SkullbonezCore::Math::Vector::Vector3( 0.0f, 0.0f, 0.0f ) );
    const auto duplicateResult = collection->TryCreateSceneEntity(
        std::move( duplicateEntity ),
        MakePhysicsBodyCreateDesc( PhysicsSceneObjectId{ 1u },
                                   duplicateShape,
                                   SkullbonezCore::Math::Vector::Vector3( 0.0f, 8.0f, 0.0f ),
                                   SkullbonezCore::Math::Orientation::IDENTITY_QUATERNION,
                                   SkullbonezCore::Math::Vector::Vector3( 0.0f, 0.0f, 0.0f ),
                                   SkullbonezCore::Math::Vector::Vector3( 0.0f, 0.0f, 0.0f ),
                                   SkullbonezCore::Math::Vector::Vector3( 1.0f, 1.0f, 1.0f ),
                                   1.0f,
                                   0.0f,
                                   PhysicsBodyMotionKind::Dynamic,
                                   nullptr,
                                   "runtime_smoke_duplicate" ),
        MakeColliderCreateDesc( duplicateShape, 0.0f, HashStr( "default" ) ) );
    const bool failedCreationIsAtomic = !duplicateResult.status.ok &&
                                        sceneEntities.Count() == entityCountBeforeFailure &&
                                        collection->BodyStore().Count() == bodyCountBeforeFailure &&
                                        collection->Colliders().Count() == colliderCountBeforeFailure &&
                                        collection->GetRenderInstanceStore().Count() == renderCountBeforeFailure &&
                                        physics.AuthoredBodyDescriptorCount().value == descriptorCountBeforeFailure;

    const PhysicsBodyHandle bodyA = createdBodies[0];
    const PhysicsBodyHandle bodyB = createdBodies[1];

    PhysicsPointJointCreateDesc jointDesc;
    jointDesc.bodyA = bodyA;
    jointDesc.bodyB = bodyB;
    jointDesc.localAnchorA = SkullbonezCore::Math::Vector::Vector3( 0.25f, 0.0f, 0.0f );
    jointDesc.localAnchorB = SkullbonezCore::Math::Vector::Vector3( -0.25f, 0.0f, 0.0f );
    const PhysicsConstraintHandle jointHandle = physics.CreatePointJoint( jointDesc );

    const PhysicsBodyStore& bodyStore = collection->BodyStore();
    const ColliderStore& colliderStore = collection->Colliders();
    const RenderInstanceStore& renderStore = collection->GetRenderInstanceStore();
    const std::vector<PointJointConstraint>& pointJoints =
        PhysicsEngine::ReadPointJointConstraints( collection->Physics() );
    const size_t initialColliderCount = colliderStore.Count();
    const ColliderRecord initialCollider = colliderStore.Records()[0];

    const SkullbonezCore::Math::Vector::Vector3 editedHalfExtents( 0.25f, 1.25f, 0.5f );
    constexpr float EDITED_RESTITUTION = 0.42f;
    PhysicsBodyUpdateDesc colliderUpdate;
    colliderUpdate.body = bodyA;
    const bool colliderUpdateAccepted = physics.UpdateAuthoredBodyAndCollider(
        colliderUpdate,
        MakeColliderCreateDesc( SkullbonezCore::Math::CollisionDetection::BoundingBox(
                                    editedHalfExtents,
                                    SkullbonezCore::Math::Vector::Vector3( 0.0f, 0.0f, 0.0f ) ),
                                EDITED_RESTITUTION,
                                HashStr( "default" ) ) );
    const ColliderStore& refreshedColliderStore = collection->Colliders();
    const ColliderRecord& refreshedCollider = refreshedColliderStore.Records()[0];
    const float expectedBoxRadius = sqrtf( 0.25f * 0.25f + 1.25f * 1.25f + 0.5f * 0.5f );
    // Invariant: same-count authoring edits must be visible through the explicit
    // collider edit commit. Store reads only auto-repair topology changes, so
    // tools and scene edits must commit before asking for collider records.
    const bool colliderRefreshMatches =
        colliderUpdateAccepted && initialCollider.shapeKind == ColliderShapeKind::Sphere &&
        refreshedCollider.shapeKind == ColliderShapeKind::Box &&
        fabsf( refreshedCollider.boundingRadius - expectedBoxRadius ) < 0.0001f &&
        fabsf( refreshedCollider.restitution - 0.42f ) < 0.0001f &&
        fabsf( refreshedCollider.projectedSurfaceArea - initialCollider.projectedSurfaceArea ) > 0.0001f &&
        fabsf( refreshedCollider.dragCoefficient - initialCollider.dragCoefficient ) > 0.0001f &&
        refreshedCollider.handle == initialCollider.handle && refreshedCollider.body == initialCollider.body &&
        refreshedColliderStore.Count() == initialColliderCount;

    const PhysicsBodyRecord* bodyARecord = bodyStore.RecordForModelIndex( 0 );
    const PhysicsBodyRecord* bodyBRecord = bodyStore.RecordForModelIndex( 1 );
    const RenderInstanceHandle renderHandleA = renderStore.HandleForModelIndex( 0 );
    const bool handlesMatchStores = bodyA.IsValid() && bodyB.IsValid() && bodyARecord && bodyBRecord &&
                                    bodyARecord->handle == bodyA && bodyBRecord->handle == bodyB &&
                                    colliderStore.HandleForBodyHandle( bodyA ).IsValid() &&
                                    colliderStore.HandleForBodyHandle( bodyB ).IsValid();
    const bool renderMirrorMatches = bodyARecord && renderStore.Count() == 2 && renderHandleA.IsValid() &&
                                     renderStore.ModelIndexForHandle( renderHandleA ) == 0 &&
                                     !renderStore.Records().empty() &&
                                     renderStore.Records()[0].replayBodyId == bodyARecord->replayBodyId;
    const bool jointUsesHandles = jointHandle.IsValid() && pointJoints.size() == 1 && pointJoints[0].bodyA == bodyA &&
                                  pointJoints[0].bodyB == bodyB && pointJoints[0].BodyAIndex( bodyStore ) == 0 &&
                                  pointJoints[0].BodyBIndex( bodyStore ) == 1;

    constexpr uint32_t REORDER_BODY_A_REPLAY_ID = 100u;
    constexpr uint32_t REORDER_BODY_B_REPLAY_ID = 101u;
    // Why: PhysicsBodyStore owns SkullbonezCore::Scene::Capacity::MAX_GAME_MODELS fixed arrays. Keep this cold
    // standalone probe owner off WinMain's bounded thread stack.
    auto reorderBodyStore = std::make_unique<PhysicsBodyStore>();
    std::vector<PhysicsBodyCreateDesc> reorderBodyDescs;
    for ( int i = 0; i < 2; ++i )
    {
        PhysicsBodyCreateDesc desc;
        desc.sceneObjectId =
            MakePhysicsSceneObjectIdFromReplayBodyId( REORDER_BODY_A_REPLAY_ID + static_cast<uint32_t>( i ) );
        desc.shape = SkullbonezCore::Math::CollisionDetection::BoundingSphere(
            0.5f,
            SkullbonezCore::Math::Vector::Vector3( 0.0f, 0.0f, 0.0f ) );
        desc.position = SkullbonezCore::Math::Vector::Vector3( static_cast<float>( i ) * 3.0f, 5.0f, 0.0f );
        desc.rotationalInertia = SkullbonezCore::Math::Vector::Vector3( 1.0f, 1.0f, 1.0f );
        desc.mass = 3.0f + static_cast<float>( i );
        desc.boundingRadius = SkullbonezCore::Math::CollisionDetection::GetShapeBoundingRadius( desc.shape );
        desc.volume = SkullbonezCore::Math::CollisionDetection::GetShapeVolume( desc.shape );
        desc.projectedSurfaceArea =
            SkullbonezCore::Math::CollisionDetection::GetShapeProjectedSurfaceArea( desc.shape );
        desc.dragCoefficient = SkullbonezCore::Math::CollisionDetection::GetShapeDragCoefficient( desc.shape );
        reorderBodyDescs.push_back( desc );
    }
    reorderBodyStore->LoadFromDescriptors( reorderBodyDescs, std::vector<uint8_t>{} );
    const PhysicsBodyHandle reorderedOriginalBody = reorderBodyStore->HandleForModelIndex( 0 );
    const uint32_t reorderBodyAReplayId = REORDER_BODY_A_REPLAY_ID;
    const uint32_t reorderBodyBReplayId = REORDER_BODY_B_REPLAY_ID;
    const SkullbonezCore::Math::Vector::Vector3 pendingImpulse( 0.0f, 2.0f, 0.0f );
    const SkullbonezCore::Math::Vector::Vector3 pendingImpulsePoint( 0.25f, 0.0f, 0.0f );
    const bool seededReorderState =
        reorderBodyStore->SetPendingBodyImpulse( reorderedOriginalBody, pendingImpulse, pendingImpulsePoint ) &&
        reorderBodyStore->SeedBodyAsleep( reorderedOriginalBody );
    reorderBodyDescs[0].sceneObjectId = MakePhysicsSceneObjectIdFromReplayBodyId( reorderBodyBReplayId );
    reorderBodyDescs[1].sceneObjectId = MakePhysicsSceneObjectIdFromReplayBodyId( reorderBodyAReplayId );
    reorderBodyStore->LoadFromDescriptors( reorderBodyDescs, std::vector<uint8_t>{} );
    const int reorderedBodyAIndex = reorderBodyStore->ModelIndexForHandle( reorderedOriginalBody );
    const PhysicsBodyRecord* reorderedBodyARecord =
        reorderedBodyAIndex >= 0 ? reorderBodyStore->RecordForModelIndex( reorderedBodyAIndex ) : nullptr;
    // Invariant: allocator-owned handles must carry physics-owned one-shot
    // state through a same-scene reorder. Otherwise the handle identity is only
    // nominally independent from model order.
    const bool reorderPreservesHandleState =
        seededReorderState && reorderedBodyAIndex == 1 && reorderedBodyARecord &&
        reorderedBodyARecord->handle == reorderedOriginalBody && reorderedBodyARecord->hasPendingImpulse &&
        reorderedBodyARecord->isSleeping &&
        fabsf( reorderedBodyARecord->pendingImpulse.y - pendingImpulse.y ) < 0.0001f &&
        fabsf( reorderedBodyARecord->pendingImpulseApplicationPoint.x - pendingImpulsePoint.x ) < 0.0001f;

    const PhysicsBodyRecord* bodyBBeforeDelete = collection->BodyStore().RecordForHandle( bodyB );
    const SkullbonezCore::Math::Vector::Vector3 liveOnlyPosition( 42.0f, 17.0f, -3.0f );
    const bool seededLiveOnlyState =
        bodyBBeforeDelete && physics.RestoreReplayBodyState( bodyB,
                                                             bodyBBeforeDelete->replayBodyId,
                                                             bodyBBeforeDelete->isFixed,
                                                             liveOnlyPosition,
                                                             bodyBBeforeDelete->orientation,
                                                             bodyBBeforeDelete->linearVelocity,
                                                             bodyBBeforeDelete->angularVelocity,
                                                             bodyBBeforeDelete->mass,
                                                             bodyBBeforeDelete->invMass,
                                                             bodyBBeforeDelete->rotationalInertia,
                                                             bodyBBeforeDelete->invRotationalInertia );
    const bool destroyedBodyA = collection->DestroySceneEntity( bodyA );
    const PhysicsBodyRecord* survivingBody = collection->BodyStore().RecordForHandle( bodyB );
    const bool deletionIsAtomic = seededLiveOnlyState && destroyedBodyA && !collection->BodyStore().Contains( bodyA ) &&
                                  survivingBody && collection->BodyStore().ModelIndexForHandle( bodyB ) == 0 &&
                                  sceneEntities.Count() == 1 && sceneEntities.At( 0 ).body == bodyB &&
                                  collection->BodyStore().Count() == 1 && collection->Colliders().Count() == 1 &&
                                  collection->Colliders().HandleForBodyHandle( bodyB ).IsValid() &&
                                  collection->GetRenderInstanceStore().Count() == 1 &&
                                  collection->GetRenderInstanceStore().PresentationCount() == 1 &&
                                  physics.AuthoredBodyDescriptorCount().value == 1u &&
                                  PhysicsEngine::ReadPointJointConstraints( collection->Physics() ).empty() &&
                                  fabsf( survivingBody->position.x - liveOnlyPosition.x ) < 0.0001f &&
                                  fabsf( survivingBody->position.y - liveOnlyPosition.y ) < 0.0001f &&
                                  fabsf( survivingBody->position.z - liveOnlyPosition.z ) < 0.0001f;

    PhysicsBodyUpdateDesc staleUpdate;
    staleUpdate.body = bodyA;
    staleUpdate.updateMask = PHYSICS_BODY_UPDATE_POSE;
    PhysicsBodyUpdateDesc survivingUpdate;
    survivingUpdate.body = bodyB;
    survivingUpdate.updateMask = PHYSICS_BODY_UPDATE_VELOCITY | PHYSICS_BODY_UPDATE_MASS;
    survivingUpdate.linearVelocity = SkullbonezCore::Math::Vector::Vector3( 2.0f, 3.0f, 4.0f );
    survivingUpdate.angularVelocity = SkullbonezCore::Math::Vector::Vector3( 0.0f, 0.5f, 0.0f );
    survivingUpdate.mass = 7.0f;
    survivingUpdate.rotationalInertia =
        survivingBody ? survivingBody->rotationalInertia : SkullbonezCore::Math::Vector::ZERO_VECTOR;
    const bool staleMutationRejected = !physics.UpdateAuthoredBody( staleUpdate );
    const bool survivingMutationAccepted = physics.UpdateAuthoredBody( survivingUpdate );
    survivingBody = collection->BodyStore().RecordForHandle( bodyB );
    const bool mutationUsesStableHandle = staleMutationRejected && survivingMutationAccepted && survivingBody &&
                                          fabsf( survivingBody->mass - 7.0f ) < 0.0001f &&
                                          fabsf( survivingBody->linearVelocity.x - 2.0f ) < 0.0001f &&
                                          fabsf( survivingBody->angularVelocity.y - 0.5f ) < 0.0001f;

    result.handlesMatchStores = handlesMatchStores;
    result.renderMirrorMatches = renderMirrorMatches;
    result.jointUsesHandles = jointUsesHandles;
    result.colliderRefreshMatches = colliderRefreshMatches;
    result.reorderPreservesHandleState = reorderPreservesHandleState;
    result.failedCreationIsAtomic = failedCreationIsAtomic;
    result.deletionIsAtomic = deletionIsAtomic;
    result.mutationUsesStableHandle = mutationUsesStableHandle;
    result.bodyCount = bodyCountBeforeFailure;
    result.colliderCount = colliderCountBeforeFailure;
    result.renderInstanceCount = renderCountBeforeFailure;
    result.pointJointCount = jointUsesHandles ? 1u : 0u;
    result.bodyA = bodyA;
    result.passed = handlesMatchStores && renderMirrorMatches && jointUsesHandles && colliderRefreshMatches &&
                    reorderPreservesHandleState && failedCreationIsAtomic && deletionIsAtomic &&
                    mutationUsesStableHandle;
    return result;
}


bool HandlePhysicsStandaloneSmoke( const CommandLineView& commandLine, int& outExitCode )
{
    if ( !HasOption( commandLine, "--physics-standalone-smoke" ) &&
         !HasOption( commandLine, "--physics_standalone_smoke" ) )
    {
        return false;
    }

    // Why: this option runs before WorkerPool, Window, renderer, Run, or scene
    // setup so it proves the public physics API and runtime handle alignment can
    // be constructed without renderer/window services.
    const PhysicsStandaloneSmokeResult result = RunPhysicsStandaloneSmoke();
    PhysicsRuntimeHandleSmokeResult runtimeMirror = RunPhysicsRuntimeHandleSmokeSample();
    auto writeReport = [&]( FILE* stream )
    {
        if ( !stream )
        {
            return;
        }
        fprintf( stream,
                 "[physics-standalone-smoke] bodies=%u steps=%u final_position=(%.6f,%.6f,%.6f) "
                 "final_velocity=(%.6f,%.6f,%.6f) secondary_position=(%.6f,%.6f,%.6f) "
                 "secondary_velocity=(%.6f,%.6f,%.6f) secondary_step=%s lifecycle_checks=%s "
                 "contacts=%u contact_hash=0x%016llX runtime_mirror_checks=%s hash=0x%016llX\n",
                 result.bodyCount,
                 result.stepCount,
                 result.finalPosition.x,
                 result.finalPosition.y,
                 result.finalPosition.z,
                 result.finalLinearVelocity.x,
                 result.finalLinearVelocity.y,
                 result.finalLinearVelocity.z,
                 result.secondaryFinalPosition.x,
                 result.secondaryFinalPosition.y,
                 result.secondaryFinalPosition.z,
                 result.secondaryFinalLinearVelocity.x,
                 result.secondaryFinalLinearVelocity.y,
                 result.secondaryFinalLinearVelocity.z,
                 result.secondaryBodyAdvanced ? "pass" : "fail",
                 result.lifecycleChecksPassed ? "pass" : "fail",
                 result.contactCount,
                 static_cast<unsigned long long>( result.contactHash ),
                 runtimeMirror.passed ? "pass" : "fail",
                 static_cast<unsigned long long>( result.deterministicHash ) );
        fprintf( stream,
                 "[physics-runtime-handle-smoke] bodies=%d colliders=%d render_instances=%d point_joints=%zu "
                 "handle_a=(%u,%u) store_handles=%s render_mirror=%s joint_handles=%s collider_refresh=%s "
                 "reorder_state=%s creation_atomic=%s deletion_atomic=%s mutation_handle=%s\n",
                 runtimeMirror.bodyCount,
                 runtimeMirror.colliderCount,
                 runtimeMirror.renderInstanceCount,
                 runtimeMirror.pointJointCount,
                 runtimeMirror.bodyA.index,
                 runtimeMirror.bodyA.generation,
                 runtimeMirror.handlesMatchStores ? "pass" : "fail",
                 runtimeMirror.renderMirrorMatches ? "pass" : "fail",
                 runtimeMirror.jointUsesHandles ? "pass" : "fail",
                 runtimeMirror.colliderRefreshMatches ? "pass" : "fail",
                 runtimeMirror.reorderPreservesHandleState ? "pass" : "fail",
                 runtimeMirror.failedCreationIsAtomic ? "pass" : "fail",
                 runtimeMirror.deletionIsAtomic ? "pass" : "fail",
                 runtimeMirror.mutationUsesStableHandle ? "pass" : "fail" );
        if ( !runtimeMirror.errorMessage.empty() )
        {
            fprintf( stream, "[physics-runtime-handle-smoke] error=\"%s\"\n", runtimeMirror.errorMessage.c_str() );
        }
        fflush( stream );
    };

    writeReport( stdout );

    const char* reportPath =
        FindOptionValue( commandLine, "--physics-standalone-smoke-log", "--physics_standalone_smoke_log" );
    if ( reportPath && !IsOptionValueMissing( reportPath ) )
    {
        FILE* reportFile = nullptr;
        if ( fopen_s( &reportFile, reportPath, "w" ) == 0 && reportFile )
        {
            writeReport( reportFile );
            fclose( reportFile );
        }
    }

    if ( !result.passed || !runtimeMirror.passed )
    {
        fprintf( stderr,
                 "FAIL: physics smoke final state, lifecycle, or runtime mirror checks did not match the expected "
                 "sample.\n" );
        outExitCode = 1;
        return true;
    }

    fprintf( stdout, "PASS: standalone physics and runtime handle mirror smoke matched expected state.\n" );
    outExitCode = 0;
    return true;
}

// ---------------------------------------------------------------------------
// Command-line parsing
// ---------------------------------------------------------------------------


struct PhysicsDebugComponentDirective
{
    const char* dashedName;
    const char* underscoredName;
    uint32_t flag;
};

struct PhysicsDebugFloatDirective
{
    const char* dashedName;
    const char* underscoredName;
    bool ParsedArgs::* hasOverride;
    float ParsedArgs::* value;
    float minValue;
    float maxValue;
    const char* errorMessage;
    bool enableTransparentBodies;
};


bool SceneArgHasPathSyntax( const std::string& sceneArg )
{
    return sceneArg.find( '/' ) != std::string::npos || sceneArg.find( '\\' ) != std::string::npos ||
           sceneArg.find( ':' ) != std::string::npos;
}


bool SceneArgHasExtension( const std::string& sceneArg )
{
    const size_t slash = sceneArg.find_last_of( "/\\" );
    const size_t dot = sceneArg.find_last_of( '.' );
    return dot != std::string::npos && ( slash == std::string::npos || dot > slash );
}


bool FileExistsForLaunch( const std::string& path )
{
    return _access( path.c_str(), 0 ) == 0;
}


std::string HeroSceneLaunchPath()
{
    return std::string( DATA_ROOT ) + "scenes/concept_12_low_poly_art_style.scene.json";
}


std::string ResolveSceneLaunchPath( const char* rawSceneArg )
{
    std::string sceneArg( rawSceneArg );
    if ( sceneArg.empty() || SceneArgHasPathSyntax( sceneArg ) )
    {
        return sceneArg;
    }

    if ( _stricmp( sceneArg.c_str(), "hero" ) == 0 || _stricmp( sceneArg.c_str(), "low_poly_hero" ) == 0 ||
         _stricmp( sceneArg.c_str(), "low-poly-hero" ) == 0 )
    {
        return HeroSceneLaunchPath();
    }

    const std::string sceneDir = std::string( DATA_ROOT ) + "scenes/";
    if ( !SceneArgHasExtension( sceneArg ) )
    {
        const std::string sceneCandidate = sceneDir + sceneArg + ".scene.json";
        if ( FileExistsForLaunch( sceneCandidate ) )
        {
            return sceneCandidate;
        }
    }

    const std::string directCandidate = sceneDir + sceneArg;
    if ( FileExistsForLaunch( directCandidate ) )
    {
        return directCandidate;
    }

    return sceneArg;
}


std::string ResolveSuiteLaunchPath( const char* rawSuiteArg )
{
    std::string suiteArg( rawSuiteArg );
    if ( suiteArg.empty() || SceneArgHasPathSyntax( suiteArg ) )
    {
        return suiteArg;
    }

    const std::string sceneDir = std::string( DATA_ROOT ) + "scenes/";
    if ( !SceneArgHasExtension( suiteArg ) )
    {
        const std::string suiteCandidate = sceneDir + suiteArg + ".suite.json";
        if ( FileExistsForLaunch( suiteCandidate ) )
        {
            return suiteCandidate;
        }
    }

    const std::string directCandidate = sceneDir + suiteArg;
    if ( FileExistsForLaunch( directCandidate ) )
    {
        return directCandidate;
    }

    return suiteArg;
}


bool ParsePhysicsDebugMode( const char* value, uint32_t& outFlags )
{
    if ( IsOptionValueMissing( value ) )
    {
        return false;
    }
    if ( _stricmp( value, "none" ) == 0 || _stricmp( value, "off" ) == 0 )
    {
        outFlags = PHYSICS_DEBUG_NONE;
        return true;
    }
    if ( _stricmp( value, "axes" ) == 0 )
    {
        outFlags = PHYSICS_DEBUG_AXES;
        return true;
    }
    if ( _stricmp( value, "contacts" ) == 0 )
    {
        outFlags = PHYSICS_DEBUG_CONTACTS;
        return true;
    }
    if ( _stricmp( value, "sleep" ) == 0 )
    {
        outFlags = PHYSICS_DEBUG_SLEEP;
        return true;
    }
    if ( _stricmp( value, "pipeline" ) == 0 )
    {
        outFlags = PHYSICS_DEBUG_PIPELINE;
        return true;
    }
    if ( _stricmp( value, "terrain" ) == 0 || _stricmp( value, "terrain_contact" ) == 0 ||
         _stricmp( value, "terrain-contact" ) == 0 || _stricmp( value, "terrain_probe" ) == 0 ||
         _stricmp( value, "terrain-probe" ) == 0 )
    {
        outFlags = PHYSICS_DEBUG_TERRAIN_CONTACT;
        return true;
    }
    if ( _stricmp( value, "all" ) == 0 || _stricmp( value, "on" ) == 0 )
    {
        outFlags = PHYSICS_DEBUG_ALL;
        return true;
    }
    return false;
}

bool ApplyPhysicsDebugComponentOverride( const CommandLineView& commandLine,
                                         const char* dashedName,
                                         const char* underscoredName,
                                         uint32_t flag,
                                         ParsedArgs& out )
{
    const char* value = FindOptionValue( commandLine, dashedName, underscoredName );
    if ( !value )
    {
        return true;
    }

    bool enabled = false;
    if ( !ParseOptionalOnOffValue( value, enabled ) )
    {
        return FailCommandLineParse( "%s expects optional on|off.", dashedName );
    }

    if ( !out.hasPhysicsDebugFlagsOverride )
    {
        out.physicsDebugFlagsOverride = PHYSICS_DEBUG_NONE;
    }
    out.hasPhysicsDebugFlagsOverride = true;
    if ( enabled )
    {
        out.physicsDebugFlagsOverride |= flag;
    }
    else
    {
        out.physicsDebugFlagsOverride &= ~flag;
    }
    return true;
}

bool ApplyPhysicsDebugFloatOverride( const CommandLineView& commandLine,
                                     const PhysicsDebugFloatDirective& directive,
                                     ParsedArgs& out )
{
    const char* value = FindOptionValue( commandLine, directive.dashedName, directive.underscoredName );
    if ( !value )
    {
        return true;
    }

    float parsed = 0.0f;
    if ( !ParseFloatToken( value, parsed ) || parsed < directive.minValue || parsed > directive.maxValue )
    {
        return FailCommandLineParse( directive.errorMessage );
    }

    out.*( directive.hasOverride ) = true;
    out.*( directive.value ) = parsed;
    if ( directive.enableTransparentBodies && !out.hasPhysicsDebugTransparentOverride )
    {
        out.hasPhysicsDebugTransparentOverride = true;
        out.physicsDebugTransparentOverride = true;
    }
    return true;
}

} // anonymous namespace


namespace SkullbonezCore
{
namespace Runtime
{
namespace Startup
{
bool ParsePhysicsDebugOverrides( const CommandLineView& commandLine, ParsedArgs& out )
{
    const char* modeValue = FindOptionValue( commandLine, "--physics-debug", "--physics_debug" );
    if ( modeValue )
    {
        if ( !ParsePhysicsDebugMode( modeValue, out.physicsDebugFlagsOverride ) )
        {
            return FailCommandLineParse(
                "--physics-debug expects none|axes|contacts|sleep|pipeline|terrain|all|on|off." );
        }
        out.hasPhysicsDebugFlagsOverride = true;
    }

    static const PhysicsDebugComponentDirective kComponentOverrides[] = {
        { "--physics-debug-axes", "--physics_debug_axes", PHYSICS_DEBUG_AXES },
        { "--physics-debug-contacts", "--physics_debug_contacts", PHYSICS_DEBUG_CONTACTS },
        { "--physics-debug-sleep", "--physics_debug_sleep", PHYSICS_DEBUG_SLEEP },
        { "--physics-debug-pipeline", "--physics_debug_pipeline", PHYSICS_DEBUG_PIPELINE },
        { "--physics-debug-terrain-contact", "--physics_debug_terrain_contact", PHYSICS_DEBUG_TERRAIN_CONTACT },
    };
    for ( const PhysicsDebugComponentDirective& component : kComponentOverrides )
    {
        if ( !ApplyPhysicsDebugComponentOverride( commandLine,
                                                  component.dashedName,
                                                  component.underscoredName,
                                                  component.flag,
                                                  out ) )
        {
            return false;
        }
    }

    const char* transparentValue =
        FindOptionValue( commandLine, "--physics-debug-transparent", "--physics_debug_transparent" );
    if ( transparentValue )
    {
        if ( !ParseOptionalOnOffValue( transparentValue, out.physicsDebugTransparentOverride ) )
        {
            return FailCommandLineParse( "--physics-debug-transparent expects optional on|off." );
        }
        out.hasPhysicsDebugTransparentOverride = true;
    }

    static const PhysicsDebugFloatDirective kFloatOverrides[] = {
        { "--physics-debug-alpha",
          "--physics_debug_alpha",
          &ParsedArgs::hasPhysicsDebugAlphaOverride,
          &ParsedArgs::physicsDebugAlphaOverride,
          0.05f,
          1.0f,
          "--physics-debug-alpha expects 0.05..1.0.",
          true },
        { "--physics-debug-contact-linger",
          "--physics_debug_contact_linger",
          &ParsedArgs::hasPhysicsDebugContactLingerOverride,
          &ParsedArgs::physicsDebugContactLingerOverride,
          0.0f,
          5.0f,
          "--physics-debug-contact-linger expects 0.0..5.0 seconds.",
          false },
    };
    for ( const PhysicsDebugFloatDirective& directive : kFloatOverrides )
    {
        if ( !ApplyPhysicsDebugFloatOverride( commandLine, directive, out ) )
        {
            return false;
        }
    }

    if ( out.hasPhysicsDebugFlagsOverride )
    {
        fprintf( stdout, "[physics-debug] Flags override: 0x%02x\n", out.physicsDebugFlagsOverride );
    }
    if ( out.hasPhysicsDebugTransparentOverride )
    {
        fprintf( stdout,
                 "[physics-debug] Transparent bodies: %s\n",
                 out.physicsDebugTransparentOverride ? "on" : "off" );
    }
    if ( out.hasPhysicsDebugAlphaOverride )
    {
        fprintf( stdout, "[physics-debug] Body alpha: %.3f\n", out.physicsDebugAlphaOverride );
    }
    if ( out.hasPhysicsDebugContactLingerOverride )
    {
        fprintf( stdout, "[physics-debug] Contact linger: %.3fs\n", out.physicsDebugContactLingerOverride );
    }

    return true;
}

// Build the ordered list of scene paths from --suite or --scene.
// Falls back to a single empty string (generated demo mode) when neither flag is given.
bool ParseSceneArgs( const CommandLineView& commandLine, std::vector<std::string>& sceneList, bool& isSuiteOrSceneMode )
{
    const char* suiteArg = FindOptionValue( commandLine, "--suite" );
    const char* sceneArg = FindOptionValue( commandLine, "--scene" );
    const bool heroArg = HasOption( commandLine, "--hero" );
    const bool demoHeroArg = HasOption( commandLine, "--demohero" ) || HasOption( commandLine, "--demo-hero" );

    if ( ( suiteArg && sceneArg ) || ( heroArg && ( suiteArg || sceneArg ) ) ||
         ( demoHeroArg && ( suiteArg || sceneArg || heroArg ) ) )
    {
        return FailCommandLineParse( "--demohero, --hero, --suite, and --scene are mutually exclusive." );
    }

    if ( heroArg )
    {
        sceneList.push_back( HeroSceneLaunchPath() );
        isSuiteOrSceneMode = true;
        fprintf( stdout, "[scene] Hero scene selected.\n" );
    }
    else if ( suiteArg )
    {
        if ( IsOptionValueMissing( suiteArg ) )
        {
            return FailCommandLineParse( "--suite requires a path." );
        }

        // Resolve a suite JSON path from either a file token or a repository-relative path.
        const std::string suitePath = ResolveSuiteLaunchPath( suiteArg );

        std::ifstream suiteFile( suitePath );
        if ( !suiteFile )
        {
            return FailCommandLineParse( "--suite could not open '%s'.", suitePath.c_str() );
        }

        Json suite = Json::parse( suiteFile, nullptr, false );
        if ( suite.is_discarded() )
        {
            return FailCommandLineParse( "--suite invalid JSON in '%s'.", suitePath.c_str() );
        }

        if ( !suite.is_object() )
        {
            return FailCommandLineParse( "--suite '%s' root must be an object.", suitePath.c_str() );
        }
        const auto formatIt = suite.find( "format" );
        if ( formatIt == suite.end() || !formatIt->is_string() ||
             formatIt->get<std::string>() != "skullbonez.suite.json" )
        {
            return FailCommandLineParse( "--suite '%s' must declare format skullbonez.suite.json.", suitePath.c_str() );
        }
        const auto scenesIt = suite.find( "scenes" );
        if ( scenesIt == suite.end() || !scenesIt->is_array() )
        {
            return FailCommandLineParse( "--suite '%s' must contain a scenes array.", suitePath.c_str() );
        }
        for ( const Json& scene : *scenesIt )
        {
            if ( !scene.is_string() )
            {
                return FailCommandLineParse( "--suite '%s' scenes entries must be strings.", suitePath.c_str() );
            }
            sceneList.push_back( ResolveSceneLaunchPath( scene.get<std::string>().c_str() ) );
        }
        isSuiteOrSceneMode = true;
    }
    else if ( sceneArg )
    {
        if ( IsOptionValueMissing( sceneArg ) )
        {
            return FailCommandLineParse( "--scene requires a path." );
        }

        if ( *sceneArg != '\0' )
        {
            // Support both quoted ("path with spaces") and unquoted tokens.
            // Quoted paths stop at the closing '"'; unquoted paths stop at whitespace.
            // This handles launchers (CDB, VS debugger) that wrap paths in quotes.
            sceneList.push_back( ResolveSceneLaunchPath( sceneArg ) );
            isSuiteOrSceneMode = true;
        }
    }

    if ( sceneList.empty() )
    {
        sceneList.push_back( "" ); // generated demo mode
    }
    return true;
}


// --vsync on|off patches the already-loaded startup config.


} // namespace Startup
} // namespace Runtime
} // namespace SkullbonezCore


namespace
{
bool HandleContactAudioSmoke( const ParsedArgs& args, const SkullbonezCore::Core::EngineConfig& cfg, int& outExitCode )
{
    if ( !args.contactAudioSmoke )
    {
        return false;
    }

    // Concept: this smoke path proves decode, voice submission, and counters
    // without creating a window, renderer, worker pool, or physics world.
    SkullbonezCore::Runtime::Audio::ContactAudioService audio;
    audio.SetMasterGain( cfg.contactAudio.masterGain );
    audio.SetMaxDistanceScale( cfg.contactAudio.maxDistanceScale );
    audio.SetRollingLevelDb( cfg.contactAudio.rollingLevelDb );
    audio.SetRollingMaxDistance( cfg.contactAudio.rollingMaxDistance );
    audio.SetRollingMinSlipSpeed( cfg.contactAudio.rollingMinSlipSpeed );
    audio.SetRollingVoicesPerWindow( static_cast<uint32_t>( cfg.contactAudio.rollingVoicesPerWindow ) );
    const bool initialized = audio.Initialize();
    const bool loaded = audio.LoadContactAudioMap( "SkullbonezData/audio/contact_audio.materials.json" );
    const bool submitted = initialized && loaded && audio.PlaySmokeImpact( HashStr( "earth" ), 6.0f );
    Sleep( 350 );
    const SkullbonezCore::Runtime::Audio::ContactAudioStats& stats = audio.Stats();
    CreateDirectoryA( "TestOutput", nullptr );
    FILE* report = nullptr;
    if ( fopen_s( &report, "TestOutput/contact_audio_smoke.json", "w" ) == 0 && report )
    {
        fprintf( report,
                 "{\n"
                 "  \"initialized\": %s,\n"
                 "  \"loaded\": %s,\n"
                 "  \"submitted\": %s,\n"
                 "  \"eventsSeen\": %u,\n"
                 "  \"rejectedByThreshold\": %u,\n"
                 "  \"rejectedByCooldown\": %u,\n"
                 "  \"submittedVoices\": %u,\n"
                 "  \"droppedVoices\": %u\n"
                 "}\n",
                 initialized ? "true" : "false",
                 loaded ? "true" : "false",
                 submitted ? "true" : "false",
                 stats.eventsSeen,
                 stats.rejectedByThreshold,
                 stats.rejectedByCooldown,
                 stats.submittedVoices,
                 stats.droppedVoices );
        fclose( report );
    }
    fprintf( stdout,
             "[audio-smoke] initialized=%d loaded=%d submitted=%d events=%u threshold=%u cooldown=%u voices=%u "
             "dropped=%u report=TestOutput/contact_audio_smoke.json\n",
             initialized ? 1 : 0,
             loaded ? 1 : 0,
             submitted ? 1 : 0,
             stats.eventsSeen,
             stats.rejectedByThreshold,
             stats.rejectedByCooldown,
             stats.submittedVoices,
             stats.droppedVoices );
    fflush( stdout );
    outExitCode = submitted ? 0 : 1;
    return true;
}


// Guards --physics-regression-log against use in non-Debug builds.
// False means startup should abort.


// Guards --physics-collision-time-log against use in non-Debug builds.
// False means startup should abort.


// Guards --physics-diag / --physics-diagnostics against use in non-Debug builds.
// Diagnostics traces are model-facing debug artifacts and are not a Profile/Release dependency.


// Guards the replay scrub SkullScope probe against use in non-Debug builds.

// Guards the replay restore hash SkullScope probe against use in non-Debug builds.

// Guards the replay v2 save probe against use in non-Debug builds.

// Guards the replay v2 load probe against use in non-Debug builds.

// Guards the saved replay checkpoint restore probe against use in non-Debug builds.

// Guards the saved replay checkpoint-plus-event target restore probe against use in non-Debug builds.

// Guards the saved replay checkpoint-plus-event branch-from-file probe against use in non-Debug builds.

// Guards the saved replay expected-failure probe against use without SkullScope diagnostics.


// ParsedArgs owns all command-line option state after this pass.
// Also loads engine.cfg and applies any overrides to the passed startup config.
// False means startup should abort, such as --physics-regression-log in Release.

// ---------------------------------------------------------------------------
// Render backend
// ---------------------------------------------------------------------------

SkullbonezCore::Core::SbResult InitRenderBackend( Window* window,
                                                  RuntimeRenderBackendView& renderBackendView,
                                                  std::unique_ptr<RenderBackendDX12>& outBackend )
{
    RuntimeAllocation::RuntimeAllocationScope allocationScope( RuntimeAllocation::RuntimeAllocationPhase::BackendInit );
    auto backend = std::make_unique<RenderBackendDX12>();
    RenderBackendDX12* renderBackend = backend.get();
    const SkullbonezCore::Core::SbResult renderInitResult = renderBackend->Init( window->NativeWindowHandle(),
                                                                                 window->NativeDeviceContext(),
                                                                                 window->ClientWidth(),
                                                                                 window->ClientHeight() );
    if ( !renderInitResult.ok )
    {
        // Lane R: render backend startup probes the host graphics environment.
        // Failures are reported at process bootstrap before any runtime borrows
        // are published into RuntimeRenderBackendView.
        renderBackendView = RuntimeRenderBackendView();
        return renderInitResult;
    }

    // Lifetime: the process bootstrap owns the backend unique_ptr. Runtime
    // render code keeps borrowed capability facets in RuntimeRenderBackendView
    // and must let them die before shutdown resets the owner.
    renderBackendView.deviceLifecycle = renderBackend;
    renderBackendView.renderCommands = renderBackend;
    renderBackendView.renderResources = renderBackend;
    renderBackendView.renderDiagnostics = renderBackend;
    renderBackendView.captureBackend = renderBackend;
    renderBackendView.rayTracingBackend = renderBackend;
    renderBackendView.shaderDevelopment = renderBackend;
    outBackend = std::move( backend );
    return SkullbonezCore::Core::SbResult::Success();
}

// ---------------------------------------------------------------------------
// Main run
// Run is scoped here so its destructor releases render-owned resources
// before the DX12 backend and the Win32 window are torn down.
// ---------------------------------------------------------------------------

RunStartupOverrides BuildRunStartupOverrides( const ParsedArgs& args )
{
    RunStartupOverrides overrides;
    RunLaunchOptions& launch = overrides.launch;

    launch.timeScaleOverride = args.timeScaleOverride;
    launch.fixedStep = args.fixedStep;
    launch.seedOverride = args.seedOverride;
    launch.noWater = args.noWater;
    launch.noSleep = args.noSleep;
    launch.noContactAudio = args.noContactAudio;
    launch.hasTornadoOverride = args.hasTornadoOverride;
    launch.tornadoEnabled = args.tornadoEnabled;
    launch.tornadoVectors = args.tornadoVectors;
    launch.hasCinematicRenderingOverride = args.hasCinematicRenderingOverride;
    launch.cinematicRendering = args.cinematicRendering;
    launch.hasCinematicShadowsOverride = args.hasCinematicShadowsOverride;
    launch.cinematicShadows = args.cinematicShadows;
    launch.demoHeroStyle = args.demoHeroStyle;
    launch.dumpTextureAssets = args.dumpAssets;
    launch.interactiveSceneRun = args.interactiveRun;
    launch.frameCountOverride = args.frameCountOverride;
    launch.uiStress = args.uiStress;
    launch.uiStressSeed = args.uiStressSeed;
    launch.uiStressActions = args.uiStressActions;
    launch.graphicsStress = args.graphicsStress;
    launch.graphicsStressSeed = args.graphicsStressSeed;
    launch.graphicsStressActions = args.graphicsStressActions;
    launch.graphicsStressSceneIntervalFrames = args.graphicsStressSceneIntervalFrames;
    launch.graphicsStressMemoryIntervalFrames = args.graphicsStressMemoryIntervalFrames;
    launch.allocationGuardMode = args.allocationGuardMode;
    launch.generatedObjectTypeOverride = args.objectTypeOverride;
    launch.hasPhysicsDebugFlagsOverride = args.hasPhysicsDebugFlagsOverride;
    launch.physicsDebugFlagsOverride = args.physicsDebugFlagsOverride;
    launch.hasPhysicsDebugTransparentOverride = args.hasPhysicsDebugTransparentOverride;
    launch.physicsDebugTransparentOverride = args.physicsDebugTransparentOverride;
    launch.hasPhysicsDebugAlphaOverride = args.hasPhysicsDebugAlphaOverride;
    launch.physicsDebugAlphaOverride = args.physicsDebugAlphaOverride;
    launch.hasPhysicsDebugContactLingerOverride = args.hasPhysicsDebugContactLingerOverride;
    launch.physicsDebugContactLingerOverride = args.physicsDebugContactLingerOverride;

    overrides.liveStyleControlDirectory = args.liveStyleControlDir[0] != '\0' ? args.liveStyleControlDir : nullptr;
    overrides.mainMemoryDumpPath = args.memoryDumpPath[0] != '\0' ? args.memoryDumpPath : nullptr;
    overrides.interactionScriptPath = args.interactionScriptPath[0] != '\0' ? args.interactionScriptPath : nullptr;
    overrides.interactionReportPath = args.interactionReportPath[0] != '\0' ? args.interactionReportPath : nullptr;

    const bool replayDefaultAllowed =
        !args.isSuiteOrSceneMode || args.interactiveRun || args.liveStyleControlDir[0] != '\0';
    const bool replayEnabled =
        args.replayExplicit ? args.replayRecording : ( args.replayRecording && replayDefaultAllowed );
    overrides.configureReplayRecording = replayEnabled || args.replayHashLogPath[0] != '\0';
    overrides.replayRecordingEnabled = true;
    overrides.replayRetentionSeconds = args.replaySeconds;
    overrides.replayHashLogPath = args.replayHashLogPath[0] != '\0' ? args.replayHashLogPath : nullptr;
    overrides.replayLoadPath = args.replayLoad ? args.replayLoadPath : nullptr;
    overrides.replayLoadProbe = args.replayLoadProbe;

    overrides.hasInitialOverlayMode = args.showProfiler;
    overrides.initialOverlayMode = args.showProfiler ? OverlayMode::Timers : OverlayMode::None;
    overrides.hideTopText = args.hideTopText;
    overrides.showBroadphaseVisualizer = args.showBroadphaseVisualizer;

#ifdef _DEBUG
    overrides.replayScrubProbe = args.replayScrubProbe;
    overrides.replayScrubProbeNormalized = args.replayScrubProbeNormalized;
    overrides.replayRestoreProbe = args.replayRestoreProbe;
    overrides.replayRestoreProbeNormalized = args.replayRestoreProbeNormalized;
    overrides.replaySaveProbe = args.replaySaveProbe;
    overrides.replaySaveProbePath = args.replaySaveProbe ? args.replaySaveProbePath : nullptr;
    overrides.replayRestoreFileProbePath = args.replayRestoreFileProbe ? args.replayRestoreFileProbePath : nullptr;
    overrides.replayRestoreTargetFileProbePath =
        args.replayRestoreTargetFileProbe ? args.replayRestoreTargetFileProbePath : nullptr;
    overrides.replayRestoreBranchFileProbePath =
        args.replayRestoreBranchFileProbe ? args.replayRestoreBranchFileProbePath : nullptr;
    overrides.replayRestoreFailureFileProbePath =
        args.replayRestoreFailureFileProbe ? args.replayRestoreFailureFileProbePath : nullptr;
    overrides.physicsRegressionLogPath =
        args.physicsRegressionLogOverride[0] != '\0' ? args.physicsRegressionLogOverride : nullptr;
    overrides.physicsCollisionTimeLogPath =
        args.physicsCollisionTimeLogOverride[0] != '\0' ? args.physicsCollisionTimeLogOverride : nullptr;
    overrides.physicsDiagnosticsPath = args.physicsDiagnosticsPath[0] != '\0' ? args.physicsDiagnosticsPath : nullptr;
    overrides.physicsDiagnosticsFixedStepForced = args.fixedStepForcedByPhysicsDiagnostics;
#endif

    return overrides;
}


// ---------------------------------------------------------------------------

int RunApp( Window* window,
            ParsedArgs& args,
            SkullbonezCore::Core::EngineConfig& cfg,
            WorkerPool& workerPool,
            SkullbonezCore::Core::Profiler* profiler,
            RuntimeRenderBackendView renderBackendView )
{
    {
        std::unique_ptr<Run> cRun =
            std::make_unique<Run>( *window, std::move( args.sceneList ), cfg, workerPool, profiler, renderBackendView );
#if defined( SKULLBONEZ_PROFILE_ENABLED )
        struct ProfilerRenderDiagnosticsLifetime
        {
            SkullbonezCore::Core::Profiler* profiler = nullptr;
            ~ProfilerRenderDiagnosticsLifetime()
            {
                if ( profiler )
                {
                    profiler->BindRenderDiagnostics( nullptr );
                }
            }
        };
        // Lifetime: this guard is declared after cRun, so it clears SkullbonezCore::Core::Profiler's
        // renderer-diagnostics borrow before Run's destructor releases
        // backend-owned resources through the still-live DX12 backend.
        ProfilerRenderDiagnosticsLifetime profilerRenderDiagnosticsLifetime{ profiler };
#endif
        const RunStartupOverrides startupOverrides = BuildRunStartupOverrides( args );
        auto reportRunResult = [&]( const SkullbonezCore::Core::SbResult& result ) -> int
        {
            const char* safeOwner =
                result.error.owner && result.error.owner[0] != '\0' ? result.error.owner : "Runtime";
            const char* safeMessage =
                result.error.message[0] != '\0' ? result.error.message : "recoverable runtime operation failed";
            SkullbonezCore::Core::Log().WriteEventf( "recoverable_failure owner=\"%s\" message=\"%s\"",
                                                     safeOwner,
                                                     safeMessage );
            fprintf( stderr, "[runtime] Recoverable failure owner=%s reason=\"%s\"\n", safeOwner, safeMessage );
            fflush( stderr );
            SkullbonezCore::Core::Log().FlushAll();
            if ( !args.isSuiteOrSceneMode && !args.suppressExitDialog )
            {
                window->MsgBox( safeMessage, "Runtime Failure", MB_OK );
            }
            return 1;
        };
        auto reportInteractionAutomationResult = [&]( const SkullbonezCore::Core::SbResult& result ) -> int
        {
            const char* safeMessage =
                result.error.message[0] != '\0' ? result.error.message : "interaction automation failed";
            SkullbonezCore::Core::Log().WriteEventf( "interaction_automation_failed message=\"%s\"", safeMessage );
            fprintf( stderr, "[interaction] Automation failed: %s\n", safeMessage );
            fflush( stderr );
            SkullbonezCore::Core::Log().FlushAll();
            if ( !args.isSuiteOrSceneMode && !args.suppressExitDialog )
            {
                window->MsgBox( safeMessage, "Interaction Automation Failed", MB_OK );
            }
            return 1;
        };

        const SkullbonezCore::Core::SbResult startupResult = cRun->ApplyStartupOverrides( startupOverrides );
        if ( !startupResult.ok )
        {
            return reportInteractionAutomationResult( startupResult );
        }

        cRun->Initialise();
        if ( !cRun->LastSceneLoadResult().ok )
        {
            return reportRunResult( cRun->LastSceneLoadResult() );
        }
        if ( args.sceneLoadOnly )
        {
            const SkullbonezCore::Core::SbResult sceneLoadOnlyResult =
                cRun->RunSceneLoadOnly( args.sceneSnapshotOutPath[0] != '\0' ? args.sceneSnapshotOutPath : nullptr );
            if ( !sceneLoadOnlyResult.ok )
            {
                return reportRunResult( sceneLoadOnlyResult );
            }
        }
        else
        {
            const SkullbonezCore::Core::SbResult executeResult = cRun->Execute();
            if ( !executeResult.ok )
            {
                if ( executeResult.error.owner && strcmp( executeResult.error.owner, "InteractionAutomation" ) == 0 )
                {
                    return reportInteractionAutomationResult( executeResult );
                }
                return reportRunResult( executeResult );
            }
            if ( args.graphicsStress )
            {
                printf( "[graphics-stress] Execute returned.\n" );
                fflush( stdout );
            }
        }

        if ( !args.isSuiteOrSceneMode && !args.suppressExitDialog )
        {
            window->MsgBox( "Thanks for using the Skullbonez Core!", "Alert!", MB_OK );
        }
    } // cRun destroyed here before backend/window cleanup
    return 0;
}

// ---------------------------------------------------------------------------
// Cleanup
// ---------------------------------------------------------------------------

void CleanupWindow( Window* window, HINSTANCE hInstance, std::unique_ptr<RenderBackendDX12>& renderBackend )
{
    // Lifetime: disarm callback-fed input queues while the HWND still names
    // the window that WndProc used, before backend/window class teardown.
    const HWND windowHandle = window->NativeWindowHandle();
    if ( windowHandle )
    {
        Input::UnbindCallbackBridge( windowHandle );
    }
    Input::UnbindWindow( *window );
    window->SetResizeRenderLifecycle( nullptr );
    renderBackend.reset();

    window->ReleaseDeviceContext();

    if ( window->IsFullScreenMode() )
    {
        ChangeDisplaySettings( nullptr, 0 ); // Restore desktop mode
        Input::SetSystemCursorVisible( true );
    }

    UnregisterClass( WINDOW_NAME, hInstance );
}

} // anonymous namespace


// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

int WINAPI WinMain( HINSTANCE hInstance, HINSTANCE hPrevInstance, PSTR szCmdLine, int iCmdShow )
{
    // Heap debug code - breaks program at specified allocation
    // _CrtSetBreakAlloc(89);

    // Floating point check routine
    // _controlfp(0, _MCW_EM ^ _EM_INEXACT);

    hPrevInstance;
    iCmdShow;

    const CommandLineView commandLine = TokenizeCommandLine( szCmdLine );

#ifdef _DEBUG
    InstallDebugCrashLogger();
    SkullbonezCore::Core::Log().WriteEventf( "process_started command_line=\"%s\"", szCmdLine ? szCmdLine : "" );
    if ( HasOption( commandLine, "--debug-crash-test" ) )
    {
        SkullbonezCore::Core::Log().WriteEventf( "debug_crash_test_requested" );
        volatile int* crashAddress = nullptr;
        *crashAddress = 1;
    }
#endif

    // Initialize COM on the main thread (multi-threaded apartment). Required before any
    // WinRT/COM activation occurs — without this, MSCTF.dll throws 0x800401F0 during
    // text/input service initialization triggered by window creation.
    CoInitializeEx( nullptr, COINIT_MULTITHREADED );

    AttachParentConsole();

    int atlasExitCode = 0;
    if ( HandleGenAtlas( commandLine, atlasExitCode ) )
    {
        return atlasExitCode;
    }

    SkullbonezCore::Core::EngineConfig cfg;

    ParsedArgs args;
    if ( !ParseCommandLine( commandLine, cfg, args ) )
    {
        const char* error = GetCommandLineError();
        fprintf( stderr, "FATAL: %s\n", error );
        MessageBoxA( nullptr, error, "Command line parse failed", MB_OK | MB_ICONERROR );
        CoUninitialize();
        return 1;
    }
    RuntimeAllocation::SetRuntimeAllocationGuardMode( args.allocationGuardMode );
    if ( RuntimeAllocation::RuntimeAllocationGuardEnabled() )
    {
        fprintf( stdout,
                 "[allocation-guard] Enabled mode=%s. Startup, scene, backend, gameplay, replay, capture, and shutdown "
                 "allocations will be summarized at process end.\n",
                 RuntimeAllocation::RuntimeAllocationGuardModeName( args.allocationGuardMode ) );
    }

    int contactAudioSmokeExitCode = 0;
    if ( HandleContactAudioSmoke( args, cfg, contactAudioSmokeExitCode ) )
    {
        CoUninitialize();
        return contactAudioSmokeExitCode;
    }

    int standalonePhysicsExitCode = 0;
    if ( HandlePhysicsStandaloneSmoke( commandLine, standalonePhysicsExitCode ) )
    {
        CoUninitialize();
        return standalonePhysicsExitCode;
    }

    WorkerPool workerPool;
    workerPool.Initialise( cfg.runtimeCapacity.workerThreads );
    if ( args.workerSelfTest )
    {
        const bool workersOk = RunWorkerSystemSelfTest( workerPool, stdout );
        workerPool.Shutdown();
        CoUninitialize();
        return workersOk ? 0 : 1;
    }

    Window windowOwner;
    Window* window = &windowOwner;
    window->SetStartupWindowSize( cfg.window.screenX, cfg.window.screenY );
    window->SetProjectionFrustum( cfg.camera.frustumNear, cfg.camera.frustumFar );
    const SkullbonezCore::Core::SbResult windowResult =
        window->CreateAppWindow( hInstance, cfg.window.fullscreen, !args.automationWindowHidden );
    if ( !windowResult.ok )
    {
        ReportStartupFailure( windowResult, "SkullbonezCore Startup Failed" );
        workerPool.Shutdown();
        CoUninitialize();
        return 1;
    }
    window->AcquireDeviceContext();

    RuntimeRenderBackendView renderBackendView;
    std::unique_ptr<RenderBackendDX12> renderBackend;
    const SkullbonezCore::Core::SbResult renderBackendResult =
        InitRenderBackend( window, renderBackendView, renderBackend );
    if ( !renderBackendResult.ok )
    {
        ReportStartupFailure( renderBackendResult, "SkullbonezCore Renderer Startup Failed" );
        workerPool.Shutdown();
        CleanupWindow( window, hInstance, renderBackend );
        CoUninitialize();
        return 1;
    }
    window->SetResizeRenderLifecycle( renderBackendView.deviceLifecycle );
    const SkullbonezCore::Core::SbResult initialResizeResult = window->HandleScreenResize();
    if ( !initialResizeResult.ok )
    {
        ReportStartupFailure( initialResizeResult, "SkullbonezCore Renderer Startup Failed" );
        workerPool.Shutdown();
        CleanupWindow( window, hInstance, renderBackend );
        CoUninitialize();
        return 1;
    }

    SkullbonezCore::Core::Profiler* profiler = nullptr;
#if defined( SKULLBONEZ_PROFILE_ENABLED )
    // Why: SkullbonezCore::Core::Profiler remains the sanctioned diagnostics singleton, but runtime
    // owners receive this startup borrow instead of resolving it mid-frame.
    profiler = &SkullbonezCore::Core::Profiler::Instance();
    profiler->BindRenderDiagnostics( renderBackendView.renderDiagnostics );
#endif

    const int runExitCode = RunApp( window, args, cfg, workerPool, profiler, renderBackendView );

    {
        RuntimeAllocation::RuntimeAllocationScope allocationScope(
            RuntimeAllocation::RuntimeAllocationPhase::Shutdown );
#if defined( SKULLBONEZ_PROFILE_ENABLED )
        profiler->BindRenderDiagnostics( nullptr );
#endif
        workerPool.Shutdown();
        CleanupWindow( window, hInstance, renderBackend );
    }
    RuntimeAllocation::PrintRuntimeAllocationSummary( stdout );
    int finalExitCode = runExitCode;
    if ( RuntimeAllocation::GetRuntimeAllocationGuardMode() ==
             RuntimeAllocation::RuntimeAllocationGuardMode::Gameplay &&
         RuntimeAllocation::RuntimeAllocationGuardHasGameplayViolations() && finalExitCode == 0 )
    {
        fprintf( stdout, "[allocation-guard] FAIL: gameplay allocation guard detected policy violations.\n" );
        finalExitCode = 9;
    }

    CoUninitialize();

    // Write memory leaks to output window
    // _CrtDumpMemoryLeaks();

    return finalExitCode;
}
