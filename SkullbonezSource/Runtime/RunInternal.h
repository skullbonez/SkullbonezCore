/*
File: SkullbonezSource/Runtime/RunInternal.h
Purpose:
  Shares private run-loop data structures between split runtime implementation files.

Mental model:
  RunInternal.h shares private run-loop data structures between split runtime
  implementation files. As a public header, keep edits anchored on local owner
  boundaries and call direction and on the glossary/invariants below.

Glossary:
  HUD (Heads-Up Display): On-screen diagnostics and control overlay.
  CLI (Command-Line Interface): Text arguments or scripts used to launch
  validation and tooling paths.
  Scrubber: Replay timeline control that lets the operator inspect stored
  solver or presentation samples.
  Hot zone: Screen-space rectangle that wakes hidden UI controls when hovered.

Invariants:
  - Types in this header are private to split Run implementation files; public
    runtime contracts belong in narrower headers.
  - Inline helpers may synchronize borrowed subsystem state, but they do not
    own renderer, physics, UI, or replay lifetimes.

Related:
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

#include "Run.h"
#include "../Rendering/Helper.h"
#include "../Physics/BoundingSphere.h"
#include "../Physics/PhysicsWorldForces.h"
#include "../GameObjects/GameModel.h"
#include "../Core/Profiler.h"
#include "../Rendering/IRenderCommandContext.h"
#include "../Rendering/IRenderDeviceLifecycle.h"
#include "../Rendering/IRenderDiagnostics.h"
#include "../Rendering/IRenderResourceFactory.h"
#include "../World/TerrainSupportClassifier.h"
#include "../UI/UIDraw.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <psapi.h>
#include <string>
#include <time.h>
#include <vector>

using SkullbonezCore::Basics::CinematicRenderConfig;
using SkullbonezCore::Environment::Camera;
using SkullbonezCore::Environment::CameraCollection;
using SkullbonezCore::Environment::WorldEnvironment;
using SkullbonezCore::GameObjects::GameModel;
using SkullbonezCore::Geometry::SkyBox;
using SkullbonezCore::Geometry::Terrain;
using SkullbonezCore::Geometry::XZBounds;
using SkullbonezCore::Hardware::Input;
using SkullbonezCore::Hardware::InputState;
using SkullbonezCore::Math::CollisionDetection::BoundingBox;
using SkullbonezCore::Math::CollisionDetection::BoundingSphere;
using SkullbonezCore::Math::Orientation::Quaternion;
using SkullbonezCore::Math::Transformation::Matrix4;
using SkullbonezCore::Math::Vector::Vector3;
using SkullbonezCore::Physics::PhysicsPipelineStage;
using SkullbonezCore::Physics::PhysicsPipelineStageName;
using SkullbonezCore::Rendering::IMesh;
using SkullbonezCore::Text::Text2d;
using SkullbonezCore::Textures::TextureCollection;
using SkullbonezCore::UI::InGameUICommands;
using SkullbonezCore::UI::InGameUIFrameData;
using SkullbonezCore::UI::InGameUIInputResult;
using SkullbonezCore::UI::InGameUITab;
using SkullbonezCore::UI::UICinematicFeature;
using SkullbonezCore::UI::UICinematicParam;
using SkullbonezCore::UI::UIRenderParam;
using SkullbonezCore::UI::UISoundBandParam;
using SkullbonezCore::UI::UISoundParam;

namespace SkullbonezCore
{
namespace Basics
{
namespace RunInternal
{
inline constexpr double PERF_TEST_PASS_SECONDS = 2.0;      // Duration of each perf pass before averaging/reporting.
inline constexpr float WATER_HEIGHT_CONTROL_SPEED = 20.0f; // UI water-height slider speed in world meters per second.
inline constexpr float NO_WATER_TERRAIN_CLEARANCE = 100.0f;
inline constexpr float CAMERA_MOUSE_REFERENCE_DT = 1.0f / 60.0f;
inline constexpr long CAMERA_MOUSE_MAX_DELTA_PIXELS = 96;
inline constexpr long CAMERA_MOUSE_SPIKE_DELTA_PIXELS = 320;
inline constexpr double REPLAY_PREDICTION_REFRESH_SECONDS = 0.35;
inline constexpr double REPLAY_PREDICTION_MAX_WORK_MILLISECONDS = 5.0;
inline constexpr float REPLAY_VELOCITY_EDIT_LINEAR_MAX = 140.0f;
inline constexpr float REPLAY_VELOCITY_EDIT_ANGULAR_MAX = 5.0f;
inline constexpr float REPLAY_VELOCITY_EDIT_LINEAR_EXTRA = 36.0f;
#ifdef _DEBUG
inline constexpr const char* LAUNCHER_REPRO_SNAPSHOT_PATH = "Debug/launcher_repro_snapshots.txt";
inline constexpr double LAUNCHER_REPRO_MESSAGE_SECONDS = 3.0;
#endif

// Applies one fixed physics step plus Run-owned presentation/diagnostic edges.
// Live frames and replay target restore use the same helper so hash validation
// does not drift from normal gameplay stepping.
void StepRuntimePhysicsTick( GameObjects::GameModelCollection& modelCollection,
                             float fixedDt,
                             const EngineConfig& config,
                             const Physics::PhysicsWorldForces& worldForces,
                             Threading::WorkerPool& workerPool );

inline void SyncTornadoRuntimeSettingsToPhysics( GameObjects::GameModelCollection& modelCollection,
                                                 const RunRuntimeSettings& runtimeSettings )
{
    modelCollection.SetTornadoFieldConfig( runtimeSettings.tornadoField );
    modelCollection.SetTornadoSystemConfig( runtimeSettings.tornadoSystem );
}

inline bool ReplayModelIsRagdollPart( const GameObjects::GameModelCollection& collection, int modelIndex )
{
    return collection.IsSimpleRagdollPart( modelIndex );
}

inline bool ReplayModelIsRagdollTorso( const GameObjects::GameModelCollection& collection, int modelIndex )
{
    return collection.IsSimpleRagdollTorso( modelIndex );
}

inline void DrawUITestPattern( Rendering::IRenderCommandContext& renderCommands, int screenW, int screenH )
{
    const UI::UIDrawContext draw( screenW, screenH, nullptr, &renderCommands );
    draw.Rect( 0.0f, 0.0f, static_cast<float>( screenW ), static_cast<float>( screenH ), 0.20f, 0.31f, 0.36f, 1.0f );

    constexpr float tile = 88.0f;
    for ( float y = 0.0f; y < static_cast<float>( screenH ); y += tile )
    {
        for ( float x = 0.0f; x < static_cast<float>( screenW ); x += tile )
        {
            const int ix = static_cast<int>( x / tile );
            const int iy = static_cast<int>( y / tile );
            const bool alternate = ( ( ix + iy ) & 1 ) != 0;
            if ( alternate )
            {
                draw.Rect( x, y, tile, tile, 0.10f, 0.78f, 0.96f, 0.96f );
            }
            else
            {
                draw.Rect( x, y, tile, tile, 1.0f, 0.72f, 0.18f, 0.94f );
            }
            draw.Rect( x + 12.0f, y + 12.0f, tile - 24.0f, 5.0f, 0.96f, 0.98f, 1.0f, 0.74f );
            draw.Rect( x + tile - 18.0f, y + 18.0f, 5.0f, tile - 32.0f, 0.12f, 0.20f, 0.24f, 0.54f );
        }
    }

    draw.Rect( 44.0f, 46.0f, 780.0f, 560.0f, 1.0f, 1.0f, 1.0f, 0.18f );
    draw.Rect( 76.0f, 116.0f, 720.0f, 8.0f, 0.98f, 0.12f, 0.46f, 0.82f );
    draw.Rect( 76.0f, 300.0f, 720.0f, 8.0f, 0.30f, 1.0f, 0.56f, 0.78f );
    draw.Rect( 76.0f, 484.0f, 720.0f, 8.0f, 0.38f, 0.54f, 1.0f, 0.82f );
    Text::Text2d::FlushQuads( renderCommands );
}

inline int RuntimeWindowScreenWidth( const RunSubsystemState& systems, const EngineConfig& config )
{
    return systems.window ? static_cast<int>( systems.window->m_sWindowDimensions.x ) : config.window.screenX;
}

inline int RuntimeWindowScreenHeight( const RunSubsystemState& systems, const EngineConfig& config )
{
    return systems.window ? static_cast<int>( systems.window->m_sWindowDimensions.y ) : config.window.screenY;
}

inline CinematicRenderConfig& RuntimeActiveCinematicConfig( RunSceneState& scene, EngineConfig& config )
{
    return scene.isSceneMode ? scene.cinematicRender : config.cinematicRender;
}

inline const CinematicRenderConfig& RuntimeActiveCinematicConfig( const RunSceneState& scene,
                                                                  const EngineConfig& config )
{
    return scene.isSceneMode ? scene.cinematicRender : config.cinematicRender;
}

inline bool RuntimeCinematicRenderingEnabled( const RunSceneState& scene,
                                              const EngineConfig& config,
                                              const RunLaunchOptions& launchOptions,
                                              const RunDebugState& debug,
                                              bool gfxReady )
{
    const bool enabled = launchOptions.hasCinematicRenderingOverride
                             ? launchOptions.cinematicRendering
                             : RuntimeActiveCinematicConfig( scene, config ).enabled;
    return enabled && gfxReady && !debug.isTextOnly;
}

inline const char* FileNameFromPath( const char* path )
{
    if ( !path )
    {
        return "";
    }

    const char* slash = strrchr( path, '/' );
    const char* backslash = strrchr( path, '\\' );
    const char* separator = slash;
    if ( backslash && ( !separator || backslash > separator ) )
    {
        separator = backslash;
    }
    return separator ? separator + 1 : path;
}

inline std::string NormalizeScenePath( const std::string& path )
{
    std::string normalized = path;
    std::replace( normalized.begin(), normalized.end(), '\\', '/' );
    return normalized;
}
} // namespace RunInternal
} // namespace Basics
} // namespace SkullbonezCore
