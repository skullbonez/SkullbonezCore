/*
File: SkullbonezSource/Rendering/DX12/Dx12CachedPsoStore.h
Purpose:
  Declares the bounded persistent DX12 cached-PSO blob owner.

Mental model:
  The fixed in-memory PSO array is the per-device fast path. This owner adds a
  cold-start layer underneath it: driver-compatible cached PSO blobs are mapped
  read-only before draws and attached to matching native create descriptions.

Glossary:
  Cached PSO blob: Driver-produced compilation bytes accepted as an optional
    accelerator by CreateGraphicsPipelineState.
  Manifest identity: SHA-256 of the checked-in shader bake manifest, including
    compiler version, flags, source hashes, and bytecode hashes.
  Recipe identity: Process-stable SHA-256 of shaders, root signature, input
    layout, and all fixed-function PSO fields.

Invariants:
  - Persistent names never contain COM pointers or process-local std::hash values.
  - Blob I/O is cold-start/shutdown only and capped at MAX_BLOB_BYTES.
  - Corrupt, stale, incompatible, or unwritable caches degrade to native creation.

Related:
  - RenderBackendDX12.Pipeline.cpp
  - SkullbonezData/shaders/shader_manifest.json
  - Agentic/Plans/TODO/shader-pipeline-modernization.md
*/
#pragma once

#include <Windows.h>
#include <d3d12.h>
#include <wrl/client.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace SkullbonezCore::Rendering
{
class Dx12CachedPsoStore
{
  public:
    static constexpr std::size_t DIGEST_BYTES = 32;
    static constexpr std::size_t ENTRY_NAME_CHARS = 69;
    static constexpr std::size_t MAX_BLOB_BYTES = 16u * 1024u * 1024u;

    bool Initialize( const void* rootSignatureBytes, std::size_t rootSignatureSize );
    bool Attach( D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc );
    void RejectAttached( D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc );
    void Store( const D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc, ID3D12PipelineState* pipeline );
    void Shutdown();

    static bool BuildPersistentEntryNameForTest( const D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc,
                                                 const std::array<std::uint8_t, DIGEST_BYTES>& manifestDigest,
                                                 const std::array<std::uint8_t, DIGEST_BYTES>& rootSignatureDigest,
                                                 wchar_t ( &outName )[ENTRY_NAME_CHARS] );

  private:
    static bool BuildEntryName( const D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc,
                                const std::array<std::uint8_t, DIGEST_BYTES>& manifestDigest,
                                const std::array<std::uint8_t, DIGEST_BYTES>& rootSignatureDigest,
                                wchar_t ( &outName )[ENTRY_NAME_CHARS] );
    void Persist();

    struct MappedEntry
    {
        std::array<std::uint8_t, DIGEST_BYTES> digest = {};
        const void* bytes = nullptr;
        std::uint32_t size = 0;
        bool rejected = false;
    };

    struct LiveEntry
    {
        std::array<std::uint8_t, DIGEST_BYTES> digest = {};
        ID3D12PipelineState* pipeline = nullptr; // Borrowed until owner shutdown.
    };

    HANDLE m_file = INVALID_HANDLE_VALUE;
    HANDLE m_mapping = nullptr;
    const void* m_mappedBytes = nullptr;
    std::size_t m_mappedSize = 0;
    std::array<MappedEntry, 96> m_mappedEntries = {};
    std::size_t m_mappedEntryCount = 0;
    std::array<LiveEntry, 96> m_liveEntries = {};
    std::size_t m_liveEntryCount = 0;
    std::array<std::uint8_t, DIGEST_BYTES> m_manifestDigest = {};
    std::array<std::uint8_t, DIGEST_BYTES> m_rootSignatureDigest = {};
    wchar_t m_cachePath[MAX_PATH] = {};
    std::uint32_t m_hits = 0;
    std::uint32_t m_misses = 0;
    std::uint32_t m_stores = 0;
    std::uint32_t m_failures = 0;
    std::size_t m_loadedBytes = 0;
};
} // namespace SkullbonezCore::Rendering
