/*
File: SkullbonezSource/Physics/PhysicsFixedList.h
Purpose:
  Defines runtime-reserved, compile-time-bounded list storage for physics state.

Summary:
  Physics hot-path owners need vector-like dense rows sized to the active scene,
  without steady-state reallocation. This type commits one aligned backing range
  during scene load, or under ReplayPrediction's already-approved replay growth
  scope, retains one allocator-registry handle and a concrete sizing reason,
  constructs only the live prefix, and treats growth past the runtime
  reservation or compile-time cap as a policy failure. Each direct instance
  claims capacity-row publication only when the allocator registry has no
  canonical publisher for that owner.

Glossary:
  Fixed-capacity list: Contiguous storage with a runtime reservation and a
    compile-time absolute maximum.
  Live count: Number of initialized entries currently visible to callers.
  Capacity cap: Compile-time maximum entry count that replaces vector capacity.
  Capacity reason: Scene quantity or fixed semantic bound that sizes one owner.
  Canonical publisher: First live list instance holding the allocator token for
    its conceptual owner's capacity row.

Invariants:
  - Construction registers one nonzero Physics owner handle in the allocator's
    fixed registry; registration itself does not allocate.
  - Reserve allocates through that registered SceneLoad owner, except that
    ReplayPrediction may use its existing registered owner and approved Replay
    growth scope; the list never creates Replay authority itself.
  - Only indices below the live count hold constructed T instances.
  - Runtime-reservation and compile-time overflow both fail loudly.
  - Copy and move are deleted because both used to hide phase-gated allocation
    behind ordinary value syntax; concrete Physics owners perform any approved
    replay prediction clone explicitly.
  - Iteration exposes the live prefix as ordinary contiguous pointers, and the
    list retains and publishes its own monotonic per-scene live-count high-water.
  - Capacity-session changes reset the local peak lazily before the next
    publication; retained stores do not carry a preceding scene's peak forward.
  - Noncanonical destruction cannot clear the canonical capacity row.

Related:
  - SkullbonezSource/Physics/PhysicsBodyStore.h
  - SkullbonezSource/Physics/ColliderStore.h
  - AGENTS.md (Runtime Static Allocation Policy)
*/
#pragma once

#include "../Core/Allocation/RuntimeReserveAllocator.h"

#include <cassert>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

