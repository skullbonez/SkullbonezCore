/*
File: SkullbonezSource/SkullbonezGameModelStreams.h
Purpose:
  Builds borrowed model data streams over the current GameModel SoA cache.

Mental model:
  These streams are read-only views for hot physics/render loops. They are a
  model-data boundary over existing storage, not independent ownership.

Related:
  - SkullbonezSource/SkullbonezGameModelStreams.cpp
  - SkullbonezSource/SkullbonezGameModelSoACache.h
*/
#pragma once

#include <vector>

#include "SkullbonezGameModelSoACache.h"

namespace SkullbonezCore
{
namespace GameObjects
{
class GameModelStreamProvider
{
  public:
    static void PrepareRenderStreams( GameModelSoACache& cache, std::vector<GameModel>& models );
    static GameModelBodyStream GetBodyStream( GameModelSoACache& cache, std::vector<GameModel>& models );
    static GameModelRenderStream GetRenderStream( GameModelSoACache& cache, std::vector<GameModel>& models );
};
} // namespace GameObjects
} // namespace SkullbonezCore
