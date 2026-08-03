/*
File: SceneRequestQueue.h
Purpose:
  Declares the fixed scene-owner request vocabulary used at the input boundary.

Summary:
  SceneRequestQueue is SceneController's fixed ring for ordered UI, keyboard,
  and replay intent. SceneController drains one value batch at the runtime
  checkpoint, while SceneLoadTransaction owns the load-phase order.

Invariants:
  - Submission order is observable same-frame behavior.
  - A frame batch contains at most the first submitted scene transition.
  - Create names are bounded without truncation.
  - Capacity exhaustion is a fatal owner-budget violation, never a growth path.

Related:
  - SkullbonezSource/Runtime/Scene/SceneController.h
  - Agentic/Reports/2026-07-11/runtime-shell-final-ownership-review.md
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "../../Core/SbResult.h"

#include <cstddef>

namespace SkullbonezCore
{
namespace Runtime
{
constexpr int SCENE_REQUEST_TEXT_CAPACITY = 256;
constexpr int SCENE_REQUEST_QUEUE_CAPACITY = 64;

enum class SceneRequestType
{
    LoadBrowserIndex,
    LoadDemoScene,
    ResetCurrentScene,
    CreateScene,
    SaveCurrentDefaults,
};

struct SceneRequest
{
    SceneRequestType type = SceneRequestType::LoadBrowserIndex;
    int index = -1;
    char text[SCENE_REQUEST_TEXT_CAPACITY] = {};
    bool preserveUIState = true;
    bool suppressExitOnComplete = true;
    bool preserveRuntimeState = true;
};

struct SceneRequestBatch
{
    SceneRequest requests[SCENE_REQUEST_QUEUE_CAPACITY];
    std::size_t count = 0;
    std::size_t rejectedTransitionCount = 0;
};

constexpr bool SceneRequestIsTransition( SceneRequestType type )
{
    return type != SceneRequestType::SaveCurrentDefaults;
}

// A failed transition may have committed teardown before reporting a
// recoverable population error. Later requests must not observe or persist
// that partial replacement scene.
constexpr bool SceneRequestBatchContinuesAfter( SceneRequestType type, bool accepted )
{
    return !SceneRequestIsTransition( type ) || accepted;
}

class SceneRequestQueue
{
  public:
    SkullbonezCore::Core::SbResult Submit( SkullbonezCore::Core::SbDiagnosticStore& diagnostics,
                                           const SceneRequest& request );
    SceneRequestBatch TakePending();

    // Lets App cancel scene-local sibling state before ExecutePending begins a
    // queued transition; the queue and request values remain owned here.
    bool HasTransition() const;
    std::size_t Size() const;

  private:
    SceneRequest m_requests[SCENE_REQUEST_QUEUE_CAPACITY]; // Fixed owner ring.
    int m_head = 0;                                        // Oldest scene request.
    int m_count = 0;                                       // Occupied scene request slots.
};
} // namespace Runtime
} // namespace SkullbonezCore
