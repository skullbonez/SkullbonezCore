/*
File: SkullbonezSource/GameObjects/GameModelStreams.h
Purpose:
  Builds borrowed model data streams over the current GameModel SoA cache.

Mental model:
  These streams are read-only views for hot physics/render loops. They are a
  model-data boundary over existing storage, not independent ownership.

Glossary:
  SoA (Structure of Arrays): Data layout that stores each field in a separate
  contiguous array for cache-friendly iteration.
  Borrowed view: Pointer/count bundle that is valid only while the underlying
  GameModel collection and cache stay unchanged.
  Hot loop: Per-frame or per-body loop where cache misses and allocations are
  visible performance costs.

Invariants:
  - Returned streams are read-only borrowed views and must not outlive the
    model/cache mutation boundary that produced them.
  - Render streams require model matrices; body streams require only body data.

Related:
  - SkullbonezSource/GameObjects/GameModelStreams.cpp
  - SkullbonezSource/GameObjects/GameModelSoACache.h
*/
#pragma once

#include <vector>

#include "GameModelSoACache.h"

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
