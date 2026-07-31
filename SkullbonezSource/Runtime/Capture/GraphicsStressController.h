/*
File: SkullbonezSource/Runtime/Capture/GraphicsStressController.h
Purpose:
  Owns deterministic graphics-stress fuzzer state.

Summary:
  Graphics stress is a CLI-driven render/runtime churn harness. The controller
  owns the seed, random stream, cadence, and counters; Run remains the executor
  because it owns scene loading, UI, render diagnostics, and live settings.

Glossary:
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
  - SkullbonezSource/Runtime/Capture/RuntimeStressController.cpp
  - SkullbonezSource/Runtime/App/RunFrame.cpp
  - Agentic/Reference/comment-style-guide.md
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "../../UI/UICommands.h"

#include <cstdint>

namespace SkullbonezCore
{
namespace Runtime
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
    bool InDescriptorChurnQuietWindow() const;
    bool ShouldCaptureDescriptorBaseline() const;
    bool ShouldIssueDescriptorResize() const;
    bool ShouldVerifyDescriptorChurn() const;
    void CaptureDescriptorBaseline( unsigned int staticUsed, uint64_t recreationGeneration );
    void ObserveRecreationGeneration( uint64_t recreationGeneration );
    void RecordDescriptorResize();
    void RecordTextureChurn();
    bool DescriptorChurnMatchesBaseline( unsigned int staticUsed ) const;
    unsigned int DescriptorBaseline() const;
    int DescriptorResizeCount() const;
    int AcknowledgedResizeCount() const;
    int TextureChurnCount() const;

  private:

    // Ten warmup frames establish stable process-lifetime rows; 131 requested
    // resizes exceed the old 128-row heap, then 30 drain frames precede proof.
    static constexpr int DESCRIPTOR_BASELINE_FRAME = 9;
    static constexpr int DESCRIPTOR_RESIZE_FIRST_FRAME = 10;
    static constexpr int DESCRIPTOR_RESIZE_LAST_FRAME = 140;
    static constexpr int DESCRIPTOR_VERIFY_FRAME = 170;
    bool m_enabled = false;                  // Active after --graphics-stress is applied.
    unsigned int m_randomState = 0;          // LCG state; 0 means uninitialized.
    int m_actionsPerFrame = 12;              // Render/state mutations per rendered frame.
    int m_sceneIntervalFrames = 45;          // Minimum frames between forced scene reloads.
    int m_memoryLogIntervalFrames = 1800;    // Coarse memory-attribution log cadence; 0 disables it.
    int m_framesRun = 0;                     // Persistent across scene reloads.
    int m_sceneLoadsRequested = 0;           // Count of stress-driven LoadScene calls.
    int m_lastSceneLoadFrame = -1000000;     // Frame index of the last stress scene load.
    unsigned int m_descriptorBaseline = 0;   // Live static rows before the bounded resize proof.
    int m_descriptorResizeCount = 0;         // Native resizes issued; acceptance requires more than 128.
    uint64_t m_lastRecreationGeneration = 0; // Last complete backend resize publication observed.
    int m_acknowledgedResizeCount = 0;       // Published backend resizes, not native requests.
    int m_textureChurnCount = 0;             // Successful texture create/delete turnovers.
};
} // namespace Runtime
} // namespace SkullbonezCore
