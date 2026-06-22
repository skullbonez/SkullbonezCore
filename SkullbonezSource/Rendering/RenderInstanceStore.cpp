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

Related:
  - SkullbonezSource/Rendering/RenderInstanceStore.h
*/
#include "RenderInstanceStore.h"

#include <cstddef>

#include "../Core/Common.h"
#include "../GameObjects/GameModel.h"

using SkullbonezCore::GameObjects::GameModel;
using SkullbonezCore::Rendering::RenderInstanceRecord;
using SkullbonezCore::Rendering::RenderInstanceStore;


RenderInstanceStore::RenderInstanceStore()
{
    m_instances.reserve( MAX_GAME_MODELS );
}


void RenderInstanceStore::Clear()
{
    m_instances.clear();
}


void RenderInstanceStore::Refresh( std::vector<GameModel>& models )
{
    m_instances.resize( models.size() );
    for ( std::size_t i = 0; i < models.size(); ++i )
    {
        GameModel& model = models[i];
        RenderInstanceRecord& record = m_instances[i];
        record.replayBodyId = model.GetReplayBodyId();
        record.modelMatrix = model.GetModelMatrix();
        record.material = model.GetRenderMaterial();
        record.isFixed = model.IsFixed();
        record.fixedContactAlpha = model.GetFixedContactHighlightAlpha();
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


const std::vector<RenderInstanceRecord>& RenderInstanceStore::Records() const
{
    return m_instances;
}
