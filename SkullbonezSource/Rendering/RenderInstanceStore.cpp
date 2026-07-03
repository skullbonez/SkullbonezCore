/*
File: SkullbonezSource/Rendering/RenderInstanceStore.cpp
Purpose:
  Builds model-order render instance snapshots from GameModel state.

Mental model:
  Refresh copies renderer-facing values after gameplay/physics have committed.
  It does not allocate GPU resources; it records the CPU-side draw intent that a
  future render snapshot can consume.

Glossary:
  Render instance: One draw-facing object record with transform and material
    intent.
  Material intent: Engine-level material choice before a renderer maps it to
    shaders, textures, or descriptor rows.
  Replay body id: Stable per-scene id shared with physics/replay records.
  Contact highlight: Render-only feedback alpha copied from GameModel after
    gameplay/physics presentation state has advanced.

Invariants:
  - Records stay in GameModelCollection model order and compatibility handles
    mirror model indices.
  - Refresh snapshots CPU draw intent only; it does not create or destroy GPU
    resources.

Related:
  - SkullbonezSource/Rendering/RenderInstanceStore.h
*/
#include "RenderInstanceStore.h"

#include <cstddef>

#include "../Core/Common.h"
#include "../GameObjects/GameModel.h"
#include "../Physics/PhysicsModelAccess.h"

using SkullbonezCore::GameObjects::GameModel;
using SkullbonezCore::Rendering::RenderInstanceHandle;
using SkullbonezCore::Rendering::RenderInstanceRecord;
using SkullbonezCore::Rendering::RenderInstanceStore;


RenderInstanceStore::RenderInstanceStore()
{
    m_instances.reserve( MAX_GAME_MODELS );
    m_modelInstanceHandles.reserve( MAX_GAME_MODELS );
}


void RenderInstanceStore::Clear()
{
    m_instances.clear();
    m_modelInstanceHandles.clear();
}


void RenderInstanceStore::Refresh( std::vector<GameModel>& models )
{
    // Invariant: render instance handles intentionally mirror model slots until
    // a future renderer-facing allocation owner replaces compatibility ids.
    m_instances.resize( models.size() );
    m_modelInstanceHandles.resize( models.size() );
    for ( std::size_t i = 0; i < models.size(); ++i )
    {
        GameModel& model = models[i];
        RenderInstanceRecord& record = m_instances[i];
        const uint32_t modelIndex = static_cast<uint32_t>( i );
        record.handle = MakeCompatibilityRenderInstanceHandle( modelIndex );
        record.replayBodyId = model.GetReplayBodyId();
        record.modelMatrix = model.GetModelMatrix();
        record.material = model.GetRenderMaterial();
        record.isFixed = model.IsFixed();
        record.fixedContactAlpha = model.GetFixedContactHighlightAlpha();
        record.audioContactAlpha = model.GetAudioContactHighlightAlpha();
        m_modelInstanceHandles[i] = record.handle;
    }
}


void RenderInstanceStore::Refresh( SkullbonezCore::Physics::PhysicsModelAccess& modelAccess )
{
    // Invariant: render instance handles intentionally mirror model slots until
    // a future renderer-facing allocation owner replaces compatibility ids.
    auto models = modelAccess.Models();
    const int modelCount = models.Count();
    m_instances.resize( static_cast<std::size_t>( modelCount ) );
    m_modelInstanceHandles.resize( static_cast<std::size_t>( modelCount ) );
    for ( int i = 0; i < modelCount; ++i )
    {
        GameModel& model = models[static_cast<std::size_t>( i )];
        RenderInstanceRecord& record = m_instances[static_cast<std::size_t>( i )];
        const uint32_t modelIndex = static_cast<uint32_t>( i );
        record.handle = MakeCompatibilityRenderInstanceHandle( modelIndex );
        record.replayBodyId = model.GetReplayBodyId();
        record.modelMatrix = model.GetModelMatrix();
        record.material = model.GetRenderMaterial();
        record.isFixed = model.IsFixed();
        record.fixedContactAlpha = model.GetFixedContactHighlightAlpha();
        record.audioContactAlpha = model.GetAudioContactHighlightAlpha();
        m_modelInstanceHandles[static_cast<std::size_t>( i )] = record.handle;
    }
}


const RenderInstanceRecord* RenderInstanceStore::Data() const
{
    return m_instances.empty() ? nullptr : m_instances.data();
}


int RenderInstanceStore::Count() const
{
    return static_cast<int>( m_instances.size() );
}


bool RenderInstanceStore::Empty() const
{
    return m_instances.empty();
}


RenderInstanceHandle RenderInstanceStore::HandleForModelIndex( int modelIndex ) const
{
    if ( modelIndex < 0 || modelIndex >= static_cast<int>( m_modelInstanceHandles.size() ) )
    {
        return RenderInstanceHandle{};
    }

    return m_modelInstanceHandles[static_cast<std::size_t>( modelIndex )];
}


int RenderInstanceStore::ModelIndexForHandle( RenderInstanceHandle handle ) const
{
    if ( !Contains( handle ) )
    {
        return -1;
    }

    return static_cast<int>( handle.index );
}


bool RenderInstanceStore::Contains( RenderInstanceHandle handle ) const
{
    if ( !handle.IsValid() || handle.generation != RENDER_INSTANCE_COMPATIBILITY_HANDLE_GENERATION )
    {
        return false;
    }
    if ( handle.index >= m_instances.size() )
    {
        return false;
    }

    return m_instances[static_cast<std::size_t>( handle.index )].handle == handle;
}


const std::vector<RenderInstanceRecord>& RenderInstanceStore::Records() const
{
    return m_instances;
}
