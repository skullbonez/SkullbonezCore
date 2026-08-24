/*
File: SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorControlPolicy.h
Purpose:
  Projects shared operator-control ranges into Dear ImGui diagnostics controls.

Summary:
  A stateless command-type lookup gives ImGui the same minimum, maximum, and
  quantization step used by GameUI and Runtime command validation without
  importing either presentation owner's retained state.

Invariants:
  - Every valid row is derived from OperatorControlPolicy constants.
  - Unknown diagnostics commands do not silently acquire a scalar range.

Related:
  - SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorOwner.cpp
  - SkullbonezSource/Runtime/Interaction/OperatorEditorExchange.h
  - SkullbonezTests/TestOwnerRequestQueues.cpp
*/
#pragma once

#include "../Interaction/OperatorEditorExchange.h"

namespace SkullbonezCore::Runtime::DevelopmentTools
{
struct ImGuiEditorScalarControlPolicy
{
    float minValue = 0.0f;
    float maxValue = 0.0f;
    float step = 0.0f;
    bool valid = false;
};

inline constexpr ImGuiEditorScalarControlPolicy
ResolveImGuiEditorDiagnosticsControlPolicy( UI::OperatorEditorDiagnosticsCommandType type ) noexcept
{
    using namespace UI::OperatorControlPolicy;

    switch ( type )
    {
    case UI::OperatorEditorDiagnosticsCommandType::SetPhysicsDebugAlpha:
        return { UI_PHYSICS_ALPHA_MIN, UI_PHYSICS_ALPHA_MAX, UI_PHYSICS_ALPHA_STEP, true };
    case UI::OperatorEditorDiagnosticsCommandType::SetPhysicsContactLinger:
        return { UI_CONTACT_LINGER_MIN, UI_CONTACT_LINGER_MAX, UI_CONTACT_LINGER_STEP, true };
    case UI::OperatorEditorDiagnosticsCommandType::SetRayCastImpulseStrength:
        return { UI_RAY_IMPULSE_MIN, UI_RAY_IMPULSE_MAX, UI_RAY_IMPULSE_STEP, true };
    case UI::OperatorEditorDiagnosticsCommandType::SetLauncherProjectileSpeed:
        return { UI_LAUNCHER_PROJECTILE_SPEED_MIN, UI_LAUNCHER_PROJECTILE_SPEED_MAX, UI_LAUNCHER_PROJECTILE_SPEED_STEP,
                 true };
    default:
        return {};
    }
}
} // namespace SkullbonezCore::Runtime::DevelopmentTools
