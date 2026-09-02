/*
File: SkullbonezSource/Runtime/Tools/EditorTracer.h
Purpose:
  Declares the generic transient line and replay-ribbon builder owned by Tools.

Summary:
  EditorTracer accepts detached geometry values and produces the frame-local
  line/ribbon packet submitted by Render. Feature owners decide which geometry
  exists; the tracer owns only bounded packing, cached ribbon expansion, and
  submission statistics.

Invariants:
  - Feature owners pass explicit poses, shapes, and colors; the tracer never
    reaches into editor, scene, replay, or prediction state.
  - Returned ReplayVisualPacket spans borrow tracer storage until Clear().
  - Frame construction never grows the pre-reserved buffers.

Related:
  - SkullbonezSource/Runtime/Tools/EditorTracer.cpp
  - SkullbonezSource/Runtime/Tools/RuntimeTools.h
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "../../Core/MainMemoryStats.h"
#include "../Replay/ReplayVisualPacket.h"
#include "../../Maths/Matrix4.h"
#include "../../Maths/Quaternion.h"
#include "../../Maths/Vector3.h"
#include "../../Physics/CollisionShape.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace SkullbonezCore::Core
{
class SbDiagnosticStore;
struct ReplayTrajectoryAppearanceConfig;
} // namespace SkullbonezCore::Core

namespace SkullbonezCore::Runtime
{
class EditorTracer
{
  private:
    struct ReplayRibbonStyle
    {
        float width = 2.0f;
        float alpha = 1.0f;
        float edgeFeather = 1.0f;
        float emphasis = 0.0f;
    };

    ReplayRibbonStyle m_replayPathStyle = { 1.25f, 1.0f, 1.0f, 0.0f };
    ReplayRibbonStyle m_replayCausalStyle = { 1.25f, 1.0f, 1.0f, 0.0f };
    ReplayRibbonStyle m_replayBaselineStyle = { 1.0f, 1.0f, 1.0f, 0.0f };
    ReplayRibbonStyle m_replayMarkerStyle = { 1.5f, 1.0f, 1.0f, 0.0f };
    float m_replaySelectedEmphasis = 0.45f;
    bool m_replayTrajectoryAppearanceInitialized = false;

    SkullbonezCore::Core::SbDiagnosticStore& m_resultDiagnostics;
    std::vector<float> m_lineData;
    std::vector<float> m_priorityLineData;
    std::vector<float> m_renderLineData;
    std::vector<float> m_replayRibbonSegments;
    std::vector<float> m_priorityReplayRibbonSegments;
    std::vector<float> m_cachedReplayRibbonSegments;
    std::vector<float> m_cachedPriorityReplayRibbonSegments;
    std::vector<float> m_replayRibbonVertexData;
    std::vector<float> m_priorityReplayRibbonVertexData;
    std::size_t m_expandedOrdinarySegmentCount = 0;
    std::size_t m_expandedPrioritySegmentCount = 0;
    bool m_replayRibbonCacheValid = false;
    SkullbonezCore::Core::MainMemoryReplayTrajectorySubmissionStats m_replaySubmissionStats;
    SkullbonezCore::Core::MainMemoryReplayTrajectorySubmissionStats m_cachedRibbonSubmissionStats;
    uint64_t m_replayGeometryRevision = 0;

    void EmitLineTo( std::vector<float>& lineData, const Math::Vector::Vector3& a, const Math::Vector::Vector3& b, float r,
                     float g, float bl );
    void EmitLine( const Math::Vector::Vector3& a, const Math::Vector::Vector3& b, float r, float g, float bl );
    void EmitArrow( const Math::Vector::Vector3& a, const Math::Vector::Vector3& b, float r, float g, float bl );
    void EmitRing( const Math::Vector::Vector3& center, int axis, float radius, float r, float g, float bl );
    void EmitSphereTo( std::vector<float>& lineData, const Math::Vector::Vector3& center, float radius, float r, float g,
                       float bl );
    void EmitSphere( const Math::Vector::Vector3& center, float radius, float r, float g, float bl );
    void EmitBoxTo( std::vector<float>& lineData, const Math::Vector::Vector3& center, const Math::Vector::Vector3& xAxis,
                    const Math::Vector::Vector3& yAxis, const Math::Vector::Vector3& zAxis, float r, float g, float bl );
    void EmitBox( const Math::Vector::Vector3& center, const Math::Vector::Vector3& xAxis,
                  const Math::Vector::Vector3& yAxis, const Math::Vector::Vector3& zAxis, float r, float g, float bl );
    bool CanEmitShapeOutlineTo( const std::vector<float>& lineData,
                                const Math::CollisionDetection::CollisionShapeReference& shape ) const noexcept;
    void EmitShapeOutlineTo( std::vector<float>& lineData, const Math::Vector::Vector3& position,
                             const Math::Orientation::Quaternion& orientation,
                             const Math::CollisionDetection::CollisionShapeReference& shape, float r, float g, float b );
    void EmitShapeOutline( const Math::Vector::Vector3& position, const Math::Orientation::Quaternion& orientation,
                           const Math::CollisionDetection::CollisionShapeReference& shape, float r, float g, float b );
    void EmitReplayRibbonSegmentTo( std::vector<float>& ribbonData, const Math::Vector::Vector3& a,
                                    const Math::Vector::Vector3& b, float r, float g, float bl,
                                    const ReplayRibbonStyle& style,
                                    SkullbonezCore::Core::MainMemoryReplayTrajectoryLane lane );
    void EmitReplayRibbonGlowPairTo( std::vector<float>& ribbonData, const Math::Vector::Vector3& a,
                                     const Math::Vector::Vector3& b, float r, float g, float bl,
                                     const ReplayRibbonStyle& glow, const ReplayRibbonStyle& core,
                                     SkullbonezCore::Core::MainMemoryReplayTrajectoryLane lane );
    void BuildReplayRibbonVertices( const Math::Vector::Vector3& cameraEye, const Math::Vector::Vector3& cameraUp );
    SkullbonezCore::Core::MainMemoryReplayTrajectoryStats m_replayTrajectoryStats;

  public:
    explicit EditorTracer( SkullbonezCore::Core::SbDiagnosticStore& resultDiagnostics );

    bool SetReplayTrajectoryAppearance( const SkullbonezCore::Core::ReplayTrajectoryAppearanceConfig& appearance );
    void Clear();
    void ClearReplayTrajectoryStats();
    const SkullbonezCore::Core::MainMemoryReplayTrajectoryStats& ReplayTrajectoryStats() const;
    uint64_t ReplayGeometryRevision() const noexcept
    {
        return m_replayGeometryRevision;
    }

    void RecordReplayRibbonDroppedSegments( SkullbonezCore::Core::MainMemoryReplayTrajectoryLane lane,
                                            std::size_t count = 1u );
    ReplayVisualPacket BuildReplayVisualPacket( const Math::Vector::Vector3& cameraEye,
                                                const Math::Vector::Vector3& cameraUp );
    std::size_t ReplayPathRibbonSegmentCapacityRemaining() const;
    std::size_t ReplayPriorityRibbonSegmentCapacityRemaining() const;

    // Detached primitive inputs let App compose feature-owned overlay policy
    // without giving Tools access to the feature owner.
    void AddLine( const Math::Vector::Vector3& start, const Math::Vector::Vector3& end, float r, float g, float b );
    void AddBoxOutline( const Math::Vector::Vector3& center, const Math::Vector::Vector3& xAxis,
                        const Math::Vector::Vector3& yAxis, const Math::Vector::Vector3& zAxis, float r, float g, float b );
    void AddSphereOutline( const Math::Vector::Vector3& center, float radius, float r, float g, float b );
    void AddRagdollOutline( const Math::Vector::Vector3& center, float scale,
                            const Math::Orientation::Quaternion& orientation, float r, float g, float b );

    void AddPlacementRay( const Math::Vector::Vector3& rayOrigin, const Math::Vector::Vector3& hitPoint );
    void AddRayCastTestLine( const Math::Vector::Vector3& start, const Math::Vector::Vector3& end, float alpha, bool hit );
    void AddReplayPathSegment( const Math::Vector::Vector3& start, const Math::Vector::Vector3& end, float r, float g,
                               float b,
                               SkullbonezCore::Core::MainMemoryReplayTrajectoryLane lane = SkullbonezCore::Core::MainMemoryReplayTrajectoryLane::FutureRoot,
                               float emphasis = 0.0f );
    void AddReplayCausalTrailSegment( const Math::Vector::Vector3& start, const Math::Vector::Vector3& end, float r, float g,
                                      float b );
    void AddReplayBaselinePathSegment( const Math::Vector::Vector3& start, const Math::Vector::Vector3& end, float r,
                                       float g, float b, float opacity = 1.0f );
    void AddReplayContactMarker( const Math::Vector::Vector3& point, const Math::Vector::Vector3& normal, float r, float g,
                                 float b );
    void AddReplayImpulseVector( const Math::Vector::Vector3& point, const Math::Vector::Vector3& impulse, float r, float g,
                                 float b );
    bool AddReplayCausalEntryMarker( const Math::Vector::Vector3& position,
                                     const Math::Orientation::Quaternion& orientation,
                                     const Math::CollisionDetection::CollisionShapeReference& shape );
    bool AddReplayCausalRestMarker( const Math::Vector::Vector3& position,
                                    const Math::Orientation::Quaternion& orientation,
                                    const Math::CollisionDetection::CollisionShapeReference& shape );
    bool AddReplayCausalHorizonMarker( const Math::Vector::Vector3& position,
                                       const Math::Orientation::Quaternion& orientation,
                                       const Math::CollisionDetection::CollisionShapeReference& shape );
    void AddReplayBaselineEntryMarker( const Math::Vector::Vector3& position,
                                       const Math::Orientation::Quaternion& orientation,
                                       const Math::CollisionDetection::CollisionShapeReference& shape );
    void AddReplayBaselineRestMarker( const Math::Vector::Vector3& position,
                                      const Math::Orientation::Quaternion& orientation,
                                      const Math::CollisionDetection::CollisionShapeReference& shape );
    void AddReplayTargetMarker( const Math::Vector::Vector3& position, const Math::Orientation::Quaternion& orientation,
                                const Math::CollisionDetection::CollisionShapeReference& shape, float radius );
    void AddAttachedCameraTargetMarker( const Math::Vector::Vector3& position,
                                        const Math::Orientation::Quaternion& orientation,
                                        const Math::CollisionDetection::CollisionShapeReference& shape, float radius,
                                        bool activeFollow );
    void AddSelectionOutline( const Math::Vector::Vector3& position, const Math::Orientation::Quaternion& orientation,
                              const Math::CollisionDetection::CollisionShapeReference& shape );
    void AddGizmo( const Math::Vector::Vector3& origin, float radius, int hotTranslateAxis, int hotRotationAxis,
                   int activeAxis, bool activeRotation, bool scaleMode, bool activeScale );
    void AddReplayVelocityGizmo( const Math::Vector::Vector3& origin, const Math::Orientation::Quaternion& orientation,
                                 const Math::CollisionDetection::CollisionShapeReference& shape, float radius,
                                 const Math::Vector::Vector3& linearVelocity, const Math::Vector::Vector3& angularVelocity,
                                 int hotLinearAxis, int hotAngularAxis, int activeAxis, bool activeAngular );
};
} // namespace SkullbonezCore::Runtime
