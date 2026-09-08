#ifndef SKULLBONEZ_LINE_COVERAGE
#define SKULLBONEZ_LINE_COVERAGE
#include "shader_behavior.hlsli"

struct LineQuad
{
    float4 position;
    float2 coordinate;
    float segmentLength;
};

LineQuad ExpandLineQuad(float4 a, float4 b, float2 corner, float2 viewport, float radius)
{
    LineQuad output;
    output.position = float4(0, 0, -1, 1);
    output.coordinate = 0;
    output.segmentLength = 0;
    // Near-plane clipping precedes perspective division and expansion. Fully
    // hidden and degenerate segments produce no triangles in the visible volume.
    if (!ClipSegmentToNearPlane(a, b)) return output;
    float2 scale = max(viewport, float2(1, 1)) * float2(0.5, -0.5);
    float2 delta = (b.xy / b.w - a.xy / a.w) * scale;
    float segmentLength = length(delta);
    if (segmentLength < 0.000001) return output;
    float2 direction = delta / segmentLength;
    float extent = radius + 0.5;
    float2 expansion = direction * (corner.x * 2 - 1) * extent
                     + float2(-direction.y, direction.x) * corner.y * extent;
    output.position = lerp(a, b, corner.x);
    output.position.xy += expansion / scale * output.position.w;
    output.coordinate = float2(corner.x * segmentLength + (corner.x * 2 - 1) * extent, corner.y * extent);
    output.segmentLength = segmentLength;
    return output;
}

float LineCoverage(float2 coordinate, float segmentLength, float radius)
{
    float2 distance = float2(coordinate.x - clamp(coordinate.x, 0, segmentLength), coordinate.y);
    return saturate(radius + 0.5 - length(distance));
}
#endif
