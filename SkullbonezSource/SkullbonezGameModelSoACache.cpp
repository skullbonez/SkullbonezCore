#include "SkullbonezGameModelSoACache.h"

#include "SkullbonezProfiler.h"

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
