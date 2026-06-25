/*
File: SkullbonezSource/Runtime/RuntimeCameraMode.h
Purpose:
  Defines the operator camera/workspace mode shared by runtime subsystems.
*/
#pragma once

namespace SkullbonezCore::Basics
{
enum class RunCameraMode
{
    Demo = 0,
    Scene,
    Inspect,
    Launcher,
    Manipulator,
    Count
};
} // namespace SkullbonezCore::Basics
