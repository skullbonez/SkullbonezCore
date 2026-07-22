/*
File: ReplayAuthoringPackets.h
Purpose:
  Publishes replay cause-tree and velocity-edit values without exposing the mutable Authoring owner.

Summary:
  Presentation and development UI consume bounded cause rows and editor-state
  values. Mutations still flow through ReplayRuntime commands.

Glossary:
  Cause row: One body, contact, solver, or prediction explanation in the replay causality tree.
  Dense-row hint: Frame-local model row validated against a stable scene object id before use.

Invariants:
  - PhysicsSceneObjectId remains durable identity; ModelRowHint is only a cache.
  - Cause rows reserve their bounded capacity before steady runtime.

Related:
  - ReplayAuthoring.h
  - ReplayOverlayPackets.h
*/
#pragma once

#include "ReplayIdentity.h"
#include "../../Maths/Vector3.h"
#include "../../Physics/PhysicsHandles.h"

#include <vector>

namespace SkullbonezCore::Runtime
{
inline constexpr float REPLAY_VELOCITY_EDIT_LINEAR_MAX = 140.0f;
inline constexpr float REPLAY_VELOCITY_EDIT_ANGULAR_MAX = 5.0f;
inline constexpr float REPLAY_VELOCITY_EDIT_LINEAR_EXTRA = 36.0f;

struct RunReplayCauseTreeRow
{
    RunReplayCauseTreeRowKind kind = RunReplayCauseTreeRowKind::Body;
    Physics::PhysicsSceneObjectId id;
    Physics::PhysicsSceneObjectId parentId;
    Physics::PhysicsSceneObjectId counterpartId;
    ReplayFrameIndex firstFrame = 0;
    int depth = 0;
    Physics::ModelRowHint modelRow;
    Physics::ModelRowHint counterpartModelRow;
    int contactIndex = -1;
    int solverRowIndex = -1;
    int pipelineIndex = -1;
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

struct RunReplayCauseTreeState
{
    // Invariant: Authoring reserves the full bounded row capacity before steady runtime; builders fail
    // closed instead of growing this vector while the replay UI is active.
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
};

struct RunReplayVelocityEditState
{
    bool enabled = false;
    bool keyboardAltWasDown = false;
    int hotLinearAxis = -1;
    int hotAngularAxis = -1;
    float dragStartAxisT = 0.0f;
    float dragStartAngle = 0.0f;
    Math::Vector::Vector3 dragStartLinearVelocity = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 dragStartAngularVelocity = Math::Vector::ZERO_VECTOR;
};
} // namespace SkullbonezCore::Runtime
