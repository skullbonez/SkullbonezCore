/*
File: DisjointSet.h
Purpose:
  Provides a small union-find helper for physics island construction over
  caller-owned scratch buffers.

Summary:
  A disjoint set tracks which rows belong to the same connected component. The
  physics solver uses that component identity for sleep and narrowphase islands
  without allocating another owner-side container.

Glossary:
  Disjoint set: Data structure that groups rows into components through find and
    unite operations.
  Root: Representative row for one connected component.
  Rank: Conservative tree-depth estimate used to keep component trees shallow.
  Path compression: Find-time rewrite that points visited rows directly at the
    root, reducing later lookup cost.

Invariants:
  - Equal-rank unions keep the first argument's root as the parent. This
    preserves the existing physics island merge tie-break and its byte-exact
    validation behavior.
  - The helper borrows scratch buffers from its stage owner; it owns no storage
    and depends on those buffers being committed before fixed-step gameplay.
  - Reset proves non-negative count and both borrowed capacities before its
    first fill, then callers must not resize either buffer while the helper lives.

Related:
  - PhysicsWorld.cpp owns the deterministic solver stages that use this helper.
*/
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>

#include "../Core/FatalError.h"

namespace SkullbonezCore::Physics
{

class DisjointSet
{
  public:
    DisjointSet( std::span<int> parent, std::span<uint8_t> rank, int count )
        : m_parent( parent ), m_rank( rank ), m_parentRows( parent.data() ), m_rankRows( rank.data() ), m_count( count )
    {
    }

    // Invariant: Reset proves both borrowed ranges before its first write. It
    // initializes rows in place and cannot resize or replace owner storage.
    void Reset()
    {
        if ( m_count < 0 || static_cast<std::size_t>( m_count ) > m_parent.size() ||
             static_cast<std::size_t>( m_count ) > m_rank.size() )
        {
            SB_FATAL( "Physics/DisjointSet",
                      "Borrowed scratch capacity is too small. count=%d parent_rows=%zu rank_rows=%zu.", m_count,
                      m_parent.size(), m_rank.size() );
        }

        const std::size_t rowCount = static_cast<std::size_t>( m_count );
        std::fill_n( m_parent.begin(), rowCount, 0 );
        std::fill_n( m_rank.begin(), rowCount, uint8_t { 0 } );

        for ( int row = 0; row < m_count; ++row )
        {
            m_parent[static_cast<std::size_t>( row )] = row;
        }

        RebindRows();
    }

    int Find( int row )
    {
        int root = row;

        while ( m_parentRows[root] != root )
        {
            root = m_parentRows[root];
        }

        while ( m_parentRows[row] != row )
        {
            const int parent = m_parentRows[row];
            m_parentRows[row] = root;
            row = parent;
        }

        return root;
    }

    void Unite( int a, int b )
    {
        int rootA = Find( a );
        int rootB = Find( b );

        if ( rootA == rootB )
        {
            return;
        }

        if ( m_rankRows[rootA] < m_rankRows[rootB] )
        {
            std::swap( rootA, rootB );
        }

        m_parentRows[rootB] = rootA;

        if ( m_rankRows[rootA] == m_rankRows[rootB] )
        {
            ++m_rankRows[rootA];
        }
    }

  private:
    void RebindRows()
    {
        m_parentRows = m_parent.data();
        m_rankRows = m_rank.data();
    }

    std::span<int> m_parent;
    std::span<uint8_t> m_rank;
    int* m_parentRows;
    uint8_t* m_rankRows;
    int m_count;
};

} // namespace SkullbonezCore::Physics
