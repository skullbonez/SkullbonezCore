/*
File: SkullbonezSource/Scene/AuthoredTornadoConfig.h
Purpose:
  Defines the content-neutral DTOs used while parsing authored tornado JSON.

Summary:
  Scene parsing stores authored values without depending on the Gameplay module.
  Runtime composition projects these cold-load records into Gameplay-owned
  configuration before the scene begins stepping.

Invariants:
  - These records mirror the stable authored schema; changing their JSON shape
    requires the repository's versioned authored-format migration process.
  - The parser rejects more than 64 vortices before Runtime projects the values
    into Gameplay's fixed-capacity force-field storage.

Related:
  - SkullbonezSource/Scene/AuthoredSceneParserRuntime.cpp
  - SkullbonezSource/Gameplay/TornadoField.h
  - SkullbonezSource/Runtime/Scene/SceneController.Load.cpp
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "../Maths/Vector3.h"

#include <cstddef>
#include <vector>

namespace SkullbonezCore
{
namespace Runtime
{
inline constexpr std::size_t MAX_AUTHORED_TORNADO_VORTICES = 64u;

struct AuthoredTornadoFieldConfig
{
    bool enabled = false;
    bool visualizeVelocityField = false;

    // Units mirror the authored schema: distances are metres, acceleration is
    // m/s^2, timing is seconds, and maxDeltaVelocity is m/s per fixed step.
    Math::Vector::Vector3 center = Math::Vector::Vector3( 620.0f, 25.0f, 615.0f );
    float radius = 210.0f;
    float height = 140.0f;
    float inwardAcceleration = 150.0f;
    float swirlAcceleration = 185.0f;
    float liftAcceleration = 64.0f;
    float ejectAcceleration = 260.0f;
    float ejectUpAcceleration = 70.0f;
    float ejectBand = 0.96f;
    float minCaptureSeconds = 2.50f;
    float ejectCooldownSeconds = 3.50f;
    float maxDeltaVelocity = 24.0f;
};

struct AuthoredTornadoVortexConfig
{
    AuthoredTornadoFieldConfig field;

    // Units: lifecycle values use seconds, drift phase uses radians, drift
    // speed uses radians/second, radii use metres, and repulsion is a scalar.
    float spawnSeconds = 0.0f;
    float timeToLiveSeconds = 0.0f;
    float growSeconds = 2.0f;
    float shrinkSeconds = 2.0f;
    float driftRadius = 0.0f;
    float driftSpeed = 0.0f;
    float driftPhase = 0.0f;
    float repulsionRadius = 0.0f;
    float repulsionStrength = 0.0f;
};

struct AuthoredTornadoSystemConfig
{
    bool enabled = false;
    bool visualizeVelocityField = false;

    // Lifetime: this vector grows only during cold authored-file parsing and is
    // projected into Gameplay before steady simulation begins.
    std::vector<AuthoredTornadoVortexConfig> vortices;
};
} // namespace Runtime
} // namespace SkullbonezCore
