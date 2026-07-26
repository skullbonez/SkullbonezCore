/*
File: SkullbonezSource/Physics/PhysicsFixedList.h
Purpose:
  Defines fixed-capacity list storage for physics runtime state.

Summary:
  Physics hot-path owners need vector-like dense rows without owning dynamic
  standard-library capacity. This type reserves raw fixed storage inside the
  owner, constructs only the live prefix, and treats growth past the compile-time
  cap as a policy failure.

Glossary:
  Fixed-capacity list: Contiguous storage with a runtime size and a compile-time
    maximum, but no heap-backed reserve or reallocation path.
  Live count: Number of initialized entries currently visible to callers.
  Capacity cap: Compile-time maximum entry count that replaces vector capacity.

Invariants:
  - No method allocates or changes backing storage.
  - Only indices below the live count hold constructed T instances.
  - Overflow is fatal to the caller via assertion/abort, not a growth path.
  - Iteration exposes only the live prefix of the backing array.

Related:
  - SkullbonezSource/Physics/PhysicsBodyStore.h
  - SkullbonezSource/Physics/ColliderStore.h
  - AGENTS.md (Runtime Static Allocation Policy)
*/
#pragma once

#include <cassert>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
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

    class iterator
    {
      public:
        iterator( PhysicsFixedList* owner, std::size_t index ) : m_owner( owner ), m_index( index )
        {
        }
        T& operator*() const
        {
            return ( *m_owner )[m_index];
        }
        iterator& operator++()
        {
            ++m_index;
            return *this;
        }
        bool operator!=( const iterator& other ) const
        {
            return m_owner != other.m_owner || m_index != other.m_index;
        }

      private:
        PhysicsFixedList* m_owner = nullptr;
        std::size_t m_index = 0u;
    };

    class const_iterator
    {
      public:
        const_iterator( const PhysicsFixedList* owner, std::size_t index ) : m_owner( owner ), m_index( index )
        {
        }
        const T& operator*() const
        {
            return ( *m_owner )[m_index];
        }
        const_iterator& operator++()
        {
            ++m_index;
            return *this;
        }
        bool operator!=( const const_iterator& other ) const
        {
            return m_owner != other.m_owner || m_index != other.m_index;
        }

      private:
        const PhysicsFixedList* m_owner = nullptr;
        std::size_t m_index = 0u;
    };

    explicit PhysicsFixedList( const char* ownerName = "PhysicsFixedList" )
        : m_ownerName( ownerName ? ownerName : "PhysicsFixedList" )
    {
    }

    PhysicsFixedList( const PhysicsFixedList& other ) : m_ownerName( other.m_ownerName )
    {

        for ( const T& value : other )
        {
            push_back( value );
        }
    }

    PhysicsFixedList& operator=( const PhysicsFixedList& other )
    {

        if ( this == &other )
        {
            return *this;
        }

        clear();

        for ( const T& value : other )
        {
            push_back( value );
        }

        return *this;
    }

    PhysicsFixedList( PhysicsFixedList&& other ) noexcept( std::is_nothrow_move_constructible<T>::value )
        : m_ownerName( other.m_ownerName )
    {

        for ( T& value : other )
        {
            push_back( std::move( value ) );
        }

        other.clear();
    }

    PhysicsFixedList& operator=( PhysicsFixedList&& other ) noexcept( std::is_nothrow_move_constructible<T>::value )
    {

        if ( this == &other )
        {
            return *this;
        }

        clear();

        for ( T& value : other )
        {
            push_back( std::move( value ) );
        }

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

    constexpr std::size_t capacity() const
    {
        return Capacity;
    }

    T* data()
    {
        return m_count == 0u ? nullptr : ValueAt( 0u );
    }

    const T* data() const
    {
        return m_count == 0u ? nullptr : ValueAt( 0u );
    }

    iterator begin()
    {
        return iterator( this, 0u );
    }

    iterator end()
    {
        return iterator( this, m_count );
    }

    const_iterator begin() const
    {
        return const_iterator( this, 0u );
    }

    const_iterator end() const
    {
        return const_iterator( this, m_count );
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

    void reserve( std::size_t requested ) const
    {

        if ( requested > Capacity )
        {
            FailCapacityExceeded( requested );
        }
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
    using Storage = typename std::aligned_storage<sizeof( T ), alignof( T )>::type;
    static_assert( sizeof( Storage ) == sizeof( T ), "PhysicsFixedList storage must preserve T stride." );
    static_assert( alignof( Storage ) == alignof( T ), "PhysicsFixedList storage must preserve T alignment." );

    // Lifetime: placement construction uses the raw slot before T exists.
    // ValueAt is only for already-live entries, where std::launder is valid.
    // Why: aligned_storage has no T object until placement construction; these
    // private helpers are the one representation seam, and launder re-establishes
    // the typed pointer after each slot lifetime begins.
    void* RawSlot( std::size_t index )
    {
        return static_cast<void*>( &m_values[index] );
    }

    T* ValueAt( std::size_t index )
    {
        return std::launder( reinterpret_cast<T*>( &m_values[index] ) );
    }

    const T* ValueAt( std::size_t index ) const
    {
        return std::launder( reinterpret_cast<const T*>( &m_values[index] ) );
    }

    void CheckCapacity( std::size_t requested ) const
    {

        if ( requested > Capacity )
        {
            FailCapacityExceeded( requested );
        }
    }

    void TrackHighWater()
    {

        if ( m_count > m_highWater )
        {
            m_highWater = m_count;
        }
    }

    [[noreturn]] void FailCapacityExceeded( std::size_t requested ) const
    {
        std::fprintf( stderr,
                      "FATAL: PhysicsFixedList capacity exceeded owner=%s requested=%zu capacity=%zu count=%zu "
                      "high_water=%zu.\n",
                      m_ownerName, requested, Capacity, m_count, m_highWater );
        std::fprintf( stdout,
                      "FATAL: PhysicsFixedList capacity exceeded owner=%s requested=%zu capacity=%zu count=%zu "
                      "high_water=%zu.\n",
                      m_ownerName, requested, Capacity, m_count, m_highWater );
        std::fflush( stderr );
        std::fflush( stdout );
        assert( false && "PhysicsFixedList capacity exceeded" );
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

    Storage m_values[Capacity];
    std::size_t m_count = 0u;
    std::size_t m_highWater = 0u;
    const char* m_ownerName = "PhysicsFixedList";
};
} // namespace Physics
} // namespace SkullbonezCore
