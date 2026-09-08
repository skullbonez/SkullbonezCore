#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace SkullbonezCore::Runtime
{
struct ComparisonObservationIndex
{
    int32_t frame = 0;
    uint32_t flags = 0, contacts = 0, iterations = 0;
    uint64_t Words() const
    {
        return uint64_t { contacts } * 13 + uint64_t { iterations } * 5;
    }
};

// Cold-load reader for SKOBS1. The checked directory supplies exact allocation
// counts; each frame is then read in one block without reparsing diagnostic text.
class ComparisonObservationBinary
{
  public:
    explicit ComparisonObservationBinary( const std::filesystem::path& path );
    bool ReadIndex();
    bool ReadFrame( const ComparisonObservationIndex& entry, std::vector<uint32_t>& words );
    const std::vector<ComparisonObservationIndex>& Index() const
    {
        return m_index;
    }
    const std::string& SourceHash() const
    {
        return m_sourceHash;
    }
    uint64_t Contacts() const
    {
        return m_contacts;
    }
    uint64_t Iterations() const
    {
        return m_iterations;
    }
    uint64_t ScratchBytes() const
    {
        return m_maxWords * 4 + m_index.capacity() * sizeof( ComparisonObservationIndex );
    }

  private:
    std::ifstream m_input;
    std::vector<ComparisonObservationIndex> m_index;
    std::string m_sourceHash;
    uint64_t m_size = 0, m_contacts = 0, m_iterations = 0, m_maxWords = 0;
};
} // namespace SkullbonezCore::Runtime
