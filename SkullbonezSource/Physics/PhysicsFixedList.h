/*
File: SkullbonezSource/Physics/PhysicsFixedList.h
Purpose:
  Defines runtime-reserved, compile-time-bounded list storage for physics state.

Summary:
  Physics hot-path owners need vector-like dense rows sized to the active scene,
  without steady-state reallocation. This type commits one aligned backing range
  during scene load, or under ReplayPrediction's already-approved replay growth
  scope, constructs only the live prefix, and treats growth past the runtime
  reservation or compile-time cap as a policy failure.

Glossary:
  Fixed-capacity list: Contiguous storage with a runtime reservation and a
    compile-time absolute maximum.
  Live count: Number of initialized entries currently visible to callers.
  Capacity cap: Compile-time maximum entry count that replaces vector capacity.

Invariants:
  - Reserve allocates through a registered SceneLoad owner, except that
    ReplayPrediction may use its existing registered owner and approved Replay
    growth scope; the list never creates Replay authority itself.
  - Only indices below the live count hold constructed T instances.
  - Runtime-reservation and compile-time overflow both fail loudly.
  - Iteration exposes the live prefix as ordinary contiguous pointers.

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
template <typename T, std::size_t Capacity> class PhysicsFixedList
{
  public:
    using value_type = T;
    using iterator = T*;
    using const_iterator = const T*;

    explicit PhysicsFixedList( const char* ownerName = "PhysicsFixedList" )
        : m_ownerName( ownerName ? ownerName : "PhysicsFixedList" )
    {
    }

    PhysicsFixedList( const PhysicsFixedList& other ) : m_ownerName( other.m_ownerName )
    {
        Reserve( other.m_count );
        CopyConstructFrom( other );
    }

    PhysicsFixedList& operator=( const PhysicsFixedList& other )
    {

        if ( this == &other )
        {
            return *this;
        }

        Reserve( other.m_count );
        clear();
        CopyConstructFrom( other );

        return *this;
    }

    PhysicsFixedList( PhysicsFixedList&& other ) noexcept( std::is_nothrow_move_constructible<T>::value )
        : m_ownerName( other.m_ownerName )
    {
        Reserve( other.m_count );
        MoveConstructFrom( other );
        other.clear();
    }

    PhysicsFixedList& operator=( PhysicsFixedList&& other ) noexcept( std::is_nothrow_move_constructible<T>::value )
    {

        if ( this == &other )
        {
            return *this;
        }

        Reserve( other.m_count );
        clear();
        MoveConstructFrom( other );
        other.clear();
        return *this;
    }

    ~PhysicsFixedList()
    {
        clear();
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

        const RuntimeReserveOwnerHandle owner = RuntimeReserveAllocator::RegisterOwner( { m_ownerName, RuntimeReserveSubsystem::Physics, RuntimeReservePhase::SceneLoad, 0, static_cast<int>( Capacity ),
                                                                                          0, false, "Scene-sized PhysicsFixedList backing storage" } );
        const RuntimeReserveGrowthResult
            growth = RuntimeReserveAllocator::RequestGrowth( owner, { m_ownerName, m_ownerName, phase, -1,
                                                                      static_cast<int>( m_runtimeCapacity ),
                                                                      static_cast<int>( requested ),
                                                                      static_cast<int>( sizeof( Storage ) ) } );

        if ( !growth.granted )
        {
            FailReserveDenied( requested, phase );
        }

        RuntimeReserveOwnerScope ownerScope( owner );
        RuntimeReserveGrowthScope growthScope( owner, phase, growth );
        CommitBacking( requested );
    }

    void reserve( std::size_t requested )
    {
        Reserve( requested );
    }

    void clear()
    {
        resize( 0u );
    }

    void resize( std::size_t count )
    {
        CheckCapacity( count );

        while ( m_count > count )
        {
            pop_back();
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
            pop_back();
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
            pop_back();
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

        --m_count;
        ValueAt( m_count )->~T();
    }

  private:
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

    void CopyConstructFrom( const PhysicsFixedList& other )
    {

        if constexpr ( std::is_trivially_copyable<T>::value )
        {

            if ( other.m_count > 0u )
            {
                std::memcpy( m_values.get(), other.m_values.get(), other.m_count * sizeof( Storage ) );
            }

            m_count = other.m_count;
        }
        else
        {
#if defined( _CPPUNWIND )
            try
            {
#endif

                for ( const T& value : other )
                {
                    new ( RawSlot( m_count ) ) T( value );
                    ++m_count;
                }

#if defined( _CPPUNWIND )
            }
            catch ( ... )
            {
                DestroyLivePrefix();
                throw;
            }
#endif
        }

        TrackHighWater();
    }

    void MoveConstructFrom( PhysicsFixedList& other )
    {

        if constexpr ( std::is_trivially_copyable<T>::value )
        {

            if ( other.m_count > 0u )
            {
                std::memcpy( m_values.get(), other.m_values.get(), other.m_count * sizeof( Storage ) );
            }

            m_count = other.m_count;
        }
        else
        {
#if defined( _CPPUNWIND )
            try
            {
#endif

                for ( T& value : other )
                {
                    new ( RawSlot( m_count ) ) T( std::move( value ) );
                    ++m_count;
                }

#if defined( _CPPUNWIND )
            }
            catch ( ... )
            {
                DestroyLivePrefix();
                throw;
            }
#endif
        }

        TrackHighWater();
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

    void TrackHighWater()
    {

        if ( m_count > m_highWater )
        {
            m_highWater = m_count;
        }
    }

    [[noreturn]] void FailCapacityExceeded( std::size_t requested, const char* ceiling ) const
    {
        std::fprintf( stderr,
                      "FATAL: PhysicsFixedList capacity exceeded owner=%s requested=%zu runtime_capacity=%zu "
                      "compile_capacity=%zu count=%zu high_water=%zu ceiling=%s.\n",
                      m_ownerName, requested, m_runtimeCapacity, Capacity, m_count, m_highWater, ceiling );
        std::fprintf( stdout,
                      "FATAL: PhysicsFixedList capacity exceeded owner=%s requested=%zu runtime_capacity=%zu "
                      "compile_capacity=%zu count=%zu high_water=%zu ceiling=%s.\n",
                      m_ownerName, requested, m_runtimeCapacity, Capacity, m_count, m_highWater, ceiling );
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
};
} // namespace Physics
} // namespace SkullbonezCore
