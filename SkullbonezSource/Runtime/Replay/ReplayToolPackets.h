/*
File: ReplayToolPackets.h
Purpose:
  Publishes launcher visual values captured and restored by Replay without exposing recorder ownership.

Summary:
  RuntimeTools converts its bounded ray and laser histories into this value
  packet. Replay stores the packet; App applies restore values to RuntimeTools.

Glossary:
  Launcher visual sample: Snapshot of ray lines, laser shots, fire mode, and authored launch strengths.
  Ring cursor: Next bounded history slot to overwrite.

Invariants:
  - Packet capacities define the matching bounded Tools histories and Replay reserves.
  - Capture and restore preserve both history cursors and fire-mode values.

Related:
  - ReplayRecorder.h
  - Runtime/Tools/RuntimeTools.h
*/
#pragma once

#include "../../Maths/Vector3.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace SkullbonezCore::Runtime
{
inline constexpr std::size_t REPLAY_LAUNCHER_RAY_LINE_CAPACITY = 64;
inline constexpr std::size_t REPLAY_LAUNCHER_LASER_SHOT_CAPACITY = 32;

enum class ReplayLauncherFireMode : uint8_t
{
    Laser,
    Projectile
};

struct ReplayRayCastLineSample
{
    Math::Vector::Vector3 start = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 end = Math::Vector::ZERO_VECTOR;
    float ageSeconds = 0.0f;
    bool active = false;
    bool hit = false;
};

struct LauncherLaserShotSnapshot
{
    Math::Vector::Vector3 start = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 end = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 cameraRight = Math::Vector::Vector3( 1.0f, 0.0f, 0.0f );
    Math::Vector::Vector3 cameraUp = Math::Vector::Vector3( 0.0f, 1.0f, 0.0f );
    float ageSeconds = 0.0f;
    float lifetimeSeconds = 0.18f;
    bool active = false;
    bool hit = false;
};

struct ReplayLauncherVisualSample
{
    std::vector<ReplayRayCastLineSample> rayLines;
    std::vector<LauncherLaserShotSnapshot> laserShots;
    int nextRayLine = 0;
    int nextLaserShot = 0;
    ReplayLauncherFireMode fireMode = ReplayLauncherFireMode::Laser;
    bool visualizeRays = false;
    float impulseStrength = 0.0f;
    float projectileSpeed = 0.0f;
};
} // namespace SkullbonezCore::Runtime
