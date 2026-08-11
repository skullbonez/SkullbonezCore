/*
File: SkullbonezSource/Rendering/DX12/Dx12TextureRegistry.h
Purpose:
  Owns generation-tagged texture slots independently from GPU command work.

Summary:
  A backend texture handle is a slot plus a generation. Removing a slot leaves
  its generation behind; reusing that slot advances the generation so an old
  handle cannot resolve to the replacement texture.

Glossary:
  Tombstone: Empty registry slot retained for bounded reuse.

Invariants:
  - Handle zero is always null.
  - A tombstone advances generation before it is published again.
  - Slot storage grows only through the existing owner-approved registry path.

Related:
  - SkullbonezSource/Rendering/DX12/RenderBackendDX12.Textures.cpp
  - SkullbonezSource/Rendering/DX12/RenderDeviceDX12.h
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "RenderDeviceDX12.h"

#include <vector>

namespace SkullbonezCore::Rendering
{
struct TextureEntryDX12
{
    ID3D12Resource* resource = nullptr;
    UINT srvIndex = UINT_MAX; // Index in the persistent SRV region.
    uint8_t generation = 1;   // Changes before a tombstone slot is reused.
    bool owned = false;       // False for framebuffer-registered SRVs.
};

class Dx12TextureRegistry
{
  public:
    void Initialize( size_t capacity )
    {
        m_entries.clear();
        m_entries.resize( capacity );
    }
    uint32_t Insert( const TextureEntryDX12& entry )
    {
        for ( size_t index = 0; index < m_entries.size(); ++index )
        {
            TextureEntryDX12& slot = m_entries[index];

            if ( !slot.resource && slot.srvIndex == UINT_MAX && !slot.owned )
            {
                const uint8_t generation = Dx12TextureHandleCodec::NextGeneration( slot.generation );
                slot = entry;
                slot.generation = generation;
                return Dx12TextureHandleCodec::Encode( index, generation );
            }
        }

        return 0;
    }

    TextureEntryDX12* Resolve( uint32_t handle )
    {
        size_t slotIndex = 0;
        return ResolveSlotIndex( handle, slotIndex ) ? &m_entries[slotIndex] : nullptr;
    }
    const TextureEntryDX12* Resolve( uint32_t handle ) const
    {
        size_t slotIndex = 0;
        return ResolveSlotIndex( handle, slotIndex ) ? &m_entries[slotIndex] : nullptr;
    }
    std::vector<TextureEntryDX12>& Entries()
    {
        return m_entries;
    }
    const std::vector<TextureEntryDX12>& Entries() const
    {
        return m_entries;
    }
    size_t Count() const
    {
        size_t active = 0;

        for ( const TextureEntryDX12& entry : m_entries )
        {
            active += entry.srvIndex != UINT_MAX ? 1u : 0u;
        }

        return active;
    }
    size_t Capacity() const
    {
        return m_entries.capacity();
    }
    void Clear()
    {
        m_entries.clear();
    }

  private:
    bool ResolveSlotIndex( uint32_t handle, size_t& outSlotIndex ) const
    {
        uint8_t generation = 0;

        if ( !Dx12TextureHandleCodec::Decode( handle, outSlotIndex, generation ) || outSlotIndex >= m_entries.size() )
        {
            return false;
        }

        const TextureEntryDX12& entry = m_entries[outSlotIndex];
        return entry.srvIndex != UINT_MAX && entry.generation == generation;
    }

    std::vector<TextureEntryDX12> m_entries;
};
} // namespace SkullbonezCore::Rendering
