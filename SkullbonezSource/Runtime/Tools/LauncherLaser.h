/*
File: SkullbonezSource/Runtime/Tools/LauncherLaser.h
Purpose:
  Owns launcher-mode laser ribbon shot state.

Summary:
  The laser is display-only feedback for launcher-mode ray shots. It never changes
  physics state; callers provide the already-computed hit/miss segment.

Invariants:
  - LauncherLaser owns only bounded CPU presentation state.
  - Replay snapshots must preserve enough state to restore visible shots.
  - Runtime/Render owns the additive GPU submission resources.

Related:
  - SkullbonezSource/Runtime/Tools/LauncherLaser.cpp
  - SkullbonezSource/Runtime/Tools/LauncherTools.cpp
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include "../../Maths/Vector3.h"
#include "../Replay/ReplayToolPackets.h"

namespace SkullbonezCore
{
namespace Runtime
{
class LauncherLaser
{
  private:
    using Shot = LauncherLaserShotSnapshot;

    static constexpr std::size_t MAX_SHOTS = REPLAY_LAUNCHER_LASER_SHOT_CAPACITY;
    std::array<Shot, MAX_SHOTS> m_shots = {};
    int m_nextShot = 0;

  public:
    void Reset();
    void Fire( const Math::Vector::Vector3& rayOrigin, const Math::Vector::Vector3& rayDirection,
               const Math::Vector::Vector3& cameraUp, float distance, bool hit );
    void Update( float dt );
    bool HasActiveShots() const;
    std::span<const LauncherLaserShotSnapshot> PresentationShots() const;
    void CaptureShots( std::vector<LauncherLaserShotSnapshot>& outShots, int& outNextShot ) const;
    void RestoreShots( const std::vector<LauncherLaserShotSnapshot>& shots, int nextShot );
};
} // namespace Runtime
} // namespace SkullbonezCore
