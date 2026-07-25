/*
File: SkullbonezSource/Rendering/RenderGpuTimingOwner.h
Purpose:
  Owns renderer timestamp queries and platform GPU ranges for profiler markers.

Summary:
  RenderGpuTimingOwner is the explicit one-way seam between Core marker history
  and renderer diagnostics. Render passes record through this concrete owner;
  completed query values return to Core as fixed GpuTimingSample spans.

Glossary:
  Record range: CPU interval spent recording commands for one GPU marker.
  Marker epoch: Core identity generation advanced when the registry resets.
  GPU timing sample: Completed timestamp result keyed by Core marker hash.

Invariants:
  - The owner borrows one startup Profiler and renderer diagnostics facet for
    the RuntimeRenderer lifetime; neither pointer is stored in Core.
  - Open ranges and completed samples use fixed arrays bounded by Profiler
    capacities, so steady render frames allocate nothing.
  - Device invalidation clears backend queries before Core timing history.

Related:
  - SkullbonezSource/Core/Profiler.h
  - SkullbonezSource/Rendering/DX12/Dx12Diagnostics.h
  - SkullbonezSource/Runtime/Render/RuntimeRenderer.h
*/
#pragma once

#include "../Core/Profiler.h"

#include <cstdint>

namespace SkullbonezCore
{
namespace Rendering
{
class Dx12Diagnostics;

class RenderGpuTimingOwner
{
  public:
    RenderGpuTimingOwner( Core::Profiler* profiler, Dx12Diagnostics* diagnostics );

    // Frame boundary: synchronize marker epochs, read completed timestamps,
    // and publish the preceding renderer counters before they are reset.
    void BeginFrame();
    void InvalidateDevice();

    void Begin( const char* fullPath, uint32_t hash );
    void End( const char* fullPath, uint32_t hash );

  private:
    struct OpenScope
    {
        const char* fullPath = nullptr;       // Borrowed marker literal; valid for the process lifetime.
        uint32_t hash = 0;
        int markerIndex = -1;                 // Core row mirrored by the backend query slot.
        bool timerOpen = false;
        bool platformEventOpen = false;
    };

    Core::Profiler* m_profiler = nullptr;     // Startup owner outlives RuntimeRenderer.
    Dx12Diagnostics* m_diagnostics = nullptr; // Concrete diagnostics owner outlives RuntimeRenderer.
    OpenScope m_openScopes[Core::Profiler::MAX_DEPTH] = {};
    Core::Profiler::GpuTimingSample m_completedSamples[Core::Profiler::MAX_MARKERS] = {};
    int m_openDepth = 0;
    uint32_t m_markerEpoch = 0;
};

class RenderGpuTimingScope
{
  public:
    RenderGpuTimingScope( RenderGpuTimingOwner* owner, const char* fullPath, uint32_t hash )
        : m_owner( owner ), m_fullPath( fullPath ), m_hash( hash )
    {
        if ( m_owner )
        {
            m_owner->Begin( m_fullPath, m_hash );
        }
    }
    ~RenderGpuTimingScope()
    {
        if ( m_owner )
        {
            m_owner->End( m_fullPath, m_hash );
        }
    }
    RenderGpuTimingScope( const RenderGpuTimingScope& ) = delete;
    RenderGpuTimingScope& operator=( const RenderGpuTimingScope& ) = delete;

  private:
    RenderGpuTimingOwner* m_owner;
    const char* m_fullPath;
    uint32_t m_hash;
};
} // namespace Rendering
} // namespace SkullbonezCore

#define RENDER_PROFILE_PASTE_INNER( a, b ) a##b
#define RENDER_PROFILE_PASTE( a, b ) RENDER_PROFILE_PASTE_INNER( a, b )

#if defined( SKULLBONEZ_PROFILE_ENABLED )

#define PROFILE_GPU_BEGIN( owner, name )                                                                               \
    do                                                                                                                 \
    {                                                                                                                  \
        constexpr uint32_t RENDER_PROFILE_PASTE( _gpuH_, __LINE__ ) = ::HashStr( name );                               \
        auto* RENDER_PROFILE_PASTE( _gpuOwner_, __LINE__ ) = ( owner );                                                \
        if ( RENDER_PROFILE_PASTE( _gpuOwner_, __LINE__ ) )                                                            \
            RENDER_PROFILE_PASTE( _gpuOwner_, __LINE__ )->Begin( name, RENDER_PROFILE_PASTE( _gpuH_, __LINE__ ) );     \
    } while ( 0 )

#define PROFILE_GPU_END( owner, name )                                                                                 \
    do                                                                                                                 \
    {                                                                                                                  \
        constexpr uint32_t RENDER_PROFILE_PASTE( _gpuH_, __LINE__ ) = ::HashStr( name );                               \
        auto* RENDER_PROFILE_PASTE( _gpuOwner_, __LINE__ ) = ( owner );                                                \
        if ( RENDER_PROFILE_PASTE( _gpuOwner_, __LINE__ ) )                                                            \
            RENDER_PROFILE_PASTE( _gpuOwner_, __LINE__ )->End( name, RENDER_PROFILE_PASTE( _gpuH_, __LINE__ ) );       \
    } while ( 0 )

#define PROFILE_GPU_SCOPED( owner, name )                                                                              \
    constexpr uint32_t RENDER_PROFILE_PASTE( _gpuSH_, __LINE__ ) = ::HashStr( name );                                  \
    ::SkullbonezCore::Rendering::RenderGpuTimingScope RENDER_PROFILE_PASTE( _gpuScope_, __LINE__ )(                    \
        owner,                                                                                                         \
        name,                                                                                                          \
        RENDER_PROFILE_PASTE( _gpuSH_, __LINE__ )                                                                      \
    )

#else

#define PROFILE_GPU_BEGIN( owner, name ) ( (void)( owner ) )
#define PROFILE_GPU_END( owner, name ) ( (void)( owner ) )
#define PROFILE_GPU_SCOPED( owner, name ) ( (void)( owner ) )

#endif
