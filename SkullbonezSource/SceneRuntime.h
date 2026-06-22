/*
File: SkullbonezSource/SceneRuntime.h
Purpose:
  Owns scene queue and scene-run state for the application runtime.

Mental model:
  Runtime code connects authored scene data, input, simulation, render
  backends, and validation-oriented launch modes. SceneRuntime is the scene
  lifecycle owner; Run coordinates higher-level work around it.

Glossary:
  Scene queue: Ordered list of authored scene paths, where an empty path means
  the generated demo scene.
  Cinematic deck: A queue of concept/cinematic scenes cycled as one authored
  visual look set.

Related:
  - SkullbonezSource/SceneRuntime.cpp
  - SkullbonezSource/Run.cpp
  - SkullbonezSource/RunScene.cpp
  - Agentic/Reference/runtime-reference.md
*/
#pragma once

#include "Config.h"

#include <string>
#include <vector>

namespace SkullbonezCore
{
namespace Basics
{
struct RunSceneState
{
    void ResetForLoad( const CinematicRenderConfig&
                           cinematicDefaults ); // Resets per-load state while preserving queue/manual-run ownership.

    int currentSceneIndex = -1;                 // Index into scene queue (-1 = not yet loaded)
    int loadCount = 0;                          // Number of scene/generated loads since startup
    int manualResetCount = 0;                   // Number of user-triggered resets since startup
    bool isSceneMode = false;                   // Scene file mode (deterministic, data-driven)
    bool isScenePhysics = true;                 // Physics enabled in scene mode
    bool isSceneText = true;                    // Text overlay enabled in scene mode
    int targetFrameCount = -1;                  // Frames to render before holding (-1 = unlimited)
    int currentFrame = 0;                       // Current frame counter for the loaded scene/generated run
    int modelCount = 0;                         // Number of models in the active scene
    int solverBallCount = 0;                    // Exact solver ball count when generated through solver_balls
    int solverBoxCount = 0;                     // Exact solver box count when generated through solver_boxes
    unsigned int rngSeed = 0;                   // Effective RNG seed used to build the current scene
    unsigned int rngState = 1;                  // Local deterministic generator state for scene object setup
    float timeScale = 1.0f;                     // Physics time multiplier
    bool isFixedStep = false;                   // One physics tick per render frame at PHYSICS_FIXED_DT (deterministic)
    bool isExitOnComplete = false;              // Exit automatically when targetFrameCount is reached
    bool isTestComplete = false;                // True after targetFrameCount without --exit; appends "- TEST COMPLETE" to HUD.
    bool isFinishLogged = false;                // Debug event log guard for scene completion
    bool isInteractiveRun = false;              // User/UI controlled scene flow: completion automation may hold/advance but never quit
    bool isEditableScene = false;               // Scene-tab-created file that should save live object state back to its scene file
    bool hasFlatSlope = false;                  // Active terrain was authored as flat_slope and can be preserved by live scene saves
    float flatBaseY = 0.0f;
    float flatSlopeX = 0.0f;
    float flatSlopeZ = 0.0f;

    // Live cinematic scene state. Scene files can override only selected fields,
    // while UI sliders mutate this copy at runtime so the user can tune the look
    // without changing engine.cfg.
    bool hasCinematicRenderingOverride = false;
    bool isCinematicRenderingEnabled = false;
    bool hasCinematicExposure = false;
    float cinematicExposure = 1.0f;
    bool hasCinematicGamma = false;
    float cinematicGamma = 2.2f;
    uint64_t cinematicOverrideMask = 0;
    uint64_t uiCinematicOverrideMask = 0;       // Cine-tab values edited by sliders/toggles and eligible for Save Defaults
    CinematicRenderConfig cinematicRender;
};

class SceneRuntime
{
  public:
    SceneRuntime() = default;
    explicit SceneRuntime( std::vector<std::string> queue );

    RunSceneState& State();
    const RunSceneState& State() const;

    bool HasEntry( int index ) const;
    bool HasCurrentEntry() const;
    const std::string* CurrentPath() const;
    const std::string& PathAt( int index ) const;
    int QueueSize() const;
    int CurrentIndex() const;
    int NextIndex() const;
    const std::vector<std::string>& Queue() const;

    void BeginLoad( int index );
    void MarkManualReset();
    int FindNormalizedPath( const std::string& normalizedPath ) const;
    int FindGeneratedDemo() const;
    int Append( std::string path );
    bool CurrentQueueIsCinematicDeck() const;
    int AdjacentQueueIndex( int direction ) const;

  private:
    RunSceneState m_state;
    std::vector<std::string> m_queue;
};
} // namespace Basics
} // namespace SkullbonezCore
