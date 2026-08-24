/*
File: ReplayAuthoringPackets.h
Purpose:
  Publishes replay cause-tree and velocity-edit values without exposing the mutable Authoring owner.

Summary:
  Presentation and development UI consume bounded cause rows and editor-state
  values. Exact prediction rows carry the full immutable evidence stamp needed
  to reject a replacement bank before numeric offsets can resolve. Velocity
  drag state retains only its target, starting values, and whether release owes
  one authoritative refresh. Cause filtering retains bounded ASCII text, chip
  state, and key edges beside the sole ReplayAuthoring window owner.

Glossary:
  Dense-row hint: Frame-local model row validated against a stable scene object id before use.
  Drag changed: Bit recording that at least one accepted velocity mutation
    requires a release-time prediction refresh.

Invariants:
  - PhysicsSceneObjectId remains durable identity; ModelRowHint is only a cache.
  - Cause rows reserve their bounded capacity before steady runtime.
  - Filter text and key-edge memory are fixed-size and never create a second
    filtered row store.
  - Prediction Manifold and SolverRow offsets are usable only with an exact
    generation/mode/epoch/frame/topology/publication identity match.
  - Velocity drag state is fixed-size and carries no Prediction owner borrow.

Related:
  - ReplayAuthoring.h
  - SkullbonezSource/Runtime/Planning/ReplayOverlayPackets.h
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "ReplayIdentity.h"
#include "../../Maths/Vector3.h"
#include "../../Physics/PhysicsHandles.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace SkullbonezCore::Runtime
{
inline constexpr float REPLAY_VELOCITY_EDIT_LINEAR_MAX = 140.0f;
inline constexpr float REPLAY_VELOCITY_EDIT_ANGULAR_MAX = 5.0f;
inline constexpr float REPLAY_VELOCITY_EDIT_LINEAR_EXTRA = 36.0f;

enum class ReplayToolGestureKind : uint8_t
{
    None,
    ScrubDrag,
    VelocityDrag,
    PredictionHorizonDrag,
    CauseTreeDrag
};

struct ReplayToolGestureView
{
    ReplayToolGestureKind kind = ReplayToolGestureKind::None;
    Physics::PhysicsBodyHandle body;
    int axis = -1;
    bool angular = false;
};

enum class ReplayWorldOwnerRequest : uint8_t
{
    None,
    Scrub,
    VelocityEdit,
    CauseTree
};

// Invariant: native capture begins and ends with the matching replay gesture.
// A request can begin or end a gesture, never both.
class ReplayInteractionRequest
{
  public:
    void RequestWorldOwner( ReplayWorldOwnerRequest owner )
    {
        m_worldOwner = owner;
    }

    void BeginCauseTreeDrag( int startX, int startY, int axis )
    {
        BeginGesture( ReplayToolGestureKind::CauseTreeDrag, startX, startY, {}, axis, false );
    }

    void BeginVelocityDrag( int startX, int startY, Physics::PhysicsBodyHandle body, int axis, bool angular )
    {
        BeginGesture( ReplayToolGestureKind::VelocityDrag, startX, startY, body, axis, angular );
    }

    void EndGesture()
    {
        m_beginGesture = ReplayToolGestureKind::None;
        m_endGesture = true;
        m_requestNativeCapture = false;
        m_releaseNativeCapture = true;
    }

    ReplayWorldOwnerRequest WorldOwner() const
    {
        return m_worldOwner;
    }
    ReplayToolGestureKind BeginGestureKind() const
    {
        return m_beginGesture;
    }
    int GestureStartX() const
    {
        return m_gestureStartX;
    }
    int GestureStartY() const
    {
        return m_gestureStartY;
    }
    int GestureAxis() const
    {
        return m_gestureAxis;
    }
    Physics::PhysicsBodyHandle GestureBody() const
    {
        return m_gestureBody;
    }
    bool GestureAngular() const
    {
        return m_gestureAngular;
    }
    bool EndsGesture() const
    {
        return m_endGesture;
    }
    bool RequestsNativeCapture() const
    {
        return m_requestNativeCapture;
    }
    bool ReleasesNativeCapture() const
    {
        return m_releaseNativeCapture;
    }

  private:
    void BeginGesture( ReplayToolGestureKind kind, int startX, int startY, Physics::PhysicsBodyHandle body, int axis,
                       bool angular )
    {
        m_beginGesture = kind;
        m_gestureStartX = startX;
        m_gestureStartY = startY;
        m_gestureAxis = axis;
        m_gestureBody = body;
        m_gestureAngular = angular;
        m_endGesture = false;
        m_requestNativeCapture = true;
        m_releaseNativeCapture = false;
    }

    ReplayWorldOwnerRequest m_worldOwner = ReplayWorldOwnerRequest::None;
    ReplayToolGestureKind m_beginGesture = ReplayToolGestureKind::None;
    int m_gestureStartX = 0;
    int m_gestureStartY = 0;
    int m_gestureAxis = -1;
    Physics::PhysicsBodyHandle m_gestureBody;
    bool m_gestureAngular = false;
    bool m_endGesture = false;
    bool m_requestNativeCapture = false;
    bool m_releaseNativeCapture = false;
};

struct ReplayCauseTreeInputFrame
{
    ReplayToolGestureView gesture;
    std::array<uint64_t, 4> currentFilterKeys = {};
    std::array<char, 16> filterCharacters = {};
    std::size_t filterCharacterCount = 0;
    int mouseX = 0;
    int mouseY = 0;
    int wheelDelta = 0;
    int screenWidth = 0;
    int screenHeight = 0;
    bool leftPressed = false;
    bool leftReleased = false;
    bool hasClientPosition = false;
    bool filterBackspacePressed = false;
    bool filterDeletePressed = false;
    bool filterEscapePressed = false;
    bool filterReturnPressed = false;
    bool rowsReady = false;
    bool uiBlocksMouse = false;
    bool editorModeEnabled = false;
};

struct ReplayCauseTreeInputResult
{
    ReplayInteractionRequest interaction;
    int focusRow = -1;
    bool exitInspectionCamera = false;
    bool consumesMouse = false;
};

struct ReplayVelocityInputFrame
{
    ReplayToolGestureView gesture;
    bool replayToolOwnsWorld = false;
    bool velocityEditOwnsWorld = false;
    int mouseX = 0;
    int mouseY = 0;
    bool leftDown = false;
    bool leftPressed = false;
    bool leftReleased = false;
    bool hasClientPosition = false;
};

struct ReplayVelocityInputResult
{
    ReplayInteractionRequest interaction;
    bool enterInteractive = false;
    bool pathPickRequested = false;
    bool consumesMouse = false;
};

struct RunReplayCauseTreeRow
{
    RunReplayCauseTreeRowKind kind = RunReplayCauseTreeRowKind::Body;
    Physics::PhysicsSceneObjectId id;
    Physics::PhysicsSceneObjectId parentId;
    Physics::PhysicsSceneObjectId counterpartId;

    // Invariant: this is the exact source-bank frame for the row. Recorded
    // children inherit their solver sample frame; prediction rows address the
    // published prediction bank. Consumers never clamp it to another frame.
    ReplayFrameIndex firstFrame = 0;
    int depth = 0;
    Physics::ModelRowHint modelRow;
    Physics::ModelRowHint counterpartModelRow;
    int contactIndex = -1;
    int solverRowIndex = -1;
    int pipelineIndex = -1;

    // Invariant: predicted exact-detail rows carry the complete immutable-bank
    // stamp. Numeric contact/pipeline offsets are meaningful only when every
    // field still resolves the same sealed evidence frame.
    uint32_t sourceGeneration = 0;
    uint64_t sourceBankEpoch = 0;
    uint32_t sourceTopologyVersion = 0;
    uint64_t sourcePublicationVersion = 0;
    bool sourceHighDetail = false;
    int featureId = 0;
    int manifoldPointCount = 0;
    float penetration = 0.0f;
    float normalImpulse = 0.0f;
    float tangentImpulse = 0.0f;
    float warmStartImpulse = 0.0f;
    float bias = 0.0f;
    float effectiveMass = 0.0f;
    float frictionLimit = 0.0f;
    Math::Vector::Vector3 point = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 normal = Math::Vector::Vector3( 0.0f, 1.0f, 0.0f );
    Math::Vector::Vector3 impulse = Math::Vector::ZERO_VECTOR;
    bool prediction = false;
    bool terrain = false;
    bool warmStarted = false;
    char name[64] = {};
    char detail[160] = {};
};

enum class RunReplayCauseTreeFilter : uint8_t
{
    All,
    Prediction,
    Contacts
};

inline constexpr std::size_t REPLAY_CAUSE_FILTER_TEXT_CAPACITY = 48u;

struct RunReplayCauseTreeState
{
    // Runtime allocation policy: Authoring reserves the full bounded row
    // capacity before steady runtime; builders never grow this vector while the
    // replay UI is active.
    // Invariant: builders fail closed before exposing a partial cause-tree row set.
    std::vector<RunReplayCauseTreeRow> rows;
    int selectedRow = -1;
    Physics::PhysicsSceneObjectId focusedId;
    bool hasWindowPlacement = false;
    int x = 0;
    int y = 0;
    int width = 380;
    int height = 420;
    float scrollY = 0.0f;
    int dragOffsetX = 0;
    int dragOffsetY = 0;
    int resizeStartMouseX = 0;
    int resizeStartMouseY = 0;
    int resizeStartWidth = 0;
    int resizeStartHeight = 0;
    int mouseX = 0;
    int mouseY = 0;
    bool pointerBlocked = true;

    // Concept: ReplayAuthoring owns filter focus and its fixed text/key memory;
    // rendering consumes only this detached value state. Unsupported virtual
    // keys never enter the ASCII evidence search buffer.
    char filterText[REPLAY_CAUSE_FILTER_TEXT_CAPACITY] = {};
    RunReplayCauseTreeFilter filter = RunReplayCauseTreeFilter::All;
    bool filterFocused = false;
    std::array<uint64_t, 4> filterKeysWasDown = {};
};

struct RunReplayVelocityEditState
{
    bool enabled = false;
    bool keyboardAltWasDown = false;
    bool dragChanged = false;
    int hotLinearAxis = -1;
    int hotAngularAxis = -1;
    Physics::PhysicsSceneObjectId dragTargetId;
    float dragStartAxisT = 0.0f;
    float dragStartAngle = 0.0f;
    Math::Vector::Vector3 dragStartLinearVelocity = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 dragStartAngularVelocity = Math::Vector::ZERO_VECTOR;
};
} // namespace SkullbonezCore::Runtime
