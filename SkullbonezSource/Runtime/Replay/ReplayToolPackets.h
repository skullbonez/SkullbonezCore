/*
File: ReplayToolPackets.h
Purpose:
  Publishes launcher visual values captured and restored by Replay without exposing recorder ownership.

Summary:
  RuntimeTools converts its bounded ray and laser histories into this value
  packet. Capture stores the packet; restore applies it through RuntimeTools.

Glossary:
  Launcher visual sample: Snapshot of ray lines, laser shots, fire mode, and authored launch strengths.
  Ring cursor: Next bounded history slot to overwrite.

Invariants:
  - Packet vectors are bounded by the existing launcher/capture reserve policy.
  - Capture and restore preserve both history cursors and fire-mode values.

Related:
  - ReplayRecorder.h
  - Runtime/Tools/RuntimeTools.h
*/
#pragma once

#include "../Editor/LauncherLaser.h"
#include "../../Maths/Vector3.h"

#include <cstdint>
#include <vector>

namespace SkullbonezCore::Runtime
{
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
