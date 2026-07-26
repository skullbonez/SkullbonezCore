/*
File: SkullbonezSource/Runtime/Prediction/ReplayPredictionRetainedGeometry.h
Purpose:
  Owns Prediction's compact retained-ribbon record and logical storage policy.

Summary:
  Prediction packs feature-specific path styling and adjacency into fixed
  logical ranges. Rendering receives only generic capacity, stream, range, and
  byte-layout values; it never interprets this record vocabulary.

Glossary:
  Record: Nineteen-float ribbon instance consumed by the retained-ribbon shader.
  Range: Stable fixed-capacity record slice with independent upload history.
  Continuation: A later range whose first record repairs the predecessor's open
    adjacency tail without moving either physical slice.

Invariants:
  - Prediction owns all record component meanings and logical capacities.
  - Range slices never overlap and never grow after creation.
  - Adjacent repair mutates only the two records that share a continuous join.
  - CPU storage is allocated during owner construction, before steady gameplay.

Related:
  - SkullbonezSource/Runtime/Prediction/ReplayPredictionDrawing.cpp
  - SkullbonezSource/Rendering/RenderCommandTypes.h
  - SkullbonezSource/Runtime/Replay/ReplayVisualPacket.h
*/
#pragma once

#include "../../Core/MainMemoryStats.h"
#include "../../Maths/Vector3.h"
#include "../../Rendering/RenderCommandTypes.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace SkullbonezCore::Core
{
struct ReplayTrajectoryAppearanceConfig;
}

namespace SkullbonezCore::Runtime
{
struct ReplayVisualPacket;
}

namespace SkullbonezCore::Runtime::ReplayOverlay
{
inline constexpr std::size_t PREDICTION_TRAJECTORY_FLOATS_PER_RECORD = 19u;
inline constexpr std::size_t PREDICTION_TRAJECTORY_ORDINARY_RECORD_CAPACITY = 24000u;
inline constexpr std::size_t PREDICTION_TRAJECTORY_PRIORITY_RECORD_CAPACITY = 3000u;
inline constexpr std::size_t PREDICTION_TRAJECTORY_RECORD_FLOAT_CAPACITY =
    ( PREDICTION_TRAJECTORY_ORDINARY_RECORD_CAPACITY + PREDICTION_TRAJECTORY_PRIORITY_RECORD_CAPACITY ) *
    PREDICTION_TRAJECTORY_FLOATS_PER_RECORD;
inline constexpr std::size_t PREDICTION_TRAJECTORY_RANGE_CAPACITY = 4096u;
inline constexpr std::size_t PREDICTION_TRAJECTORY_ORDINARY_LINE_FLOAT_CAPACITY = 262144u;
inline constexpr std::size_t PREDICTION_TRAJECTORY_PRIORITY_LINE_FLOAT_CAPACITY = 524288u;
// Prediction retains the authored asset identity while Rendering receives it
// only as a cold source value paired with the generic retained-ribbon contract.
inline constexpr const char* PREDICTION_RETAINED_RIBBON_SHADER_BASE_NAME = "shaders/trajectory_ribbon";

constexpr Rendering::RetainedGeometryCapacity PredictionRetainedGeometryCapacity() noexcept
{
    return { static_cast<uint32_t>( PREDICTION_TRAJECTORY_FLOATS_PER_RECORD ),
             static_cast<uint32_t>( PREDICTION_TRAJECTORY_ORDINARY_RECORD_CAPACITY ),
             static_cast<uint32_t>( PREDICTION_TRAJECTORY_PRIORITY_RECORD_CAPACITY ),
             static_cast<uint32_t>( PREDICTION_TRAJECTORY_ORDINARY_LINE_FLOAT_CAPACITY ),
             static_cast<uint32_t>( PREDICTION_TRAJECTORY_PRIORITY_LINE_FLOAT_CAPACITY ),
             static_cast<uint32_t>( PREDICTION_TRAJECTORY_RANGE_CAPACITY ) };
}

struct ReplayPredictionRetainedRecord
{
    Math::Vector::Vector3 start;
    Math::Vector::Vector3 end;
    float width = 0.0f;
    float colorR = 0.0f;
    float colorG = 0.0f;
    float colorB = 0.0f;
    float alpha = 0.0f;
    float edgeFeather = 0.0f;
    float emphasis = 0.0f;
    Math::Vector::Vector3 previousStart;
    Math::Vector::Vector3 nextEnd;

