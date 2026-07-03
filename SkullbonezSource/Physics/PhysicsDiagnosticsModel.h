/*
File: SkullbonezSource/Physics/PhysicsDiagnosticsModel.h
Purpose:
  Defines the debug-only model facts serialized by physics diagnostics streams.

Mental model:
  SkullScope and Debug CSV output need stable per-body names, motion values, and
  shape metadata, but diagnostics must not borrow the mutable GameModel range.
  This record is a one-frame serialization DTO, not physics storage authority.

Glossary:
  DTO (Data Transfer Object): Plain value record passed across a subsystem
    boundary so the receiver can serialize data without owning the source.
  SkullScope: Queryable physics diagnostics workflow backed by bounded trace
    output and local queries.

Invariants:
  - Records are sampled from dense model indices during one diagnostics pass.
  - Borrowed string pointers are valid only while the model owner is unchanged.

Related:
  - SkullbonezSource/Physics/PhysicsDiagnosticsSink.cpp
  - SkullbonezSource/Core/SkullScope.cpp
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

#ifdef _DEBUG

#include <cstdint>

#include "../Maths/Vector3.h"

namespace SkullbonezCore
{
namespace Physics
{
struct PhysicsDiagnosticsModelRecord
{
    const char* name = "";
    const char* shapeName = "";
    const char* hullName = "";
    Math::Vector::Vector3 position = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 velocity = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 angularVelocity = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 rotationalInertia = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 halfExtents = Math::Vector::ZERO_VECTOR;
    float qx = 0.0f;
    float qy = 0.0f;
    float qz = 0.0f;
    float qw = 1.0f;
    float mass = 0.0f;
    float inverseMass = 0.0f;
    float radius = 0.0f;
    uint16_t hullVertices = 0;
    uint16_t hullFaces = 0;
    uint16_t hullEdges = 0;
};
} // namespace Physics
} // namespace SkullbonezCore

#endif
