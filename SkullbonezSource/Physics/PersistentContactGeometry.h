#pragma once
#include "ObjectContactManifold.h"
#include "PersistentContactSolver.h"
#include "PhysicsBodyStore.h"

namespace SkullbonezCore::Physics
{
// Refresh a hull face patch only after exact narrowphase confirms contact.
// The caller keeps primitive manifolds on their existing construction path.
void RefreshHullContactPatch( std::span<const PersistentContactCacheEntry> cache, const PhysicsBodyHotFieldsConstView& hot, ObjectContactManifold& manifold );
} // namespace SkullbonezCore::Physics