    std::array<float, PREDICTION_TRAJECTORY_FLOATS_PER_RECORD> Packed() const noexcept
    {
        return { start.x,
                 start.y,
                 start.z,
                 end.x,
                 end.y,
                 end.z,
                 width,
                 colorR,
                 colorG,
                 colorB,
                 alpha,
                 edgeFeather,
                 emphasis,
                 previousStart.x,
                 previousStart.y,
                 previousStart.z,
                 nextEnd.x,
                 nextEnd.y,
                 nextEnd.z };
    }
};

inline bool AppendPredictionRetainedRecord( std::span<float> arena,
                                            Rendering::RetainedGeometryRangeToken& range,
                                            const ReplayPredictionRetainedRecord& incoming,
                                            float continuityToleranceSquared ) noexcept
{
    if ( range.recordCount >= range.recordCapacity )
    {
        return false;
    }
    const std::size_t recordIndex = static_cast<std::size_t>( range.firstRecord ) + range.recordCount;
    const std::size_t firstFloat = recordIndex * PREDICTION_TRAJECTORY_FLOATS_PER_RECORD;
    if ( firstFloat + PREDICTION_TRAJECTORY_FLOATS_PER_RECORD > arena.size() )
    {
        return false;
    }

    std::array<float, PREDICTION_TRAJECTORY_FLOATS_PER_RECORD> record = incoming.Packed();
    if ( range.recordCount > 0u )
    {
        float* previous = arena.data() + firstFloat - PREDICTION_TRAJECTORY_FLOATS_PER_RECORD;
        const float dx = previous[3] - record[0];
        const float dy = previous[4] - record[1];
        const float dz = previous[5] - record[2];
        const bool samePresentation = previous[6] == record[6] && previous[10] == record[10] &&
                                      previous[11] == record[11] && previous[12] == record[12];
        if ( samePresentation && dx * dx + dy * dy + dz * dz <= continuityToleranceSquared )
        {
            // Invariant: adjacent records repair both ends of the shared join,
            // leaving unrelated fixed slices untouched.
            record[13] = previous[0];
            record[14] = previous[1];
            record[15] = previous[2];
            previous[16] = record[3];
            previous[17] = record[4];
            previous[18] = record[5];
        }
    }

    float* destination = arena.data() + firstFloat;
    for ( std::size_t component = 0; component < PREDICTION_TRAJECTORY_FLOATS_PER_RECORD; ++component )
    {
        destination[component] = record[component];
    }
    ++range.recordCount;
    return true;
}

inline bool AppendPredictionRetainedContinuation( std::span<float> arena,
                                                  Rendering::RetainedGeometryRangeToken& previousRange,
                                                  Rendering::RetainedGeometryRangeToken& range,
                                                  const ReplayPredictionRetainedRecord& incoming,
                                                  float continuityToleranceSquared ) noexcept
{
    if ( range.recordCount != 0u )
    {
        return false;
    }
    if ( !AppendPredictionRetainedRecord( arena, range, incoming, continuityToleranceSquared ) )
    {
        return false;
    }
    if ( previousRange.recordCount == 0u )
    {
        return true;
    }

    const std::size_t previousRecord = static_cast<std::size_t>( previousRange.firstRecord ) +
                                       previousRange.recordCount - 1u;
    const std::size_t currentRecord = static_cast<std::size_t>( range.firstRecord );
    const std::size_t previousFloat = previousRecord * PREDICTION_TRAJECTORY_FLOATS_PER_RECORD;
    const std::size_t currentFloat = currentRecord * PREDICTION_TRAJECTORY_FLOATS_PER_RECORD;
    if ( previousFloat + PREDICTION_TRAJECTORY_FLOATS_PER_RECORD > arena.size() ||
         currentFloat + PREDICTION_TRAJECTORY_FLOATS_PER_RECORD > arena.size() )
    {
        return true;
    }

    float* previous = arena.data() + previousFloat;
    float* current = arena.data() + currentFloat;
    const float dx = previous[3] - current[0];
    const float dy = previous[4] - current[1];
    const float dz = previous[5] - current[2];
    const bool samePresentation = previous[6] == current[6] && previous[10] == current[10] &&
                                  previous[11] == current[11] && previous[12] == current[12];
    if ( samePresentation && dx * dx + dy * dy + dz * dz <= continuityToleranceSquared )
    {
        current[13] = previous[0];
        current[14] = previous[1];
        current[15] = previous[2];
        previous[16] = current[3];
        previous[17] = current[4];
        previous[18] = current[5];
        ++previousRange.sourceVersion;
    }
    return true;
}

// Prediction owns the compact record vocabulary, adjacency repair, logical
// capacities, and stable range publication. Rendering sees only generic spans,
// layout values, and cache tokens after PublishToPacket returns.
class ReplayPredictionRetainedGeometry
{
  private:
    struct RibbonStyle
    {
        float width = 2.0f;
        float alpha = 1.0f;
        float edgeFeather = 1.0f;
        float emphasis = 0.0f;
    };

