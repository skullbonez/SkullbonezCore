/*
File: SkullbonezSource/Runtime/RuntimePickService.h
Purpose:
  Defines explicit runtime picking requests for editor, tool, and replay input.

Mental model:
  Input routing should choose a pick purpose, then ask one service for the
  selection result. Early slices keep legacy Run helpers as wrappers while
  callers move over one purpose at a time.

Glossary:
  Pick purpose: The tool-specific policy for interpreting a mouse ray.
  Validation gate: Repository script that proves a class of changes before
  commit or PR.

Related:
  - Agentic/Plans/runtime-interaction-state-machine-hardening-plan.md
  - SkullbonezSource/Runtime/Editor/RunEditorTools.cpp
*/
#pragma once

#include <cfloat>
#include <vector>

#include "../Maths/Vector3.h"

namespace SkullbonezCore
{
namespace GameObjects
{
class GameModel;
}

namespace Basics
{
enum class RuntimePickPurpose
{
    EditorSelection,
    ManipulatorPickup
};

struct RuntimePickRequest
{
    RuntimePickPurpose purpose = RuntimePickPurpose::EditorSelection;
    const std::vector<GameObjects::GameModel>* models = nullptr;
    Math::Vector::Vector3 rayOrigin = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 rayDirection = Math::Vector::ZERO_VECTOR;
    float modelRadiusPadding = 1.0f;
};

struct RuntimePickResult
{
    int modelIndex = -1;
    float rayT = FLT_MAX;
};

class RuntimePickService
{
  public:
    static bool TryPickModel( const RuntimePickRequest& request, RuntimePickResult& outResult );
};
} // namespace Basics
} // namespace SkullbonezCore
