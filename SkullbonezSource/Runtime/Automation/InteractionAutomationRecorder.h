/*
File: SkullbonezSource/Runtime/Automation/InteractionAutomationRecorder.h
Purpose:
  Records interactive human input and UI gestures into resolution-independent automation scripts.

Summary:
  Captures live mouse clicks, window drags, tab switches, filter queries, key presses,
  and timeline scrubs into a fixed-capacity sequence of normalized and semantic actions.
  On completion, serializes the action sequence into a clean JSON script that can be
  replayed at any display resolution or converted into automated C++ doctest cases.

Invariants:
  - Recording is strictly opt-in via launch argument or hotkey; steady game turns incur zero allocation.
  - Recorded pointer coordinates store normalized viewport ratios (u, v in [0, 1]) alongside semantic control IDs.
  - File writing occurs only on explicit stop or application exit outside the simulation tick.

Related:
  - SkullbonezSource/Runtime/Automation/InteractionAutomationController.h
  - SkullbonezSource/Runtime/Automation/InteractionAutomationInputDriver.h
  - SkullbonezSource/Runtime/Input/InputRouter.h
  - SkullbonezSource/Runtime/Replay/ReplayOverlayLayout.h
*/
#pragma once

#include "../../Core/PlatformWin32.h"
#include "../../Core/SbResult.h"
#include "../Replay/ReplayAuthoringPackets.h"
#include "../Planning/ReplayCauseInspection.h"

#include <array>
#include <cstdint>

namespace SkullbonezCore::Runtime
{
class InputRouter;
class RuntimeInteractionController;

enum class RecordedActionKind : uint8_t
{
    None,
    ClickPoint,
    MoveMouse,
    ScrollPoint,
    PressKey,
    ClickReplayControl,
    SelectReplayCauseRow,
    ScrubReplaySolverTrack
};

struct RecordedInteractionAction
{
    int frame = 0;
    RecordedActionKind kind = RecordedActionKind::None;
    float normalizedX = 0.0f;
    float normalizedY = 0.0f;
    int pixelX = 0;
    int pixelY = 0;
    int holdFrames = 1;
    bool isRightButton = false;
    int wheelDelta = 0;
    int virtualKey = 0;
    float scrubFraction = 0.0f;
    int selectedRow = -1;
    char semanticControl[64] = {};
    char keyName[32] = {};
};

class InteractionAutomationRecorder
{
  public:
    static constexpr std::size_t MAX_RECORDED_ACTIONS = 2048u;

    void StartRecording( const char* outputPath, const char* scenePath = nullptr );
    void StopRecording();
    bool IsRecording() const noexcept
    {
        return m_isRecording;
    }
    const char* OutputPath() const noexcept
    {
        return m_outputPath;
    }
    const char* ScenePath() const noexcept
    {
        return m_scenePath;
    }
    std::size_t ActionCount() const noexcept
    {
        return m_actionCount;
    }

    void ToggleRecording( const char* defaultPath, int currentFrame, const char* defaultScenePath = nullptr );

    // Captures mouse, keyboard, and semantic UI events for one runtime frame.
    void RecordFrame( int currentFrame, int screenWidth, int screenHeight, const InputRouter& inputRouter,
                      const RuntimeInteractionController& interaction, const RunReplayCauseTreeState& causeTree,
                      const ReplayCauseInspectionView& causeInspection );

    // Serializes recorded actions to the configured output file.
    bool SaveToFile();

  private:
    bool AppendAction( const RecordedInteractionAction& action );

    bool m_isRecording = false;
    char m_outputPath[260] = {};
    char m_scenePath[260] = {};
    int m_startFrame = 0;
    int m_recordingTurn = 0;
    int m_previousPointerX = -1;
    int m_previousPointerY = -1;
    std::size_t m_actionCount = 0;
    std::array<RecordedInteractionAction, MAX_RECORDED_ACTIONS> m_actions = {};
    std::array<uint64_t, 4> m_previousKeys = {};
    std::array<int, 256> m_keyDownFrame = {};
    bool m_previousLeftDown = false;
    bool m_previousRightDown = false;
    int m_dragStartFrame = -1;
    int m_dragStartX = 0;
    int m_dragStartY = 0;
};
} // namespace SkullbonezCore::Runtime
