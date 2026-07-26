/*
File: SkullbonezSource/Core/TracyClientOwner.h
Purpose:
  Declares the development-only Tracy process owner and zero-cost marker seams.

Summary:
  One startup-owned object brackets Tracy's explicitly selected manual
  lifetime. Fixed source locations translate engine-owner intervals into Tracy
  zones, and frame, plot, allocation, and worker call sites use macros that
  disappear completely when Tracy is not compiled.

Glossary:
  Manual lifetime: Tracy mode where the engine explicitly starts and stops the
    client instead of relying on static initialization and destruction.
  Submitted-frame mark: One Tracy frame boundary emitted only after DX12
    Present succeeds.
  Composite main lane: The current engine thread that owns main-loop, render,
    replay/prediction coordination, and cold IO work.
  Standard capture: Explicit SKORE_TRACY_MODE=standard selection, or the ImGui
    cold-start command, that enables owner zones and capacity plots without
    call stacks or allocation events.
  Heavy capture: Explicit SKORE_TRACY_MODE=heavy selection that adds call stacks
    and global C++ allocation events to the standard owner-zone capture.

Invariants:
  - TRACY_ENABLE is valid only with SKULLBONEZ_DEVELOPMENT_TOOLS.
  - A startup-selected client begins before the initial engine workers. An
    interactive Standard start is followed by worker recreation before the
    next simulation tick, and shutdown still happens after every worker joins.
  - Disabled marker macros do not evaluate their arguments.
  - Standard mode emits no call stacks or allocation events.
  - Source-location and zone-token storage is fixed and bounded.
  - Status snapshots are fixed values; copying them allocates no memory and
    performs no socket or process query.

Related:
  - SkullbonezSource/Core/TracyClientOwner.cpp
  - SkullbonezSource/Core/Allocation/DevelopmentToolAllocation.h
  - ThirdPtySource/tracy/manual/tracy.md
*/
#pragma once

#include <cstdint>

#if defined( TRACY_ENABLE ) && !defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
#error "TRACY_ENABLE requires the shared SKULLBONEZ_DEVELOPMENT_TOOLS capability."
#endif

#if defined( TRACY_ENABLE )
namespace SkullbonezCore::Core::DevelopmentTools
{
struct TracyClientStatus
{
    bool buildEnabled = true;
    bool initialized = false;
    bool viewerConnected = false;
    bool heavyMode = false;
};

struct TracyZoneToken
{
    uint32_t id = 0u;
    int32_t active = 0;
    uint64_t connectionId = 0u;
};

class TracyClientOwner
{
  public:
    TracyClientOwner() noexcept = default;
    ~TracyClientOwner();

    TracyClientOwner( const TracyClientOwner& ) = delete;
    TracyClientOwner& operator=( const TracyClientOwner& ) = delete;

    void Start();

    // Starts the lightweight Standard capture in response to a cold editor
    // command. The caller must recreate engine workers before simulation
    // resumes so their Tracy thread names belong to the new client lifetime.
    [[nodiscard]] bool StartStandardCapture();
    void Shutdown() noexcept;

    static TracyClientStatus CopyStatus() noexcept;
    static void MarkSubmittedFrame() noexcept;
    static void NameWorkerThread( int workerIndex ) noexcept;

    // Registers a borrowed process-lifetime owner path in fixed storage. Zero
    // means the bounded registry is unavailable or exhausted.
    static uint32_t RegisterOwnerZone( const char* fullPath, uint32_t hash ) noexcept;

    // Begins/ends one zone on the calling thread. End discards tokens from a
    // disconnected or superseded viewer session.
    static TracyZoneToken BeginOwnerZone( uint32_t sourceLocationHandle ) noexcept;
    static void EndOwnerZone( TracyZoneToken token ) noexcept;

    // Plot names must have process lifetime because Tracy records their address.
    static void PublishPlot( const char* name, double value ) noexcept;
    static void PublishDevelopmentAllocationPlots() noexcept;

