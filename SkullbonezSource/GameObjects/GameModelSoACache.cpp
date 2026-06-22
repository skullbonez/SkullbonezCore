/*
File: SkullbonezSource/GameObjects/GameModelSoACache.cpp
Purpose:
  Caches model state in structure-of-arrays form for render and physics hot paths.

Mental model:
  Runtime code connects authored scene data, input, simulation, render
  backends, and validation-oriented launch modes. Follow who owns state and
  when that state changes.

Glossary:
  SoA (Structure of Arrays): Cache layout that stores each field in its own
  contiguous array for faster iteration.
  Validation gate: Repository script that proves a class of changes before
  commit or PR.

Related:
  - SkullbonezSource/GameObjects/GameModelSoACache.h
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "GameModelSoACache.h"

#include "../Core/Profiler.h"

using namespace SkullbonezCore::GameObjects;


void GameModelSoACache::Clear()
{
    activeCount = 0;
    Invalidate();
}


void GameModelSoACache::Invalidate()
{
    bodyDataValid = false;
    modelMatricesValid = false;
}


void GameModelSoACache::RefreshBodyData( std::vector<GameModel>& models )
{
    PROFILE_SCOPED( "Frame/SoA" );
    PROFILE_SCOPED( "Frame/SoA/RefreshBodyData" );

    // Refresh only the fields needed by physics broadphase/solver decisions:
    // position, conservative collision radius, shape class, and fixed/dynamic
    // state. Model matrices are intentionally separate because rendering may
    // need them without forcing every physics-facing field to be recomputed.
    const int modelCount = static_cast<int>( models.size() );
    for ( int i = 0; i < modelCount; ++i )
    {
        positions[i] = models[i].GetPosition();
        boundingRadii[i] = models[i].GetBoundingRadius();
        isBox[i] = models[i].IsBox() ? 1 : 0;
        isFixed[i] = models[i].IsFixed() ? 1 : 0;
    }

    activeCount = modelCount;
    bodyDataValid = true;
}


void GameModelSoACache::EnsureModelMatrices( std::vector<GameModel>& models )
{
    // Model matrices depend on body pose and shape scale. Rebuild lazily so UI,
    // scene parsing, or physics-only validation can skip matrix work until a
    // render path actually asks for it.
    if ( !bodyDataValid )
    {
        RefreshBodyData( models );
    }

    const int modelCount = static_cast<int>( models.size() );
    if ( modelMatricesValid && activeCount == modelCount )
    {
        return;
    }

    for ( int i = 0; i < modelCount; ++i )
    {
        modelMatrices[i] = models[i].GetModelMatrix();
    }

    modelMatricesValid = true;
}