    RibbonStyle m_pathStyle = { 1.25f, 1.0f, 1.0f, 0.0f };
    RibbonStyle m_causalStyle = { 1.25f, 1.0f, 1.0f, 0.0f };
    RibbonStyle m_baselineStyle = { 1.0f, 1.0f, 1.0f, 0.0f };
    float m_selectedEmphasis = 0.45f;
    bool m_appearanceInitialized = false;
    // Invariant: the compact arena has one construction-time allocation and
    // no capacity-changing API. Keeping the fixed 513,000-float payload behind
    // a pointer also prevents the process-lifetime ReplayRuntime owner from
    // consuming the bounded launcher stack.
    std::unique_ptr<float[]> m_records;
    std::array<Rendering::RetainedGeometryRangeToken, PREDICTION_TRAJECTORY_RANGE_CAPACITY> m_ranges = {};
    std::array<Rendering::RetainedGeometryRangeToken, PREDICTION_TRAJECTORY_RANGE_CAPACITY> m_drawRanges = {};
    std::size_t m_rangeCount = 0;
    std::size_t m_ordinaryRecordCapacityUsed = 0;
    std::size_t m_priorityRecordCapacityUsed = 0;
    std::size_t m_ordinaryRecordCount = 0;
    std::size_t m_priorityRecordCount = 0;
    SkullbonezCore::Core::MainMemoryReplayTrajectoryStats m_stats;
    uint64_t m_revision = 0;

    void RecordDropped( SkullbonezCore::Core::MainMemoryReplayTrajectoryLane lane );
    bool EmitRecord( std::size_t rangeIndex,
                     const Math::Vector::Vector3& start,
                     const Math::Vector::Vector3& end,
                     float r,
                     float g,
                     float b,
                     const RibbonStyle& style,
                     SkullbonezCore::Core::MainMemoryReplayTrajectoryLane lane );

  public:
    ReplayPredictionRetainedGeometry();

    bool SetAppearance( const Core::ReplayTrajectoryAppearanceConfig& appearance );
    void Clear() noexcept;
    void PublishToPacket( ReplayVisualPacket& packet );
    uint64_t Revision() const noexcept
    {
        return m_revision;
    }

    std::size_t BeginRange( uint64_t identity,
                            uint32_t sourceVersion,
                            bool priority,
                            std::size_t recordCapacity,
                            uint64_t drawOrder,
                            std::size_t continuationRange = PREDICTION_TRAJECTORY_RANGE_CAPACITY );
    std::size_t RangeCapacityRemaining( std::size_t rangeIndex ) const noexcept;
    std::size_t OrdinaryCapacityRemaining() const noexcept;
    std::size_t PriorityCapacityRemaining() const noexcept;
    std::size_t OrdinaryCountRemaining() const noexcept;
    std::size_t PriorityCountRemaining() const noexcept;
    void RecordDroppedSegment( SkullbonezCore::Core::MainMemoryReplayTrajectoryLane lane )
    {
        RecordDropped( lane );
    }
    void AddPathSegment( std::size_t rangeIndex,
                         const Math::Vector::Vector3& start,
                         const Math::Vector::Vector3& end,
                         float r,
                         float g,
                         float b,
                         SkullbonezCore::Core::MainMemoryReplayTrajectoryLane lane,
                         float emphasis = 0.0f );
    void AddCausalTrailSegment( std::size_t rangeIndex,
                                const Math::Vector::Vector3& start,
                                const Math::Vector::Vector3& end,
                                float r,
                                float g,
                                float b );
    void AddBaselinePathSegment( std::size_t rangeIndex,
                                 const Math::Vector::Vector3& start,
                                 const Math::Vector::Vector3& end,
                                 float r,
                                 float g,
                                 float b,
                                 float opacity = 1.0f );
};
} // namespace SkullbonezCore::Runtime::ReplayOverlay
