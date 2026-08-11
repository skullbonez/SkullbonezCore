/*
File: SkullbonezSource/Runtime/Scene/SceneTerrain.h
Purpose:
  Owns the replaceable terrain and its matching scene-shape classification.

Summary:
  A scene load constructs a complete Terrain off to the side, then publishes it
  through one SceneTerrain replacement. Render passes borrow this stable owner
  and resolve the current terrain only when they execute, so later scene loads
  cannot leave them pointing at destroyed storage.

Glossary:
  Terrain replacement: Cold scene-load publication of a fully constructed
    height-map or flat-slope terrain.
  Flat-slope classification: Scene-load fact that selects whether the default
    height-map terrain may be reused or must be rebuilt.

Invariants:
  - A published terrain is never null.
  - Terrain storage and flat-slope classification change in one operation.
  - Consumers borrow Terrain pointers only for synchronous work and never cache
    them across scene replacement.

Related:
  - SkullbonezSource/Runtime/Scene/SceneController.h
  - SkullbonezSource/Runtime/Scene/SceneController.Load.cpp
  - SkullbonezSource/Runtime/Render/RuntimeRenderPasses.h
*/
#pragma once

#include "../../Core/FatalError.h"
#include "../../World/Terrain.h"

#include <memory>
#include <utility>

namespace SkullbonezCore
{
namespace Runtime
{
class SceneTerrain
{
  public:
    Geometry::Terrain* Get()
    {
        return m_terrain.get();
    }

    const Geometry::Terrain* Get() const
    {
        return m_terrain.get();
    }

    bool IsFlatSlope() const
    {
        return m_isFlatSlope;
    }

    void Replace( std::unique_ptr<Geometry::Terrain> terrain, bool isFlatSlope )
    {
        if ( !terrain )
        {

            // Lane F: publishing no terrain would invalidate every scene-world
            // consumer and violates the replacement transaction contract.
            SB_FATAL( "SceneTerrain", "Cannot publish a null scene terrain." );
        }

        m_terrain = std::move( terrain );
        m_isFlatSlope = isFlatSlope;
    }

  private:
    std::unique_ptr<Geometry::Terrain> m_terrain;
    bool m_isFlatSlope = false;
};
} // namespace Runtime
} // namespace SkullbonezCore