  private:
    bool m_started = false;
};

class TracyOwnerZoneScope
{
  public:
    explicit TracyOwnerZoneScope( uint32_t sourceLocationHandle ) noexcept
        : m_token( TracyClientOwner::BeginOwnerZone( sourceLocationHandle ) )
    {
    }
    ~TracyOwnerZoneScope()
    {
        TracyClientOwner::EndOwnerZone( m_token );
    }

    TracyOwnerZoneScope( const TracyOwnerZoneScope& ) = delete;
    TracyOwnerZoneScope& operator=( const TracyOwnerZoneScope& ) = delete;

  private:
    TracyZoneToken m_token;
};
} // namespace SkullbonezCore::Core::DevelopmentTools

#define SKORE_TRACY_PASTE_INNER( a, b ) a##b
#define SKORE_TRACY_PASTE( a, b ) SKORE_TRACY_PASTE_INNER( a, b )
#define SKORE_TRACY_SCOPED_OWNER_ZONE_IMPL( name, hash, line )                                                              \
    static const uint32_t                                                                                                   \
        SKORE_TRACY_PASTE( _skoreTracySource_,                                                                              \
                           line ) = ::SkullbonezCore::Core::DevelopmentTools::TracyClientOwner::RegisterOwnerZone( name,    \
                                                                                                                   hash );  \
    ::SkullbonezCore::Core::DevelopmentTools::TracyOwnerZoneScope SKORE_TRACY_PASTE( _skoreTracyScope_, line )(             \
        SKORE_TRACY_PASTE( _skoreTracySource_, line ) )
#define SKORE_TRACY_SCOPED_OWNER_ZONE( name, hash ) SKORE_TRACY_SCOPED_OWNER_ZONE_IMPL( name, hash, __LINE__ )

#define SKORE_TRACY_MARK_SUBMITTED_FRAME() ::SkullbonezCore::Core::DevelopmentTools::TracyClientOwner::MarkSubmittedFrame()
#define SKORE_TRACY_NAME_WORKER_THREAD( workerIndex )                                                                       \
    ::SkullbonezCore::Core::DevelopmentTools::TracyClientOwner::NameWorkerThread( workerIndex )
#define SKORE_TRACY_REGISTER_OWNER_ZONE( fullPath, hash )                                                                   \
    ::SkullbonezCore::Core::DevelopmentTools::TracyClientOwner::RegisterOwnerZone( fullPath, hash )
#define SKORE_TRACY_BEGIN_OWNER_ZONE( sourceLocationHandle )                                                                \
    ::SkullbonezCore::Core::DevelopmentTools::TracyClientOwner::BeginOwnerZone( sourceLocationHandle )
#define SKORE_TRACY_END_OWNER_ZONE( token ) ::SkullbonezCore::Core::DevelopmentTools::TracyClientOwner::EndOwnerZone( token )
#define SKORE_TRACY_PLOT_VALUE( name, value )                                                                               \
    ::SkullbonezCore::Core::DevelopmentTools::TracyClientOwner::PublishPlot( name, static_cast<double>( value ) )
#else

// Invariant: the argument tokens are discarded, so disabled builds cannot pay
// formatting, function-call, allocation, or side-effect cost by accident.
#define SKORE_TRACY_MARK_SUBMITTED_FRAME() ( (void)0 )
#define SKORE_TRACY_NAME_WORKER_THREAD( workerIndex ) ( (void)0 )
#define SKORE_TRACY_SCOPED_OWNER_ZONE( name, hash ) ( (void)0 )
#define SKORE_TRACY_REGISTER_OWNER_ZONE( fullPath, hash ) 0u
#define SKORE_TRACY_BEGIN_OWNER_ZONE( sourceLocationHandle ) 0u
#define SKORE_TRACY_END_OWNER_ZONE( token ) ( (void)0 )
#define SKORE_TRACY_PLOT_VALUE( name, value ) ( (void)0 )
#endif
