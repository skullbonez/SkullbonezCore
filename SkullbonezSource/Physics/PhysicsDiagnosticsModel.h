/*
File: SkullbonezSource/Physics/PhysicsDiagnosticsModel.h
Purpose:
  Defines the debug-only model facts serialized by physics diagnostics streams.

Summary:
  SkullScope and Debug CSV output need stable per-body names, motion values, and
  shape metadata, but diagnostics must not borrow mutable authoring storage.
  This record is a one-frame serialization DTO, not physics storage authority.

Invariants:
  - Records are sampled from dense model indices during one diagnostics pass.
  - Borrowed string pointers are valid only while the model owner is unchanged.

Related:
  - SkullbonezSource/Physics/PhysicsDiagnosticsSink.cpp
  - SkullbonezSource/Physics/Diagnostics/SkullScope.cpp
  - Agentic/Reference/comment-style-guide.md
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#ifdef _DEBUG

#include <cstdint>

#include "../Maths/Vector3.h"

namespace SkullbonezCore
{
namespace Physics
{
class ColliderStore;
class PhysicsBodyStore;

// Cold presentation names for diagnostics rows. The table is supplied by the
// scene/model edge; diagnostics treats missing names as empty and never indexes
// authoring storage for physics state.
struct PhysicsDiagnosticsNameView
{
    const char* const* names = nullptr;
    int count = 0;

    const char* NameFor( int index ) const
    {
        return ( names && index >= 0 && index < count && names[index] ) ? names[index] : "";
    }
};

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

// Builds one serializable diagnostics row from store-owned physics state plus
// an optional name overlay. The caller owns row ordering and baseline emission.
bool TryBuildPhysicsDiagnosticsModelRecord( int index, const PhysicsBodyStore& bodyStore, const ColliderStore& colliderStore,
                                            const PhysicsDiagnosticsNameView& names,
                                            PhysicsDiagnosticsModelRecord& outRecord );
} // namespace Physics
} // namespace SkullbonezCore

#endif
