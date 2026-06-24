/*
File: SkullbonezSource/Runtime/Tools/RuntimeTools.h
Purpose:
  Owns transient runtime tool state while tool behavior moves out of Run.

Mental model:
  RuntimeTools is the Phase 6 compatibility boundary. Existing Run methods can
  still execute launcher/tool behavior, but launcher state and render feedback
  ownership live here instead of directly on Run.
*/
#pragma once

#include "../Editor/LauncherLaser.h"
#include "../../Maths/Vector3.h"

#include <array>
#include <cstddef>

namespace SkullbonezCore::Basics
{
struct RunRayCastTestLine
{
    Math::Vector::Vector3 start = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 end = Math::Vector::ZERO_VECTOR;
    float ageSeconds = 0.0f;
    bool active = false;
    bool hit = false;
};

enum class RunLauncherFireMode
{
    Laser,
    Projectile
};

struct RunRayCastTestState
{
    static constexpr std::size_t MAX_LINES = 64;

    std::array<RunRayCastTestLine, MAX_LINES> lines = {};
    int nextLine = 0;
    RunLauncherFireMode fireMode = RunLauncherFireMode::Laser;
    bool visualizeRays = false;
    float impulseStrength = 1800.0f;
    float projectileSpeed = 160.0f;
};

struct RunMousePickupState
{
    bool active = false;
    bool mouseCaptured = false;
    int modelIndex = -1;
    Math::Vector::Vector3 planePoint = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 planeNormal = Math::Vector::Vector3( 0.0f, 0.0f, 1.0f );
    Math::Vector::Vector3 grabOffset = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 targetPoint = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 preservedAngularVelocity = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 lastImpulse = Math::Vector::ZERO_VECTOR;
};

class RuntimeTools
{
  public:
    RunRayCastTestState& RayCastTest();
    const RunRayCastTestState& RayCastTest() const;

    LauncherLaser& Laser();
    const LauncherLaser& Laser() const;

    RunMousePickupState& MousePickup();
    const RunMousePickupState& MousePickup() const;

  private:
    RunRayCastTestState m_rayCastTest;
    LauncherLaser m_laser;
    RunMousePickupState m_mousePickup;
};
} // namespace SkullbonezCore::Basics
