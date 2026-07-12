/*
File: SkullbonezTests/TestDx12CachedPsoStore.cpp
Purpose:
  Proves persistent DX12 PSO recipe names are stable and complete.

Summary:
  A cached-PSO record name is a content address. Repeating an identical recipe
  must reproduce the name across processes; changing any cache-invalidating
  identity must select a different entry.

Glossary:
  Manifest digest: Identity of compiler settings and all shipped shader stages.
  Root digest: Identity of serialized UnifiedRaster binding bytes.

Invariants:
  - A COM pointer value never changes persistent identity.
  - Shader bytes, manifest/root identity, input layout, and fixed state do.

Related:
  - SkullbonezSource/Rendering/DX12/Dx12CachedPsoStore.cpp
  - Agentic/Reports/2026-07-12/shader-pipeline-modernization-closure.md
*/
#include "../SkullbonezSource/Rendering/DX12/Dx12CachedPsoStore.h"
#include "../ThirdPtySource/doctest/doctest.h"

#include <array>
#include <cwchar>

using SkullbonezCore::Rendering::Dx12CachedPsoStore;

namespace
{
D3D12_GRAPHICS_PIPELINE_STATE_DESC MakeRecipe( const std::array<std::uint8_t, 8>& vs,
                                               const std::array<std::uint8_t, 8>& ps,
                                               const D3D12_INPUT_ELEMENT_DESC* input )
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
    desc.pRootSignature = reinterpret_cast<ID3D12RootSignature*>( 0x1234 );
    desc.VS = { vs.data(), vs.size() };
    desc.PS = { ps.data(), ps.size() };
    desc.InputLayout = { input, 1 };
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    desc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
    desc.RasterizerState.DepthClipEnable = TRUE;
    desc.DepthStencilState.DepthEnable = TRUE;
    desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    desc.SampleMask = UINT_MAX;
    desc.NumRenderTargets = 1;
    desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    desc.SampleDesc.Count = 1;
    return desc;
}

using Name = wchar_t[Dx12CachedPsoStore::ENTRY_NAME_CHARS];

bool BuildName( const D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc,
                const std::array<std::uint8_t, Dx12CachedPsoStore::DIGEST_BYTES>& manifest,
                const std::array<std::uint8_t, Dx12CachedPsoStore::DIGEST_BYTES>& root,
                Name& name )
{
    return Dx12CachedPsoStore::BuildPersistentEntryNameForTest( desc, manifest, root, name );
}
} // namespace

TEST_CASE( "DX12 persistent PSO identity is stable and pointer independent" )
{
    const std::array<std::uint8_t, 8> vs = { 1, 2, 3, 4, 5, 6, 7, 8 };
    const std::array<std::uint8_t, 8> ps = { 8, 7, 6, 5, 4, 3, 2, 1 };
    const D3D12_INPUT_ELEMENT_DESC input =
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };
    std::array<std::uint8_t, Dx12CachedPsoStore::DIGEST_BYTES> manifest = {};
    std::array<std::uint8_t, Dx12CachedPsoStore::DIGEST_BYTES> root = {};
    manifest[0] = 11;
    root[0] = 22;
    auto desc = MakeRecipe( vs, ps, &input );
    Name first = {};
    Name repeated = {};
    REQUIRE( BuildName( desc, manifest, root, first ) );
    REQUIRE( BuildName( desc, manifest, root, repeated ) );
    CHECK( std::wcslen( first ) == Dx12CachedPsoStore::ENTRY_NAME_CHARS - 1 );
    CHECK( std::wcscmp( first, repeated ) == 0 );

    desc.pRootSignature = reinterpret_cast<ID3D12RootSignature*>( 0x9876 );
    Name pointerChanged = {};
    REQUIRE( BuildName( desc, manifest, root, pointerChanged ) );
    CHECK( std::wcscmp( first, pointerChanged ) == 0 );

    const std::array<std::uint8_t, 4> cachedBytes = { 9, 8, 7, 6 };
    desc.CachedPSO = { cachedBytes.data(), cachedBytes.size() };
    Name cacheAttached = {};
    REQUIRE( BuildName( desc, manifest, root, cacheAttached ) );
    CHECK( std::wcscmp( first, cacheAttached ) == 0 );
}

TEST_CASE( "DX12 persistent PSO identity invalidates every owner boundary" )
{
    std::array<std::uint8_t, 8> vs = { 1, 2, 3, 4, 5, 6, 7, 8 };
    const std::array<std::uint8_t, 8> ps = { 8, 7, 6, 5, 4, 3, 2, 1 };
    D3D12_INPUT_ELEMENT_DESC input =
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };
    std::array<std::uint8_t, Dx12CachedPsoStore::DIGEST_BYTES> manifest = {};
    std::array<std::uint8_t, Dx12CachedPsoStore::DIGEST_BYTES> root = {};
    auto desc = MakeRecipe( vs, ps, &input );
    Name baseline = {};
    REQUIRE( BuildName( desc, manifest, root, baseline ) );

    auto differs = [&]( const D3D12_GRAPHICS_PIPELINE_STATE_DESC& candidate,
                        const auto& candidateManifest,
                        const auto& candidateRoot )
    {
        Name changed = {};
        REQUIRE( BuildName( candidate, candidateManifest, candidateRoot, changed ) );
        CHECK( std::wcscmp( baseline, changed ) != 0 );
    };

    auto changedManifest = manifest;
    changedManifest[31] = 1;
    differs( desc, changedManifest, root );
    auto changedRoot = root;
    changedRoot[31] = 1;
    differs( desc, manifest, changedRoot );

    vs[0] ^= 0xff;
    desc.VS = { vs.data(), vs.size() };
    differs( desc, manifest, root );
    vs[0] ^= 0xff;
    desc.VS = { vs.data(), vs.size() };

    desc.BlendState.RenderTarget[0].BlendEnable = TRUE;
    differs( desc, manifest, root );
    desc.BlendState.RenderTarget[0].BlendEnable = FALSE;
    input.Format = DXGI_FORMAT_R32G32_FLOAT;
    differs( desc, manifest, root );
}
