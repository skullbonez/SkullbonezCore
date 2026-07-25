/*
File: SkullbonezSource/Scene/AuthoredScene.cpp
Purpose:
  Stores parsed authored-scene JSON and applies it to runtime scene state.

Summary:
  AuthoredScene.cpp stores parsed authored-scene JSON and applies it to runtime scene
  state. As an implementation unit, keep edits anchored on scene-file parsing
  or snapshot contracts and on the glossary/invariants below.

Glossary:
  Scene collection: Vector-backed parsed scene array for cameras, bodies,
    constraints, or validation expectations that authored setup later replays
    into runtime owners.
  Lane F fatal: Should-never-happen caller/scene-state invariant reported with
    owner diagnostics before process termination.
  Lane R result: Recoverable load outcome carrying owner/message diagnostics
    for authored scene/style data failures.
  Schema version: Validated scene-file contract version retained with the
    parsed record so later save/load owners can choose the matching shape.

Invariants:
  - Command-line and scene JSON spellings are user-facing compatibility
    surface.
  - Scene collection getters require indices produced from the matching count
    API; out-of-range access means authored setup and parsed scene state have
    diverged.

Related:
  - SkullbonezSource/Scene/AuthoredScene.h
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "AuthoredScene.h"

#include "../Core/FatalError.h"

using namespace SkullbonezCore::Runtime;

namespace
{
[[noreturn]] void
FatalSceneIndexOutOfRange( const char* collectionName, const char* functionName, int index, int count )
{
    // Concept: parsed scene getters are Lane F once parsing has succeeded.
    //
    // Scene JSON syntax/data failures are Lane R at TryLoadFromFile, but an
    // internal caller asking for an index outside the paired count is a scene
    // setup invariant failure. Fatal diagnostics keep the owner and collection
    // visible without unwinding through runtime setup.
    SB_FATAL(
        "AuthoredScene",
        "%s index out of range in %s. index=%d count=%d",
        collectionName,
        functionName,
        index,
        count
    );
}


SkullbonezCore::Core::SbResult TryLoadSceneFile(
    const char* path,
    SkullbonezCore::Assets::AssetContext assets,
    bool styleOnly,
    AuthoredScene& outScene
)
{
    return styleOnly ? TryLoadStyleSceneFromFileImpl( path, assets, outScene )
                     : TryLoadAuthoredSceneFromFileImpl( path, assets, outScene );
}
} // namespace


AuthoredScene::AuthoredScene()
{
}


AuthoredScene AuthoredScene::LoadFromFile( const char* path )
{
    return LoadAuthoredSceneFromFileImpl( path, Assets::AssetContext {} );
}


SkullbonezCore::Core::SbResult AuthoredScene::TryLoadFromFile( const char* path, AuthoredScene& outScene )
{
    return TryLoadSceneFile( path, Assets::AssetContext {}, false, outScene );
}


AuthoredScene AuthoredScene::LoadFromFile( const char* path, const Assets::AssetSystem& assets )
{
    return LoadAuthoredSceneFromFileImpl( path, Assets::AssetContext { &assets } );
}


SkullbonezCore::Core::SbResult
AuthoredScene::TryLoadFromFile( const char* path, const Assets::AssetSystem& assets, AuthoredScene& outScene )
{
    return TryLoadSceneFile( path, Assets::AssetContext { &assets }, false, outScene );
}


AuthoredScene AuthoredScene::LoadStyleFromFile( const char* path )
{
    return LoadStyleSceneFromFileImpl( path, Assets::AssetContext {} );
}


SkullbonezCore::Core::SbResult AuthoredScene::TryLoadStyleFromFile( const char* path, AuthoredScene& outScene )
{
    return TryLoadSceneFile( path, Assets::AssetContext {}, true, outScene );
}


AuthoredScene AuthoredScene::LoadStyleFromFile( const char* path, const Assets::AssetSystem& assets )
{
    return LoadStyleSceneFromFileImpl( path, Assets::AssetContext { &assets } );
}


SkullbonezCore::Core::SbResult
AuthoredScene::TryLoadStyleFromFile( const char* path, const Assets::AssetSystem& assets, AuthoredScene& outScene )
{
    return TryLoadSceneFile( path, Assets::AssetContext { &assets }, true, outScene );
}


bool AuthoredScene::IsPhysicsEnabled() const
{
    return m_sceneOptions.isPhysicsEnabled;
}


bool AuthoredScene::IsTextEnabled() const
{
    return m_sceneOptions.isTextEnabled;
}


bool AuthoredScene::IsTextOnly() const
{
    return m_sceneOptions.isTextOnly;
}


bool AuthoredScene::IsWaterHidden() const
{
    return m_sceneOptions.waterHidden;
}


bool AuthoredScene::IsTerrainHidden() const
{
    return m_sceneOptions.terrainHidden;
}


bool AuthoredScene::IsEditableScene() const
{
    return m_sceneOptions.editableScene;
}


bool AuthoredScene::HasCinematicRenderingOverride() const
{
    return m_sceneOptions.hasCinematicRenderingOverride;
}


bool AuthoredScene::IsCinematicRenderingEnabled() const
{
    return m_sceneOptions.cinematicRendering;
}


bool AuthoredScene::HasCinematicExposure() const
{
    return m_sceneOptions.hasCinematicExposure;
}


float AuthoredScene::GetCinematicExposure() const
{
    return m_sceneOptions.cinematicExposure;
}


bool AuthoredScene::HasCinematicGamma() const
{
    return m_sceneOptions.hasCinematicGamma;
}


float AuthoredScene::GetCinematicGamma() const
{
    return m_sceneOptions.cinematicGamma;
}


uint64_t AuthoredScene::GetCinematicOverrideMask() const
{
    return m_sceneOptions.cinematicOverrideMask;
}


const SkullbonezCore::Core::CinematicRenderConfig& AuthoredScene::GetCinematicRenderConfig() const
{
    return m_sceneOptions.cinematicRender;
}


int AuthoredScene::GetFrameCount() const
{
    return m_sceneOptions.frameCount;
}


const char* AuthoredScene::GetScreenshotPath() const
{
    return m_captureOptions.screenshotPath;
}


int AuthoredScene::GetScreenshotFrame() const
{
    return m_captureOptions.screenshotFrame;
}


int AuthoredScene::GetScreenshotMs() const
{
    return m_captureOptions.screenshotMs;
}


unsigned int AuthoredScene::GetSeed() const
{
    return m_sceneOptions.seed;
}


int AuthoredScene::GetSolverBallCount() const
{
    return m_sceneOptions.solverBallCount;
}


int AuthoredScene::GetSolverBoxCount() const
{
    return m_sceneOptions.solverBoxCount;
}


bool AuthoredScene::HasModelCapacityOverride() const
{
    return m_sceneOptions.modelCapacity > 0;
}


int AuthoredScene::GetModelCapacity() const
{
    return m_sceneOptions.modelCapacity;
}


bool AuthoredScene::HasWorkerThreadOverride() const
{
    return m_sceneOptions.workerThreads >= -1;
}


int AuthoredScene::GetWorkerThreads() const
{
    return m_sceneOptions.workerThreads;
}


const char* AuthoredScene::GetPerfLogPath() const
{
    return m_loggingOptions.perfLogPath;
}


bool AuthoredScene::IsPerfLogFlushEnabled() const
{
    return m_loggingOptions.isPerfLogFlush;
}


int AuthoredScene::GetPerfLogFlushInterval() const
{
    return m_loggingOptions.perfLogFlushInterval;
}

bool AuthoredScene::HasVsyncOverride() const
{
    return m_runtimeOverrides.hasVsyncOverride;
}


bool AuthoredScene::IsVsyncEnabled() const
{
    return m_runtimeOverrides.isVsyncEnabled;
}


bool AuthoredScene::HasPipelineSyncOverride() const
{
    return m_runtimeOverrides.hasPipelineSyncOverride;
}


bool AuthoredScene::IsPipelineSyncEnabled() const
{
    return m_runtimeOverrides.isPipelineSyncEnabled;
}


int AuthoredScene::GetScreenshotInterval() const
{
    return m_captureOptions.screenshotInterval;
}


const char* AuthoredScene::GetScreenshotDir() const
{
    return m_captureOptions.screenshotDir;
}


int AuthoredScene::GetCameraCount() const
{
    return static_cast<int>( m_cameras.size() );
}


uint32_t AuthoredScene::GetSchemaVersion() const
{
    return m_schemaVersion;
}


float AuthoredScene::GetTimeScale() const
{
    return m_sceneOptions.timeScale;
}


bool AuthoredScene::IsFixedStep() const
{
    return m_sceneOptions.isFixedStep;
}


bool AuthoredScene::ShouldPauseSnapshotState() const
{
    return m_sceneOptions.pauseSnapshotState;
}


uint32_t AuthoredScene::GetPhysicsDebugFlags() const
{
    return m_sceneOptions.physicsDebugFlags;
}


bool AuthoredScene::IsPhysicsDebugTransparent() const
{
    return m_sceneOptions.physicsDebugTransparent;
}


float AuthoredScene::GetPhysicsDebugAlpha() const
{
    return m_sceneOptions.physicsDebugAlpha;
}


float AuthoredScene::GetPhysicsDebugContactLinger() const
{
    return m_sceneOptions.physicsDebugContactLinger;
}


float AuthoredScene::GetTrackHeight() const
{
    return m_sceneOptions.trackHeight;
}


float AuthoredScene::GetAutoCycleInterval() const
{
    return m_sceneOptions.autoCycleInterval;
}


bool AuthoredScene::IsScreenshotAndExit() const
{
    return m_sceneOptions.screenshotAndExit;
}


bool AuthoredScene::IsExitOnComplete() const
{
    return m_sceneOptions.exitOnComplete;
}


bool AuthoredScene::IsCollisionVisualizerEnabled() const
{
    return m_sceneOptions.collisionVisualizer;
}


bool AuthoredScene::IsBroadphaseOverlayEnabled() const
{
    return m_sceneOptions.broadphaseOverlay;
}


bool AuthoredScene::IsWaterFreezeDebugEnabled() const
{
    return m_sceneOptions.waterFreezeDebug;
}


bool AuthoredScene::IsWaterFlatDebugEnabled() const
{
    return m_sceneOptions.waterFlatDebug;
}


int AuthoredScene::GetWaterReflectionMode() const
{
    return m_sceneOptions.waterReflectionMode;
}


bool AuthoredScene::HasFlatSlope() const
{
    return m_terrainOverride.hasFlatSlope;
}


float AuthoredScene::GetFlatBaseY() const
{
    return m_terrainOverride.flatBaseY;
}


float AuthoredScene::GetFlatSlopeX() const
{
    return m_terrainOverride.flatSlopeX;
}


float AuthoredScene::GetFlatSlopeZ() const
{
    return m_terrainOverride.flatSlopeZ;
}


int AuthoredScene::GetBallCount() const
{
    return static_cast<int>( m_balls.size() );
}


const SceneCamera& AuthoredScene::GetCamera( int index ) const
{
    if ( index < 0 || index >= static_cast<int>( m_cameras.size() ) )
    {
        FatalSceneIndexOutOfRange( "Camera", "AuthoredScene::GetCamera", index, static_cast<int>( m_cameras.size() ) );
    }

    return m_cameras[index];
}


const SceneBall& AuthoredScene::GetBall( int index ) const
{
    if ( index < 0 || index >= static_cast<int>( m_balls.size() ) )
    {
        FatalSceneIndexOutOfRange( "Ball", "AuthoredScene::GetBall", index, static_cast<int>( m_balls.size() ) );
    }

    return m_balls[index];
}


int AuthoredScene::GetBallStateCount() const
{
    return static_cast<int>( m_ballStates.size() );
}


const SceneBallState& AuthoredScene::GetBallState( int index ) const
{
    if ( index < 0 || index >= static_cast<int>( m_ballStates.size() ) )
    {
        FatalSceneIndexOutOfRange(
            "BallState",
            "AuthoredScene::GetBallState",
            index,
            static_cast<int>( m_ballStates.size() )
        );
    }

    return m_ballStates[index];
}


int AuthoredScene::GetBoxStateCount() const
{
    return static_cast<int>( m_boxStates.size() );
}


const SceneBoxState& AuthoredScene::GetBoxState( int index ) const
{
    if ( index < 0 || index >= static_cast<int>( m_boxStates.size() ) )
    {
        FatalSceneIndexOutOfRange(
            "BoxState",
            "AuthoredScene::GetBoxState",
            index,
            static_cast<int>( m_boxStates.size() )
        );
    }

    return m_boxStates[index];
}


int AuthoredScene::GetBoxCount() const
{
    return static_cast<int>( m_boxes.size() );
}


const SceneBox& AuthoredScene::GetBox( int index ) const
{
    if ( index < 0 || index >= static_cast<int>( m_boxes.size() ) )
    {
        FatalSceneIndexOutOfRange( "Box", "AuthoredScene::GetBox", index, static_cast<int>( m_boxes.size() ) );
    }

    return m_boxes[index];
}


int AuthoredScene::GetConvexHullCount() const
{
    return static_cast<int>( m_convexHulls.size() );
}


const SceneConvexHull& AuthoredScene::GetConvexHull( int index ) const
{
    if ( index < 0 || index >= static_cast<int>( m_convexHulls.size() ) )
    {
        FatalSceneIndexOutOfRange(
            "ConvexHull",
            "AuthoredScene::GetConvexHull",
            index,
            static_cast<int>( m_convexHulls.size() )
        );
    }

    return m_convexHulls[index];
}


int AuthoredScene::GetConvexHullStateCount() const
{
    return static_cast<int>( m_convexHullStates.size() );
}


const SceneConvexHullState& AuthoredScene::GetConvexHullState( int index ) const
{
    if ( index < 0 || index >= static_cast<int>( m_convexHullStates.size() ) )
    {
        FatalSceneIndexOutOfRange(
            "ConvexHullState",
            "AuthoredScene::GetConvexHullState",
            index,
            static_cast<int>( m_convexHullStates.size() )
        );
    }

    return m_convexHullStates[index];
}


int AuthoredScene::GetRagdollCount() const
{
    return static_cast<int>( m_ragdolls.size() );
}


const SceneRagdoll& AuthoredScene::GetRagdoll( int index ) const
{
    if ( index < 0 || index >= static_cast<int>( m_ragdolls.size() ) )
    {
        FatalSceneIndexOutOfRange(
            "Ragdoll",
            "AuthoredScene::GetRagdoll",
            index,
            static_cast<int>( m_ragdolls.size() )
        );
    }

    return m_ragdolls[index];
}


int AuthoredScene::GetPointJointConstraintCount() const
{
    return static_cast<int>( m_pointJointConstraints.size() );
}


const ScenePointJointConstraint& AuthoredScene::GetPointJointConstraint( int index ) const
{
    if ( index < 0 || index >= static_cast<int>( m_pointJointConstraints.size() ) )
    {
        FatalSceneIndexOutOfRange(
            "PointJointConstraint",
            "AuthoredScene::GetPointJointConstraint",
            index,
            static_cast<int>( m_pointJointConstraints.size() )
        );
    }

    return m_pointJointConstraints[index];
}


int AuthoredScene::GetRequiredContactCount() const
{
    return static_cast<int>( m_requiredContacts.size() );
}


const SceneRequiredContact& AuthoredScene::GetRequiredContact( int index ) const
{
    if ( index < 0 || index >= static_cast<int>( m_requiredContacts.size() ) )
    {
        FatalSceneIndexOutOfRange(
            "RequiredContact",
            "AuthoredScene::GetRequiredContact",
            index,
            static_cast<int>( m_requiredContacts.size() )
        );
    }

    return m_requiredContacts[index];
}


int AuthoredScene::GetRequiredBroadphaseXCellCount() const
{
    return static_cast<int>( m_requiredBroadphaseXCells.size() );
}


const SceneRequiredBroadphaseXCells& AuthoredScene::GetRequiredBroadphaseXCell( int index ) const
{
    if ( index < 0 || index >= static_cast<int>( m_requiredBroadphaseXCells.size() ) )
    {
        FatalSceneIndexOutOfRange(
            "RequiredBroadphaseXCell",
            "AuthoredScene::GetRequiredBroadphaseXCell",
            index,
            static_cast<int>( m_requiredBroadphaseXCells.size() )
        );
    }

    return m_requiredBroadphaseXCells[index];
}


int AuthoredScene::GetAssetLibraryCount() const
{
    return static_cast<int>( m_assetLibraries.size() );
}


const SceneAssetLibraryRef& AuthoredScene::GetAssetLibrary( int index ) const
{
    if ( index < 0 || index >= static_cast<int>( m_assetLibraries.size() ) )
    {
        FatalSceneIndexOutOfRange(
            "AssetLibrary",
            "AuthoredScene::GetAssetLibrary",
            index,
            static_cast<int>( m_assetLibraries.size() )
        );
    }

    return m_assetLibraries[index];
}


int AuthoredScene::GetAssetInstanceCount() const
{
    return static_cast<int>( m_assetInstances.size() );
}


const SceneAssetInstanceRecord& AuthoredScene::GetAssetInstance( int index ) const
{
    if ( index < 0 || index >= static_cast<int>( m_assetInstances.size() ) )
    {
        FatalSceneIndexOutOfRange(
            "AssetInstance",
            "AuthoredScene::GetAssetInstance",
            index,
            static_cast<int>( m_assetInstances.size() )
        );
    }

    return m_assetInstances[index];
}


int AuthoredScene::GetAssetPartCount() const
{
    return static_cast<int>( m_assetParts.size() );
}


const SceneAssetPartRef& AuthoredScene::GetAssetPart( int index ) const
{
    if ( index < 0 || index >= static_cast<int>( m_assetParts.size() ) )
    {
        FatalSceneIndexOutOfRange(
            "AssetPart",
            "AuthoredScene::GetAssetPart",
            index,
            static_cast<int>( m_assetParts.size() )
        );
    }

    return m_assetParts[index];
}


int AuthoredScene::GetObjectMaterialOverrideCount() const
{
    return static_cast<int>( m_objectMaterials.size() );
}


const SceneObjectMaterialOverride& AuthoredScene::GetObjectMaterialOverride( int index ) const
{
    if ( index < 0 || index >= static_cast<int>( m_objectMaterials.size() ) )
    {
        FatalSceneIndexOutOfRange(
            "ObjectMaterialOverride",
            "AuthoredScene::GetObjectMaterialOverride",
            index,
            static_cast<int>( m_objectMaterials.size() )
        );
    }

    return m_objectMaterials[index];
}


bool AuthoredScene::HasWorldOverride() const
{
    return m_worldOverride.hasWorldOverride;
}


float AuthoredScene::GetWorldGravity() const
{
    return m_worldOverride.worldGravity;
}


float AuthoredScene::GetWorldFluidHeight() const
{
    return m_worldOverride.worldFluidHeight;
}


float AuthoredScene::GetWorldFluidDensity() const
{
    return m_worldOverride.worldFluidDensity;
}


const SkullbonezCore::Physics::MutualGravitySettings& AuthoredScene::GetWorldMutualGravitySettings() const
{
    return m_worldOverride.mutualGravity;
}


bool AuthoredScene::HasMutualGravityEnabled() const
{
    return m_worldOverride.mutualGravity.enabled;
}


bool AuthoredScene::HasTornadoSystem() const
{
    return m_tornadoSystem.hasTornadoSystem;
}


const AuthoredTornadoSystemConfig& AuthoredScene::GetTornadoSystemConfig() const
{
    return m_tornadoSystem.config;
}


const SceneUIOptions& AuthoredScene::GetUIOptions() const
{
    return m_UIOptions;
}
