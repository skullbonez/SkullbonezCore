/*
File: SkullbonezSource/Runtime/RuntimeViewModel.h
Purpose:
  Defines the lightweight runtime view model consumed by UI and diagnostics.

Mental model:
  RuntimeViewModel is a read-only snapshot of common runtime presentation data.
  It is rebuilt from EngineContext rather than letting UI code chase storage
  owners directly.

Related:
  - SkullbonezSource/Runtime/EngineContext.h
  - SkullbonezSource/Runtime/RunUiTextPass.cpp
*/
#pragma once

namespace SkullbonezCore
{
namespace Basics
{
class EngineContext;

struct RuntimeViewModel
{
    bool sceneMode = false;         // True when an authored scene is active
    bool scenePhysics = false;      // Active scene physics toggle
    bool sceneText = false;         // Active scene text overlay toggle
    bool fixedStep = false;         // Active fixed-step toggle
    bool screenshotPending = false; // True when scene capture has not completed
    int sceneIndex = -1;            // Current scene queue index
    int sceneCount = 0;             // Number of queued scene entries
    int frame = 0;                  // Current per-load frame
    int targetFrameCount = -1;      // Completion frame target (-1 = unlimited)
    int modelCount = 0;             // Current runtime model count
    float timeScale = 1.0f;         // Active simulation time scale
};

class RuntimeViewModelBuilder
{
  public:
    static RuntimeViewModel Build( const EngineContext& context );
};
} // namespace Basics
} // namespace SkullbonezCore
