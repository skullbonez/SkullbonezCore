/*
File: SkullbonezSource/GameObjects/GameModelStreams.cpp
Purpose:
  Constructs body/render model stream views from the collection's SoA cache.

Mental model:
  GameModelCollection owns storage. GameModelStreamProvider owns the rules for
  presenting cache-backed body and render views to subsystem clients.

Glossary:
  SoA (Structure of Arrays): Data layout that stores each field in a separate
  contiguous array for cache-friendly iteration.
  Stream view: Borrowed pointer/count bundle over existing model storage.
  Cache validity: Flagged state showing whether the SoA arrays mirror the
  current GameModel list.

Invariants:
  - Stream providers return borrowed views into GameModelSoACache and never
    allocate independent stream storage.
  - Providers must refresh the relevant cache before returning pointers.

Related:
  - SkullbonezSource/GameObjects/GameModelStreams.h
  - Agentic/Reference/comment-style-guide.md
*/
#include "GameModelStreams.h"

namespace SkullbonezCore
{
namespace GameObjects
{
void GameModelStreamProvider::PrepareRenderStreams( GameModelSoACache& cache, std::vector<GameModel>& models )
{
    cache.EnsureModelMatrices( models );
}

GameModelBodyStream GameModelStreamProvider::GetBodyStream( GameModelSoACache& cache, std::vector<GameModel>& models )
{
    const int modelCount = static_cast<int>( models.size() );
    if ( !cache.bodyDataValid || cache.activeCount != modelCount )
    {
        cache.RefreshBodyData( models );
    }
    return GameModelBodyStream{ cache.positions.data(),
                                cache.boundingRadii.data(),
                                cache.isBox.data(),
                                cache.isFixed.data(),
                                modelCount };
}

GameModelRenderStream GameModelStreamProvider::GetRenderStream( GameModelSoACache& cache,
                                                                std::vector<GameModel>& models )
{
    cache.EnsureModelMatrices( models );
    return GameModelRenderStream{ cache.isBox.data(),
                                  cache.isFixed.data(),
                                  cache.modelMatrices.data(),
                                  static_cast<int>( models.size() ) };
}
} // namespace GameObjects
} // namespace SkullbonezCore