namespace SkullbonezCore
{
namespace Physics
{
namespace PhysicsCapacityReason
{
inline constexpr char SceneBodies[] = "Exact scene body count";
inline constexpr char SceneColliders[] = "Exact scene collider count";
inline constexpr char BodyHandleSlots[] = "Maximum of exact scene body count and retained body handle-slot high-water";
inline constexpr char
    ColliderHandleSlots[] = "Maximum of exact scene collider count and retained collider handle-slot high-water";
inline constexpr char SphereColliders[] = "Exact scene sphere-collider count";
inline constexpr char BoxColliders[] = "Exact scene box-collider count";
inline constexpr char HullColliders[] = "Distinct shareable plus explicitly unique scene convex-hull variant count";
inline constexpr char PointJoints[] = "Exact authored and ragdoll point-joint count";
inline constexpr char CandidatePairs[] = "Minimum of the scene body pair count and the compile-time candidate-pair ceiling";
inline constexpr char
    PersistentContacts[] = "Four manifold points per candidate pair plus eight terrain points per scene body";
inline constexpr char PipelineRecords[] = "Fixed 4096-record physics pipeline trace ceiling";
inline constexpr char CollisionVisualBodies[] = "Two body references per bounded candidate pair";
inline constexpr char MutualGravityPairs[] = "Pair count for the first min(scene body count, 512) bodies";
inline constexpr char SpatialGridPersistentEntries
    [] = "Eight cells per scene body plus a fixed 32-row spill covering the measured 19-row oversized-shape excess";
inline constexpr char SpatialGridPairDedupWords[] = "Triangular scene body-pair identities rounded up to 64-bit dedup words";
inline constexpr char SpatialGridBodyMemberships[] = "Exact scene body count for persistent broadphase membership";
inline constexpr char SpatialGridCandidatePairHeads[] = "Exact scene body count for canonical candidate-pair head rows";
inline constexpr char SpatialGridCellObjectSeen[] = "Exact scene body count for per-cell dedup generation stamps";
inline constexpr char SpatialGridCandidatePairNodes[] = "Four canonical candidate-pair nodes per scene body";
inline constexpr char SpatialGridCandidatePairSortKeys[] = "Four canonical candidate-pair sort-key rows per scene body";
inline constexpr char
    SpatialGridCandidatePairSortScratch[] = "Four canonical candidate-pair radix-sort scratch rows per scene body";
inline constexpr char SpatialGridSweptOverlayEntries[] = "Fixed 4096-row transient swept broadphase occupancy ceiling";
inline constexpr char ExplicitTestCapacity[] = "Explicit unit-test fixed-list capacity";
} // namespace PhysicsCapacityReason

template <typename T, std::size_t Capacity> class PhysicsFixedList
{
  public:
    using value_type = T;
    using iterator = T*;
    using const_iterator = const T*;

    explicit PhysicsFixedList( const char* ownerName, const char* capacityReason )
        : m_ownerName( ownerName ? ownerName : "PhysicsFixedList" ),
          m_capacityReason( capacityReason ? capacityReason : "Unspecified PhysicsFixedList capacity" ),
          m_ownerHandle( RegisterFixedOwner( m_ownerName, m_capacityReason ) ),
          m_capacityPublisher( SkullbonezCore::Core::Allocation::RuntimeReserveAllocator::ClaimCapacityPublisher( m_ownerHandle ) )
    {

        if ( m_ownerHandle == SkullbonezCore::Core::Allocation::INVALID_RUNTIME_RESERVE_OWNER )
        {
            FailOwnerRegistration();
        }
    }

    PhysicsFixedList( const PhysicsFixedList& ) = delete;
    PhysicsFixedList& operator=( const PhysicsFixedList& ) = delete;
    PhysicsFixedList( PhysicsFixedList&& ) = delete;
    PhysicsFixedList& operator=( PhysicsFixedList&& ) = delete;

    ~PhysicsFixedList()
    {
        clear();

        if ( m_capacityPublisher != SkullbonezCore::Core::Allocation::INVALID_RUNTIME_RESERVE_CAPACITY_PUBLISHER )
        {
            SkullbonezCore::Core::Allocation::RuntimeReserveAllocator::ReleaseCapacityPublisher( m_ownerHandle,
                                                                                                 m_capacityPublisher,
                                                                                                 static_cast<int>( m_highWater ) );
            m_capacityPublisher = SkullbonezCore::Core::Allocation::INVALID_RUNTIME_RESERVE_CAPACITY_PUBLISHER;
        }
    }

    bool empty() const
    {
        return m_count == 0u;
    }

    std::size_t size() const
    {
        return m_count;
    }

    std::size_t capacity() const
    {
        return m_runtimeCapacity;
    }

    static constexpr std::size_t max_capacity()
    {
        return Capacity;
    }

    std::size_t committed_bytes() const
    {
        return m_runtimeCapacity * sizeof( Storage );
    }

    std::size_t high_water() const
    {
        return m_capacitySessionGeneration ==
                       SkullbonezCore::Core::Allocation::RuntimeReserveAllocator::CapacitySessionGeneration()
                   ? m_highWater
                   : m_count;
    }

    SkullbonezCore::Core::Allocation::RuntimeReserveOwnerHandle owner_handle() const
    {
        return m_ownerHandle;
    }

    const char* capacity_reason() const
    {
        return m_capacityReason;
    }

    T* data()
    {
        return RawData();
    }

    const T* data() const
    {
        return RawData();
    }

    iterator begin()
    {
        return data();
    }

    iterator end()
    {
        return data() ? data() + m_count : nullptr;
    }

    const_iterator begin() const
    {
        return data();
    }

    const_iterator end() const
    {
        return data() ? data() + m_count : nullptr;
    }

    T& operator[]( std::size_t index )
    {
        return *ValueAt( index );
    }

    const T& operator[]( std::size_t index ) const
    {
        return *ValueAt( index );
    }

    T& back()
    {
        assert( m_count > 0u );
        return *ValueAt( m_count - 1u );
    }

    const T& back() const
    {
        assert( m_count > 0u );
        return *ValueAt( m_count - 1u );
    }

    void Reserve( std::size_t requested )
    {

        if ( requested > Capacity )
        {
            FailCapacityExceeded( requested, "compile_time_ceiling" );
        }

        if ( requested <= m_runtimeCapacity )
        {
            return;
        }

        using namespace SkullbonezCore::Core::Allocation;
        const RuntimeReservePhase phase = GetRuntimeAllocationPhase();

        if ( phase == RuntimeReservePhase::Replay )
        {
            const RuntimeReserveOwnerHandle owner = RuntimeReserveAllocator::CurrentOwner();

            // Replay prediction is the only post-gameplay consumer. Its outer
            // byte-budget owner and granted growth scope must already be active;
            // the list cannot manufacture replay authority for itself.

            if ( owner == INVALID_RUNTIME_RESERVE_OWNER ||
                 !RuntimeReserveAllocator::IsApprovedReplayGrowthAllocation( owner, static_cast<int>( phase ) ) )
            {
                FailReserveDenied( requested, phase );
            }

            CommitBacking( requested );
            return;
        }

        if ( phase != RuntimeReservePhase::SceneLoad )
        {
            FailReserveDenied( requested, phase );
        }

        const RuntimeReserveGrowthResult
            growth = RuntimeReserveAllocator::RequestGrowth( m_ownerHandle, { m_ownerName, m_ownerName, phase, -1,
                                                                              static_cast<int>( m_runtimeCapacity ),
                                                                              static_cast<int>( requested ),
                                                                              static_cast<int>( sizeof( Storage ) ) } );

        if ( !growth.granted )
        {
            FailReserveDenied( requested, phase );
        }

        RuntimeReserveOwnerScope ownerScope( m_ownerHandle );
        RuntimeReserveGrowthScope growthScope( m_ownerHandle, phase, growth );
        CommitBacking( requested );
        PublishUsage();
    }

    void reserve( std::size_t requested )
    {
        Reserve( requested );
    }

    void clear()
    {
        resize( 0u );
    }

    void ResetDefault( std::size_t count )
    {

        // Why: runtime phase owners need to replace a working set without
        // exposing an STL-growth spelling or bypassing this list's capacity
        // check, construction, destruction, and high-water accounting.
        clear();
        resize( count );
    }

    void ResetFill( std::size_t count, const T& value )
    {

        // Why: runtime owners replace bounded working sets without exposing
        // STL-growth vocabulary that would obscure the fixed-list policy.
        assign( count, value );
    }

    void ExtendDefaultTo( std::size_t count )
    {
        CheckCapacity( count );

        // Why: additional scene admission may enlarge indexed owner storage
        // while live rows still carry authoritative state. Construct only the
        // newly admitted suffix; shrinking or resetting the prefix here would
        // detach parallel stores from their stable body identities.

        while ( m_count < count )
        {
            new ( RawSlot( m_count ) ) T();
            ++m_count;
        }

        TrackHighWater();
    }

    void resize( std::size_t count )
    {
        CheckCapacity( count );

        while ( m_count > count )
        {
            DestroyLast();
        }

        while ( m_count < count )
        {
            new ( RawSlot( m_count ) ) T();
            ++m_count;
        }

        TrackHighWater();
    }

    void resize( std::size_t count, const T& value )
    {
        CheckCapacity( count );

        while ( m_count > count )
        {
            DestroyLast();
        }

        while ( m_count < count )
        {
            new ( RawSlot( m_count ) ) T( value );
            ++m_count;
        }

        TrackHighWater();
    }

    void assign( std::size_t count, const T& value )
    {
        CheckCapacity( count );
        const std::size_t common = ( count < m_count ) ? count : m_count;

        for ( std::size_t index = 0; index < common; ++index )
        {
            *ValueAt( index ) = value;
        }

        while ( m_count > count )
        {
            DestroyLast();
        }

        while ( m_count < count )
        {
            new ( RawSlot( m_count ) ) T( value );
            ++m_count;
        }

        TrackHighWater();
    }

    void push_back( const T& value )
    {
        CheckCapacity( m_count + 1u );
        new ( RawSlot( m_count ) ) T( value );
        ++m_count;
        TrackHighWater();
    }

    void push_back( T&& value )
    {
        CheckCapacity( m_count + 1u );
        new ( RawSlot( m_count ) ) T( std::move( value ) );
        ++m_count;
        TrackHighWater();
    }

    void Append( const T& value )
    {
        push_back( value );
    }

    void Append( T&& value )
    {
        push_back( std::move( value ) );
    }

    template <typename... Args> T& emplace_back( Args&&... args )
    {
        CheckCapacity( m_count + 1u );
        new ( RawSlot( m_count ) ) T( std::forward<Args>( args )... );
        ++m_count;
        TrackHighWater();
        return back();
    }

    iterator erase( iterator first, iterator last )
    {

        if ( first == last )
        {
            return first;
        }

        assert( data() && first >= begin() && first <= end() && last >= first && last <= end() );
        const std::size_t firstIndex = static_cast<std::size_t>( first - begin() );
        const std::size_t removedCount = static_cast<std::size_t>( last - first );

        for ( std::size_t index = firstIndex; index + removedCount < m_count; ++index )
        {
            *ValueAt( index ) = std::move( *ValueAt( index + removedCount ) );
        }

        resize( m_count - removedCount );
        return begin() + firstIndex;
    }

    void pop_back()
    {
        assert( m_count > 0u );

        if ( m_count == 0u )
        {
            FailPopFromEmpty();
        }

        DestroyLast();
        TrackHighWater();
    }

  private:
    static SkullbonezCore::Core::Allocation::RuntimeReserveOwnerHandle RegisterFixedOwner( const char* ownerName,
                                                                                           const char* capacityReason )
    {
        using namespace SkullbonezCore::Core::Allocation;
        return RuntimeReserveAllocator::RegisterOwner( { ownerName, RuntimeReserveSubsystem::Physics,
                                                         RuntimeReservePhase::SceneLoad, 0, static_cast<int>( Capacity ), 0,
                                                         false, capacityReason, false, static_cast<int>( sizeof( T ) ) } );
    }

    struct alignas( alignof( T ) ) Storage
    {
        unsigned char bytes[sizeof( T )];
    };
    static_assert( sizeof( Storage ) == sizeof( T ), "PhysicsFixedList storage must preserve T stride." );
    static_assert( alignof( Storage ) == alignof( T ), "PhysicsFixedList storage must preserve T alignment." );
    static constexpr std::size_t STORAGE_ALIGNMENT = alignof( T ) > 32u ? alignof( T ) : 32u;

    struct StorageDeleter
    {
        void operator()( Storage* storage ) const noexcept
        {
            ::operator delete[]( storage, std::align_val_t( STORAGE_ALIGNMENT ) );
        }
    };

    using StoragePointer = std::unique_ptr<Storage, StorageDeleter>;

    static StoragePointer AllocateStorage( std::size_t count )
    {

        if ( count == 0u )
        {
            return {};
        }

        void* storage = ::operator new[]( count * sizeof( Storage ), std::align_val_t( STORAGE_ALIGNMENT ) );
        return StoragePointer( static_cast<Storage*>( storage ) );
    }

    // Lifetime: placement construction uses the raw slot before T exists.
    // ValueAt is only for already-live entries, where std::launder is valid.
    // Why: the byte buffer has no T object until placement construction. These
    // helpers are the one representation seam, and launder re-establishes the
    // typed pointer after each slot lifetime begins.
    void* RawSlot( std::size_t index )
    {
        return static_cast<void*>( m_values.get() + index );
    }

    T* RawData()
    {
        return m_values ? reinterpret_cast<T*>( m_values.get() ) : nullptr;
    }

    const T* RawData() const
    {
        return m_values ? reinterpret_cast<const T*>( m_values.get() ) : nullptr;
    }

    T* ValueAt( std::size_t index )
    {
        return std::launder( reinterpret_cast<T*>( m_values.get() + index ) );
    }

    const T* ValueAt( std::size_t index ) const
    {
        return std::launder( reinterpret_cast<const T*>( m_values.get() + index ) );
    }

    void CheckCapacity( std::size_t requested ) const
    {

        if ( requested > Capacity )
        {
            FailCapacityExceeded( requested, "compile_time_ceiling" );
        }

        if ( requested > m_runtimeCapacity )
        {
            FailCapacityExceeded( requested, "runtime_reservation" );
        }
    }

    void RelocateInto( Storage* replacement )
    {

        if constexpr ( std::is_trivially_copyable<T>::value )
        {

            if ( m_count > 0u )
            {
                std::memcpy( replacement, m_values.get(), m_count * sizeof( Storage ) );
            }
        }
        else
        {
            std::size_t constructed = 0u;

#if defined( _CPPUNWIND )
            try
            {
#endif

                for ( ; constructed < m_count; ++constructed )
                {
                    new ( static_cast<void*>( &replacement[constructed] ) )
                        T( std::move_if_noexcept( *ValueAt( constructed ) ) );
                }

#if defined( _CPPUNWIND )
            }
            catch ( ... )
            {

                while ( constructed > 0u )
                {
                    --constructed;
                    std::launder( reinterpret_cast<T*>( &replacement[constructed] ) )->~T();
                }

                throw;
            }
#endif

            for ( std::size_t index = 0; index < m_count; ++index )
            {
                ValueAt( index )->~T();
            }
        }
    }

    void CommitBacking( std::size_t requested )
    {
        StoragePointer replacement = AllocateStorage( requested );
        RelocateInto( replacement.get() );
        m_values = std::move( replacement );
        m_runtimeCapacity = requested;
    }

    void DestroyLivePrefix() noexcept
    {

        while ( m_count > 0u )
        {
            --m_count;
            ValueAt( m_count )->~T();
        }
    }

    void DestroyLast()
    {
        --m_count;
        ValueAt( m_count )->~T();
    }

    void PublishUsage()
    {

        if ( m_capacityPublisher == SkullbonezCore::Core::Allocation::INVALID_RUNTIME_RESERVE_CAPACITY_PUBLISHER )
        {
            return;
        }

        SyncCapacitySession();
        SkullbonezCore::Core::Allocation::RuntimeReserveAllocator::PublishCapacityUsage( m_ownerHandle, m_capacityPublisher,
                                                                                         static_cast<int>( m_runtimeCapacity ),
                                                                                         static_cast<int>( m_count ),
                                                                                         static_cast<int>( m_highWater ) );
    }

    void SyncCapacitySession()
    {
        const uint64_t generation = SkullbonezCore::Core::Allocation::RuntimeReserveAllocator::CapacitySessionGeneration();

        if ( generation != m_capacitySessionGeneration )
        {
            m_capacitySessionGeneration = generation;
            m_highWater = m_count;
        }
    }

    void TrackHighWater()
    {
        SyncCapacitySession();

        if ( m_count > m_highWater )
        {
            m_highWater = m_count;
        }

        PublishUsage();
    }

    [[noreturn]] void FailCapacityExceeded( std::size_t requested, const char* ceiling ) const
    {
        const char* phaseName = SkullbonezCore::Core::Allocation::RuntimeReservePhaseName( SkullbonezCore::Core::Allocation::GetRuntimeAllocationPhase() );
        std::fprintf( stderr,
                      "FATAL: PhysicsFixedList capacity exceeded owner=%s requested=%zu runtime_capacity=%zu "
                      "compile_capacity=%zu count=%zu high_water=%zu ceiling=%s phase=%s.\n",
                      m_ownerName, requested, m_runtimeCapacity, Capacity, m_count, m_highWater, ceiling, phaseName );
        std::fprintf( stdout,
                      "FATAL: PhysicsFixedList capacity exceeded owner=%s requested=%zu runtime_capacity=%zu "
                      "compile_capacity=%zu count=%zu high_water=%zu ceiling=%s phase=%s.\n",
                      m_ownerName, requested, m_runtimeCapacity, Capacity, m_count, m_highWater, ceiling, phaseName );
        std::fflush( stderr );
        std::fflush( stdout );
        assert( false && "PhysicsFixedList capacity exceeded" );
        std::abort();
    }

    [[noreturn]] void FailReserveDenied( std::size_t requested,
                                         SkullbonezCore::Core::Allocation::RuntimeReservePhase phase ) const
    {
        const char* phaseName = SkullbonezCore::Core::Allocation::RuntimeReservePhaseName( phase );
        std::fprintf( stderr,
                      "FATAL: PhysicsFixedList reserve denied owner=%s requested=%zu runtime_capacity=%zu "
                      "compile_capacity=%zu phase=%s.\n",
                      m_ownerName, requested, m_runtimeCapacity, Capacity, phaseName );
        std::fprintf( stdout,
                      "FATAL: PhysicsFixedList reserve denied owner=%s requested=%zu runtime_capacity=%zu "
                      "compile_capacity=%zu phase=%s.\n",
                      m_ownerName, requested, m_runtimeCapacity, Capacity, phaseName );
        std::fflush( stderr );
        std::fflush( stdout );
        assert( false && "PhysicsFixedList reserve denied" );
        std::abort();
    }

    [[noreturn]] void FailOwnerRegistration() const
    {
        std::fprintf( stderr, "FATAL: PhysicsFixedList owner registration failed owner=%s reason=%s.\n", m_ownerName,
                      m_capacityReason );
        std::fprintf( stdout, "FATAL: PhysicsFixedList owner registration failed owner=%s reason=%s.\n", m_ownerName,
                      m_capacityReason );
        std::fflush( stderr );
        std::fflush( stdout );
        assert( false && "PhysicsFixedList owner registration failed" );
        std::abort();
    }

    [[noreturn]] void FailPopFromEmpty() const
    {
        std::fprintf( stderr, "FATAL: PhysicsFixedList pop from empty list owner=%s.\n", m_ownerName );
        std::fprintf( stdout, "FATAL: PhysicsFixedList pop from empty list owner=%s.\n", m_ownerName );
        std::fflush( stderr );
        std::fflush( stdout );
        assert( false && "PhysicsFixedList pop from empty list" );
        std::abort();
    }

    StoragePointer m_values;
    std::size_t m_count = 0u;
    std::size_t m_highWater = 0u;
    std::size_t m_runtimeCapacity = 0u;
    const char* m_ownerName = "PhysicsFixedList";
    const char* m_capacityReason = "Unspecified PhysicsFixedList capacity";
    SkullbonezCore::Core::Allocation::RuntimeReserveOwnerHandle
        m_ownerHandle = SkullbonezCore::Core::Allocation::INVALID_RUNTIME_RESERVE_OWNER;
    SkullbonezCore::Core::Allocation::RuntimeReserveCapacityPublisherToken
        m_capacityPublisher = SkullbonezCore::Core::Allocation::INVALID_RUNTIME_RESERVE_CAPACITY_PUBLISHER;
    uint64_t m_capacitySessionGeneration = 0u;
};
} // namespace Physics
} // namespace SkullbonezCore
