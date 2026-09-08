#pragma once

#include <array>
#include <cstring>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

namespace SkullbonezCore::Runtime
{
class ComparisonDiagnosticLines
{
    // Invariant: a bounded block reader preserves every byte and final line,
    // rejecting rows over 64 KiB before growing their string. Bulk reads avoid
    // the per-character CRT work in std::getline on gigabyte diagnostic files.
    std::ifstream m_input;
    std::array<char, 65536> m_buffer {};
    std::size_t m_cursor = 0, m_end = 0;
    uint64_t m_position = 0, m_size = 0;
    bool m_valid = true;

  public:
    explicit ComparisonDiagnosticLines( const std::filesystem::path& path ) : m_input( path, std::ios::binary )
    {
        if ( m_input )
        {
            m_input.seekg( 0, std::ios::end );
            const auto size = m_input.tellg();
            m_valid = size >= 0;
            m_size = m_valid ? static_cast<uint64_t>( size ) : 0;
            m_input.seekg( 0 );
        }
    }
    bool Open() const
    {
        return m_input.is_open();
    }
    bool Good() const
    {
        return m_valid && !m_input.bad();
    }
    uint64_t Position() const
    {
        return m_position;
    }
    uint64_t Size() const
    {
        return m_size;
    }
    bool Rewind()
    {
        m_input.clear();
        m_input.seekg( 0 );
        m_cursor = m_end = 0;
        m_position = 0;
        return static_cast<bool>( m_input ) && m_valid;
    }
    bool Read( std::string& line )
    {
        line.clear();
        while ( m_valid )
        {
            if ( m_cursor == m_end )
            {
                m_input.read( m_buffer.data(), m_buffer.size() );
                m_end = static_cast<std::size_t>( m_input.gcount() );
                m_cursor = 0;
                if ( !m_end )
                {
                    return !line.empty() && Good();
                }
            }
            const char* begin = m_buffer.data() + m_cursor;
            const auto available = m_end - m_cursor;
            const auto* newline = static_cast<const char*>( std::memchr( begin, '\n', available ) );
            const auto count = newline ? static_cast<std::size_t>( newline - begin ) : available;
            if ( line.size() + count > 65536 )
            {
                m_valid = false;
                return false;
            }
            line.append( begin, count );
            const auto consumed = count + ( newline ? 1 : 0 );
            m_cursor += consumed;
            m_position += consumed;
            if ( newline )
            {
                return true;
            }
        }
        return false;
    }
};
} // namespace SkullbonezCore::Runtime
