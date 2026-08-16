/*
File: SkullbonezSource/Runtime/Scene/SceneAuthoredSetup.CameraSlots.h
Purpose:
  Registers scene-lifetime camera slots shared by authored and generated setup.

Summary:
  Scene setup gives causal inspection one dedicated pose slot seeded from a
  valid scene camera. The slot is registered consistently while selection and
  saved-main-camera authority remain with existing camera and Replay owners.

Invariants:
  - Callers register after a main camera; adding the copied detail pose then
    preserves the selected main slot.
  - Ordinary demo cycling does not enumerate the detail slot.

Related:
  - SkullbonezSource/Runtime/Scene/SceneAuthoredSetup.cpp
  - SkullbonezSource/Runtime/Scene/SceneGeneratedSetup.cpp
  - SkullbonezSource/Runtime/Camera/CameraControlState.h
*/
#pragma once

#include "../Camera/CameraCollection.h"
#include "../../Assets/AssetKeys.h"

namespace SkullbonezCore::Runtime
{
inline void RegisterCausalDetailCamera( Environment::CameraCollection& cameras, const Math::Vector::Vector3& position,
                                        const Math::Vector::Vector3& view, const Math::Vector::Vector3& up )
{
    cameras.AddCamera( position, view, up, CAMERA_CAUSAL_DETAIL );
}
} // namespace SkullbonezCore::Runtime
