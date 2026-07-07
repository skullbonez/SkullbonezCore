//
// File: SkullbonezTests/TestTerrainLinkStubs.cpp
// Purpose:
//   Provide loud unit-test link stubs for Terrain queries pulled in by focused
//   PhysicsBodyStore tests.
//
// Mental model:
//   The unit harness links selected runtime translation units directly. Some
//   uncalled PhysicsBodyStore integration helpers reference Terrain, but the
//   handle-store tests must stay scoped to identity maps and dense-row moves.
//
// Glossary:
//   Link stub: Test-only method definition that satisfies unresolved symbols
//     while failing loudly if the focused test crosses into that dependency.
//   Test boundary: The intended subject of the unit test; crossing it means the
//     test is exercising integration behavior and needs a different fixture.
//
// Invariants:
//   - These methods are test-only link stubs, not physics fixtures.
//   - A focused handle test reaching Terrain is a test boundary failure.
//
// Related:
//   - SkullbonezSource/Physics/PhysicsBodyStore.cpp
//   - SkullbonezSource/World/Terrain.h
//   - fable_plans/01-unit-test-pyramid-progress.md
//

#include "../SkullbonezSource/World/Terrain.h"

#include <stdexcept>
#include <string>

namespace SkullbonezCore
{
namespace Geometry
{
namespace
{
[[noreturn]] void ThrowUnexpectedTerrainQuery( const char* methodName )
{
    throw std::runtime_error(
        std::string( "TestTerrainLinkStubs: unexpected Terrain query in focused unit test: " ) +
        methodName );
}
} // namespace

bool Terrain::IsInBounds( float, float )
{
    // Hazard: this stub exists only to satisfy link-time references from uncalled
    // Terrain integration helpers; using it would hide a physics-boundary leak.
    ThrowUnexpectedTerrainQuery( "Terrain::IsInBounds" );
}

float Terrain::GetTerrainHeightAt( float, float, bool )
{
    ThrowUnexpectedTerrainQuery( "Terrain::GetTerrainHeightAt" );
}

void Terrain::GetTerrainHeightAndPlaneAt( float, float, float&, Plane& )
{
    ThrowUnexpectedTerrainQuery( "Terrain::GetTerrainHeightAndPlaneAt" );
}
} // namespace Geometry
} // namespace SkullbonezCore
