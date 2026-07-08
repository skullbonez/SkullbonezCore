/*
File: DisjointSet.h
Purpose:
  Provides a small union-find helper for physics island construction over
  caller-owned scratch buffers.

Mental model:
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
  - The helper borrows scratch buffers from PhysicsWorld; it owns no storage and
    depends on those buffers being pre-reserved before fixed-step gameplay.

Related:
  - PhysicsWorld.cpp owns the deterministic solver stages that use this helper.
  - engine-cleanup-plans/02-physicsworld-solver-decomposition.md
*/
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace SkullbonezCore::Physics
{

class DisjointSet
{
public:
    DisjointSet( std::vector<int>& parent, std::vector<uint8_t>& rank, int count )
        : m_parent( parent ), m_rank( rank ), m_count( count )
    {
    }

    // Invariant: PhysicsWorld reserves these buffers up front. Reset() may
    // resize within that capacity, but the fixed-step path must not grow them.
    void Reset()
    {
        const std::size_t rowCount = static_cast<std::size_t>( m_count );
        m_parent.assign( rowCount, 0 );
        m_rank.assign( rowCount, 0 );
        for ( int row = 0; row < m_count; ++row )
        {
            m_parent[static_cast<std::size_t>( row )] = row;
        }
    }

    int Find( int row )
    {
        int root = row;
        while ( m_parent[static_cast<std::size_t>( root )] != root )
        {
            root = m_parent[static_cast<std::size_t>( root )];
        }

        while ( m_parent[static_cast<std::size_t>( row )] != row )
        {
            const int parent = m_parent[static_cast<std::size_t>( row )];
            m_parent[static_cast<std::size_t>( row )] = root;
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

        if ( m_rank[static_cast<std::size_t>( rootA )] <
             m_rank[static_cast<std::size_t>( rootB )] )
        {
            std::swap( rootA, rootB );
        }

        m_parent[static_cast<std::size_t>( rootB )] = rootA;
        if ( m_rank[static_cast<std::size_t>( rootA )] ==
             m_rank[static_cast<std::size_t>( rootB )] )
        {
            ++m_rank[static_cast<std::size_t>( rootA )];
        }
    }

private:
    std::vector<int>& m_parent;
    std::vector<uint8_t>& m_rank;
    int m_count;
};

} // namespace SkullbonezCore::Physics
