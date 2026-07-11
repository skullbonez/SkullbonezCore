/*
File: SkullbonezSource/Runtime/GraphicsStressController.h
Purpose:
  Owns deterministic graphics-stress fuzzer state.

Mental model:
  Graphics stress is a CLI-driven render/runtime churn harness. The controller
  owns the seed, random stream, cadence, and counters; Run remains the executor
  because it owns scene loading, UI, render diagnostics, and live settings.

Glossary:
  Graphics stress: Deterministic fuzzer that mutates render settings, UI state,
    and scene loads to reproduce DX12 lifetime or resource bugs.
  Scene interval: Minimum rendered-frame spacing between stress-requested scene
    reloads.
  Memory log interval: Frame cadence for emitting memory attribution lines
    during long stress runs.

Invariants:
  - The random stream must advance only through controller methods.
  - Frame and scene-load counters persist across scene reloads.
  - A zero seed means uninitialized and is replaced by the launch seed when a
    scene reload resumes an existing graphics-stress run.

Related:
  - SkullbonezSource/Runtime/RuntimeStressController.cpp
  - SkullbonezSource/Runtime/RunFrame.cpp
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

#include "../UI/UICommands.h"

namespace SkullbonezCore
{
namespace Basics
{
class GraphicsStressController
{
  public:
    void Configure( unsigned int seed, int actionsPerFrame, int sceneIntervalFrames, int memoryLogIntervalFrames );
    void ResumeAfterSceneLoad( unsigned int seed, int actionsPerFrame, int sceneIntervalFrames );
    bool IsEnabled() const;
    unsigned int RandomState() const;
    int ActionsPerFrame() const;
    int SceneIntervalFrames() const;
    int FramesRun() const;
    int SceneLoadsRequested() const;
    void BeginFrame();
    void RecordSceneLoad();
    int NextInt( int maxExclusive );
    float NextFloat( float minValue, float maxValue );
    int ActionCount() const;
    int NextAction();
    bool SceneLoadDue() const;
    bool ShouldPrintFrameSummary() const;
    bool ShouldLogMemory() const;
    float RandomCinematicParamValue( UI::UICinematicParam param );

  private:
    bool m_enabled = false;               // Active after --graphics-stress is applied.
    unsigned int m_randomState = 0;       // LCG state; 0 means uninitialized.
    int m_actionsPerFrame = 12;           // Render/state mutations per rendered frame.
    int m_sceneIntervalFrames = 45;       // Minimum frames between forced scene reloads.
    int m_memoryLogIntervalFrames = 1800; // Coarse memory-attribution log cadence; 0 disables it.
    int m_framesRun = 0;                  // Persistent across scene reloads.
    int m_sceneLoadsRequested = 0;        // Count of stress-driven LoadScene calls.
    int m_lastSceneLoadFrame = -1000000;  // Frame index of the last stress scene load.
};
} // namespace Basics
} // namespace SkullbonezCore
