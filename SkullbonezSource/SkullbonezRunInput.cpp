/*
File: SkullbonezSource/SkullbonezRunInput.cpp
Purpose:
  Translates input and UI commands into runtime state changes.

Mental model:
  Runtime code connects authored scene data, input, simulation, render
  backends, and validation-oriented launch modes. Follow who owns state and
  when that state changes.

Glossary:
  Validation gate: Repository script that proves a class of changes before
  commit or PR.

Related:
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "SkullbonezRunInternal.h"
#include "SkullbonezInputController.h"
#include "SkullbonezWorkerPool.h"
#include "UI/UILayout.h"

#include <cfloat>

using namespace SkullbonezCore::Basics;
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Orientation;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Physics;
using namespace SkullbonezCore::UI::Layout;
using namespace SkullbonezCore::Basics::RunInternal;

namespace
{
bool TransformClipPointToWorld( const Matrix4& inverseViewProjection, float x, float y, float z, Vector3& outWorld )
{
    const float worldX = inverseViewProjection.m[0] * x + inverseViewProjection.m[4] * y + inverseViewProjection.m[8] * z + inverseViewProjection.m[12];
    const float worldY = inverseViewProjection.m[1] * x + inverseViewProjection.m[5] * y + inverseViewProjection.m[9] * z + inverseViewProjection.m[13];
    const float worldZ = inverseViewProjection.m[2] * x + inverseViewProjection.m[6] * y + inverseViewProjection.m[10] * z + inverseViewProjection.m[14];
    const float worldW = inverseViewProjection.m[3] * x + inverseViewProjection.m[7] * y + inverseViewProjection.m[11] * z + inverseViewProjection.m[15];
    if ( fabsf( worldW ) < 1e-6f )
    {
        return false;
    }

    const float invW = 1.0f / worldW;
    outWorld = Vector3( worldX * invW, worldY * invW, worldZ * invW );
    return true;
}


Vector3 BoxInertiaForHalfExtents( const Vector3& halfExtents, float mass )
{
    const float hx2 = halfExtents.x * halfExtents.x;
    const float hy2 = halfExtents.y * halfExtents.y;
    const float hz2 = halfExtents.z * halfExtents.z;
    const float m3 = mass / 3.0f;
    return Vector3( m3 * ( hy2 + hz2 ), m3 * ( hx2 + hz2 ), m3 * ( hx2 + hy2 ) );
}


float HullBottomOffset( const ConvexHullShape& hull )
{
    float minY = FLT_MAX;
    for ( uint16_t i = 0; i < hull.GetVertexCount(); ++i )
    {
        minY = (std::min)( minY, hull.GetVertex( i ).y );
    }
    return minY == FLT_MAX ? 0.0f : -minY;
}


constexpr float EDITOR_PLACEMENT_SURFACE_EPSILON = 0.02f;
constexpr float EDITOR_PLACEMENT_SNAP = 2.0f;


const char* FixedHullPathForType( int fixedObjectType )
{
    switch ( fixedObjectType )
    {
    case SkullbonezCore::UI::EditorTab::FIXED_HULL_WEDGE:
        return "SkullbonezData/hulls/wedge.hull";
    case SkullbonezCore::UI::EditorTab::FIXED_HULL_TRI_PRISM:
        return "SkullbonezData/hulls/tri_prism.hull";
    case SkullbonezCore::UI::EditorTab::FIXED_HULL_TAPERED_BLOCK:
        return "SkullbonezData/hulls/tapered_block.hull";
    case SkullbonezCore::UI::EditorTab::FIXED_HULL_PYRAMID:
        return "SkullbonezData/hulls/pyramid.hull";
    case SkullbonezCore::UI::EditorTab::FIXED_HULL_HEX_PRISM:
        return "SkullbonezData/hulls/hex_prism.hull";
    case SkullbonezCore::UI::EditorTab::FIXED_HULL_DIAMOND:
        return "SkullbonezData/hulls/diamond.hull";
    default:
        return nullptr;
    }
}


const ConvexHullShape* CachedFixedHullForType( int fixedObjectType )
{
    const int type = std::clamp( fixedObjectType, 0, SkullbonezCore::UI::EditorTab::FIXED_TYPE_COUNT - 1 );
    const char* path = FixedHullPathForType( type );
    if ( !path )
    {
        return nullptr;
    }

    static std::array<ConvexHullShape, SkullbonezCore::UI::EditorTab::FIXED_TYPE_COUNT> hulls = {};
    static std::array<bool, SkullbonezCore::UI::EditorTab::FIXED_TYPE_COUNT> loaded = {};
    if ( !loaded[type] )
    {
        hulls[type] = ConvexHullShape::LoadFromFile( path );
        loaded[type] = true;
    }
    return &hulls[type];
}


Vector3 EditorAxisVector( int axis )
{
    switch ( axis )
    {
    case 0:
        return Vector3( 1.0f, 0.0f, 0.0f );
    case 1:
        return Vector3( 0.0f, 1.0f, 0.0f );
    case 2:
        return Vector3( 0.0f, 0.0f, 1.0f );
    default:
        return SkullbonezCore::Math::Vector::ZERO_VECTOR;
    }
}


float EditorModelRadius( const GameModel& model )
{
    return (std::max)( GetShapeBoundingRadius( model.GetCollisionShape() ), 1.0f );
}


float EditorGizmoAxisLength( float modelRadius )
{
    return (std::max)( 14.0f, modelRadius + 12.0f );
}


float DistanceRayToSegmentSquared( const Vector3& rayOrigin,
                                   const Vector3& rayDirection,
                                   const Vector3& segmentA,
                                   const Vector3& segmentB )
{
    const Vector3 segment = segmentB - segmentA;
    const float segmentLenSq = segment * segment;
    if ( segmentLenSq <= TOLERANCE * TOLERANCE )
    {
        const Vector3 toPoint = segmentA - rayOrigin;
        const float rayT = (std::max)( 0.0f, toPoint * rayDirection );
        return VectorMagSquared( rayOrigin + rayDirection * rayT - segmentA );
    }

    const Vector3 w0 = rayOrigin - segmentA;
    const float a = rayDirection * rayDirection;
    const float b = rayDirection * segment;
    const float c = segmentLenSq;
    const float d = rayDirection * w0;
    const float e = segment * w0;
    const float denom = a * c - b * b;

    float rayT = 0.0f;
    float segmentT = 0.0f;
    if ( fabsf( denom ) > 1e-5f )
    {
        rayT = ( b * e - c * d ) / denom;
        segmentT = ( a * e - b * d ) / denom;
    }

    if ( rayT < 0.0f )
    {
        rayT = 0.0f;
        segmentT = std::clamp( e / c, 0.0f, 1.0f );
    }
    else if ( segmentT < 0.0f )
    {
        segmentT = 0.0f;
        rayT = (std::max)( 0.0f, -d / a );
    }
    else if ( segmentT > 1.0f )
    {
        segmentT = 1.0f;
        rayT = (std::max)( 0.0f, ( b - d ) / a );
    }

    const Vector3 rayPoint = rayOrigin + rayDirection * rayT;
    const Vector3 segmentPoint = segmentA + segment * segmentT;
    return VectorMagSquared( rayPoint - segmentPoint );
}


uint64_t CinematicOverrideMaskForUIParam( UICinematicParam param )
{
    switch ( param )
    {
    case UICinematicParam::Exposure:
        return SCENE_CINE_EXPOSURE;
    case UICinematicParam::Gamma:
        return SCENE_CINE_GAMMA;
    case UICinematicParam::SkyMode:
    case UICinematicParam::TerrainMode:
    case UICinematicParam::ObjectStyle:
    case UICinematicParam::WaterMode:
        return SCENE_CINE_STYLE_MODES;
    case UICinematicParam::StyleSaturation:
    case UICinematicParam::StyleContrast:
    case UICinematicParam::StyleVignette:
        return SCENE_CINE_STYLE_GRADE;
    case UICinematicParam::SunX:
        return SCENE_CINE_SUN_SCREEN_X;
    case UICinematicParam::SunY:
        return SCENE_CINE_SUN_SCREEN_Y;
    case UICinematicParam::SunBrightness:
        return SCENE_CINE_SUN_INTENSITY;
    case UICinematicParam::SunRed:
        return SCENE_CINE_SUN_COLOR_R;
    case UICinematicParam::SunGreen:
        return SCENE_CINE_SUN_COLOR_G;
    case UICinematicParam::SunBlue:
        return SCENE_CINE_SUN_COLOR_B;
    case UICinematicParam::SkyGlow:
        return SCENE_CINE_SKY_GLOW_STRENGTH;
    case UICinematicParam::HorizonRed:
        return SCENE_CINE_SKY_HORIZON_R;
    case UICinematicParam::HorizonGreen:
        return SCENE_CINE_SKY_HORIZON_G;
    case UICinematicParam::HorizonBlue:
        return SCENE_CINE_SKY_HORIZON_B;
    case UICinematicParam::ZenithRed:
        return SCENE_CINE_SKY_ZENITH_R;
    case UICinematicParam::ZenithGreen:
        return SCENE_CINE_SKY_ZENITH_G;
    case UICinematicParam::ZenithBlue:
        return SCENE_CINE_SKY_ZENITH_B;
    case UICinematicParam::CloudCoverage:
        return SCENE_CINE_CLOUD_COVERAGE;
    case UICinematicParam::CloudSoftness:
        return SCENE_CINE_CLOUD_SOFTNESS;
    case UICinematicParam::CloudScale:
        return SCENE_CINE_CLOUD_SCALE;
    case UICinematicParam::CloudIntensity:
        return SCENE_CINE_CLOUD_INTENSITY;
    case UICinematicParam::ShaftStrength:
        return SCENE_CINE_SUN_SHAFT_STRENGTH;
    case UICinematicParam::ShaftFalloff:
        return SCENE_CINE_SUN_SHAFT_FALLOFF;
    case UICinematicParam::VolumetricStrength:
        return SCENE_CINE_VOLUMETRIC_STRENGTH;
    case UICinematicParam::VolumetricDensity:
        return SCENE_CINE_VOLUMETRIC_DENSITY;
    case UICinematicParam::VolumetricDecay:
        return SCENE_CINE_VOLUMETRIC_DECAY;
    case UICinematicParam::BloomThreshold:
        return SCENE_CINE_BLOOM_THRESHOLD;
    case UICinematicParam::BloomKnee:
        return SCENE_CINE_BLOOM_KNEE;
    case UICinematicParam::BloomStrength:
        return SCENE_CINE_BLOOM_STRENGTH;
    case UICinematicParam::BloomRadius:
        return SCENE_CINE_BLOOM_RADIUS;
    case UICinematicParam::TerrainRelief:
        return SCENE_CINE_TERRAIN_RELIEF;
    case UICinematicParam::TerrainTintRed:
    case UICinematicParam::TerrainTintGreen:
    case UICinematicParam::TerrainTintBlue:
        return SCENE_CINE_TERRAIN_TINT;
    case UICinematicParam::TerrainAccentRed:
    case UICinematicParam::TerrainAccentGreen:
    case UICinematicParam::TerrainAccentBlue:
        return SCENE_CINE_TERRAIN_ACCENT;
    case UICinematicParam::TerrainGridScale:
    case UICinematicParam::TerrainGridStrength:
        return SCENE_CINE_TERRAIN_GRID;
    case UICinematicParam::WaterTintRed:
    case UICinematicParam::WaterTintGreen:
    case UICinematicParam::WaterTintBlue:
        return SCENE_CINE_WATER_TINT;
    case UICinematicParam::WaterAlpha:
    case UICinematicParam::WaterReflection:
    case UICinematicParam::WaterGlint:
        return SCENE_CINE_WATER_PROFILE;
    case UICinematicParam::BasinCenterX:
    case UICinematicParam::BasinCenterZ:
    case UICinematicParam::BasinRadiusX:
    case UICinematicParam::BasinRadiusZ:
    case UICinematicParam::BasinFeather:
        return SCENE_CINE_BASIN_MASK;
    case UICinematicParam::BasinDepth:
        return SCENE_CINE_BASIN_DEPTH;
    case UICinematicParam::BasinRimLift:
        return SCENE_CINE_BASIN_RIM_LIFT;
    case UICinematicParam::FogDensity:
        return SCENE_CINE_FOG_DENSITY;
    case UICinematicParam::FogOpacity:
        return SCENE_CINE_FOG_MAX_OPACITY;
    case UICinematicParam::FogStart:
        return SCENE_CINE_FOG_START;
    case UICinematicParam::FogEnd:
        return SCENE_CINE_FOG_END;
    case UICinematicParam::FogRed:
        return SCENE_CINE_FOG_COLOR_R;
    case UICinematicParam::FogGreen:
        return SCENE_CINE_FOG_COLOR_G;
    case UICinematicParam::FogBlue:
        return SCENE_CINE_FOG_COLOR_B;
    default:
        return 0;
    }
}


uint64_t CinematicOverrideMaskForUIFeature( UICinematicFeature feature )
{
    switch ( feature )
    {
    case UICinematicFeature::Sky:
        return SCENE_CINE_SKY_ATMOSPHERE;
    case UICinematicFeature::Clouds:
        return SCENE_CINE_CLOUDS;
    case UICinematicFeature::GodRays:
        return SCENE_CINE_GOD_RAYS;
    case UICinematicFeature::VolumetricLight:
        return SCENE_CINE_VOLUMETRIC_LIGHTING;
    case UICinematicFeature::Bloom:
        return SCENE_CINE_BLOOM;
    case UICinematicFeature::Fog:
        return SCENE_CINE_FOG;
    case UICinematicFeature::TerrainRelief:
        return SCENE_CINE_TERRAIN_RELIEF_ENABLED;
    case UICinematicFeature::Shadows:
        return SCENE_CINE_SHADOWS;
    default:
        return 0;
    }
}


void ApplyWorkerThreadCountOverride( int requestedWorkerThreads )
{
    const int clampedWorkerThreads = requestedWorkerThreads < 0 ? -1 : std::clamp( requestedWorkerThreads, 0, SkullbonezCore::Threading::WorkerPool::MaxThreadCount() );
    SkullbonezCore::Threading::WorkerPool& workerPool = SkullbonezCore::Threading::WorkerPool::Instance();
    const int resolvedWorkerThreads = SkullbonezCore::Threading::WorkerPool::ResolveThreadCount( clampedWorkerThreads );
    Cfg().workerThreads = clampedWorkerThreads;
    if ( workerPool.GetThreadCount() != resolvedWorkerThreads )
    {
        workerPool.Initialise( clampedWorkerThreads );
    }
}


void ApplyCinematicUIParam( CinematicRenderConfig& cinematic, RunSceneState& scene, UICinematicParam param, float rawValue )
{
    // The UI sends "the user dragged this slider to this raw value." This helper
    // clamps the value into a safe range, writes it into the live cinematic
    // config, and marks the scene override bit so reloads keep the user's tweak.
    const auto clampValue = []( float value, float minValue, float maxValue ) -> float
    {
        return std::clamp( value, minValue, maxValue );
    };
    const auto clampIntValue = []( float value, int minValue, int maxValue ) -> int
    {
        return std::clamp( static_cast<int>( std::round( value ) ), minValue, maxValue );
    };

    switch ( param )
    {
    case UICinematicParam::Exposure:
        cinematic.exposure = clampValue( rawValue, 0.05f, 3.00f );
        scene.hasCinematicExposure = true;
        scene.cinematicExposure = cinematic.exposure;
        scene.cinematicOverrideMask |= SCENE_CINE_EXPOSURE;
        break;
    case UICinematicParam::Gamma:
        cinematic.gamma = clampValue( rawValue, 1.00f, 3.00f );
        scene.hasCinematicGamma = true;
        scene.cinematicGamma = cinematic.gamma;
        scene.cinematicOverrideMask |= SCENE_CINE_GAMMA;
        break;
    case UICinematicParam::SkyMode:
        cinematic.skyMode = clampIntValue( rawValue, 0, 32 );
        scene.cinematicOverrideMask |= SCENE_CINE_STYLE_MODES;
        break;
    case UICinematicParam::TerrainMode:
        cinematic.terrainMode = clampIntValue( rawValue, 0, 32 );
        scene.cinematicOverrideMask |= SCENE_CINE_STYLE_MODES;
        break;
    case UICinematicParam::ObjectStyle:
        cinematic.objectStyle = clampIntValue( rawValue, 0, 32 );
        scene.cinematicOverrideMask |= SCENE_CINE_STYLE_MODES;
        break;
    case UICinematicParam::WaterMode:
        cinematic.waterMode = clampIntValue( rawValue, 0, 4 );
        scene.cinematicOverrideMask |= SCENE_CINE_STYLE_MODES;
        break;
    case UICinematicParam::StyleSaturation:
        cinematic.styleSaturation = clampValue( rawValue, 0.00f, 2.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_STYLE_GRADE;
        break;
    case UICinematicParam::StyleContrast:
        cinematic.styleContrast = clampValue( rawValue, 0.00f, 2.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_STYLE_GRADE;
        break;
    case UICinematicParam::StyleVignette:
        cinematic.styleVignette = clampValue( rawValue, 0.00f, 1.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_STYLE_GRADE;
        break;
    case UICinematicParam::SunX:
        cinematic.sunScreenX = clampValue( rawValue, 0.00f, 1.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_SUN_SCREEN_X;
        break;
    case UICinematicParam::SunY:
        cinematic.sunScreenY = clampValue( rawValue, 0.00f, 1.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_SUN_SCREEN_Y;
        break;
    case UICinematicParam::SunBrightness:
        cinematic.sunIntensity = clampValue( rawValue, 0.00f, 40.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_SUN_INTENSITY;
        break;
    case UICinematicParam::SunRed:
        cinematic.sunColorR = clampValue( rawValue, 0.00f, 2.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_SUN_COLOR_R;
        break;
    case UICinematicParam::SunGreen:
        cinematic.sunColorG = clampValue( rawValue, 0.00f, 2.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_SUN_COLOR_G;
        break;
    case UICinematicParam::SunBlue:
        cinematic.sunColorB = clampValue( rawValue, 0.00f, 2.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_SUN_COLOR_B;
        break;
    case UICinematicParam::SkyGlow:
        cinematic.skyGlowStrength = clampValue( rawValue, 0.00f, 8.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_SKY_GLOW_STRENGTH;
        break;
    case UICinematicParam::HorizonRed:
        cinematic.skyHorizonR = clampValue( rawValue, 0.00f, 1.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_SKY_HORIZON_R;
        break;
    case UICinematicParam::HorizonGreen:
        cinematic.skyHorizonG = clampValue( rawValue, 0.00f, 1.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_SKY_HORIZON_G;
        break;
    case UICinematicParam::HorizonBlue:
        cinematic.skyHorizonB = clampValue( rawValue, 0.00f, 1.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_SKY_HORIZON_B;
        break;
    case UICinematicParam::ZenithRed:
        cinematic.skyZenithR = clampValue( rawValue, 0.00f, 1.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_SKY_ZENITH_R;
        break;
    case UICinematicParam::ZenithGreen:
        cinematic.skyZenithG = clampValue( rawValue, 0.00f, 1.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_SKY_ZENITH_G;
        break;
    case UICinematicParam::ZenithBlue:
        cinematic.skyZenithB = clampValue( rawValue, 0.00f, 1.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_SKY_ZENITH_B;
        break;
    case UICinematicParam::CloudCoverage:
        cinematic.cloudCoverage = clampValue( rawValue, 0.00f, 1.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_CLOUD_COVERAGE;
        break;
    case UICinematicParam::CloudSoftness:
        cinematic.cloudSoftness = clampValue( rawValue, 0.01f, 0.65f );
        scene.cinematicOverrideMask |= SCENE_CINE_CLOUD_SOFTNESS;
        break;
    case UICinematicParam::CloudScale:
        cinematic.cloudScale = clampValue( rawValue, 0.50f, 12.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_CLOUD_SCALE;
        break;
    case UICinematicParam::CloudIntensity:
        cinematic.cloudIntensity = clampValue( rawValue, 0.00f, 1.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_CLOUD_INTENSITY;
        break;
    case UICinematicParam::ShaftStrength:
        cinematic.sunShaftStrength = clampValue( rawValue, 0.00f, 3.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_SUN_SHAFT_STRENGTH;
        break;
    case UICinematicParam::ShaftFalloff:
        cinematic.sunShaftFalloff = clampValue( rawValue, 0.25f, 5.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_SUN_SHAFT_FALLOFF;
        break;
    case UICinematicParam::VolumetricStrength:
        cinematic.volumetricStrength = clampValue( rawValue, 0.00f, 2.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_VOLUMETRIC_STRENGTH;
        break;
    case UICinematicParam::VolumetricDensity:
        cinematic.volumetricDensity = clampValue( rawValue, 0.00f, 2.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_VOLUMETRIC_DENSITY;
        break;
    case UICinematicParam::VolumetricDecay:
        cinematic.volumetricDecay = clampValue( rawValue, 0.800f, 0.995f );
        scene.cinematicOverrideMask |= SCENE_CINE_VOLUMETRIC_DECAY;
        break;
    case UICinematicParam::BloomThreshold:
        cinematic.bloomThreshold = clampValue( rawValue, 0.00f, 4.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_BLOOM_THRESHOLD;
        break;
    case UICinematicParam::BloomKnee:
        cinematic.bloomKnee = clampValue( rawValue, 0.01f, 2.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_BLOOM_KNEE;
        break;
    case UICinematicParam::BloomStrength:
        cinematic.bloomStrength = clampValue( rawValue, 0.00f, 2.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_BLOOM_STRENGTH;
        break;
    case UICinematicParam::BloomRadius:
        cinematic.bloomRadius = clampValue( rawValue, 0.25f, 8.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_BLOOM_RADIUS;
        break;
    case UICinematicParam::TerrainRelief:
        cinematic.terrainRelief = clampValue( rawValue, 0.00f, 1.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_TERRAIN_RELIEF;
        break;
    case UICinematicParam::TerrainTintRed:
        cinematic.terrainTintR = clampValue( rawValue, 0.00f, 1.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_TERRAIN_TINT;
        break;
    case UICinematicParam::TerrainTintGreen:
        cinematic.terrainTintG = clampValue( rawValue, 0.00f, 1.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_TERRAIN_TINT;
        break;
    case UICinematicParam::TerrainTintBlue:
        cinematic.terrainTintB = clampValue( rawValue, 0.00f, 1.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_TERRAIN_TINT;
        break;
    case UICinematicParam::TerrainAccentRed:
        cinematic.terrainAccentR = clampValue( rawValue, 0.00f, 1.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_TERRAIN_ACCENT;
        break;
    case UICinematicParam::TerrainAccentGreen:
        cinematic.terrainAccentG = clampValue( rawValue, 0.00f, 1.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_TERRAIN_ACCENT;
        break;
    case UICinematicParam::TerrainAccentBlue:
        cinematic.terrainAccentB = clampValue( rawValue, 0.00f, 1.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_TERRAIN_ACCENT;
        break;
    case UICinematicParam::TerrainGridScale:
        cinematic.terrainGridScale = clampValue( rawValue, 0.10f, 120.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_TERRAIN_GRID;
        break;
    case UICinematicParam::TerrainGridStrength:
        cinematic.terrainGridStrength = clampValue( rawValue, 0.00f, 4.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_TERRAIN_GRID;
        break;
    case UICinematicParam::WaterTintRed:
        cinematic.waterTintR = clampValue( rawValue, 0.00f, 1.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_WATER_TINT;
        break;
    case UICinematicParam::WaterTintGreen:
        cinematic.waterTintG = clampValue( rawValue, 0.00f, 1.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_WATER_TINT;
        break;
    case UICinematicParam::WaterTintBlue:
        cinematic.waterTintB = clampValue( rawValue, 0.00f, 1.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_WATER_TINT;
        break;
    case UICinematicParam::WaterAlpha:
        cinematic.waterAlpha = clampValue( rawValue, 0.00f, 1.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_WATER_PROFILE;
        break;
    case UICinematicParam::WaterReflection:
        cinematic.waterReflectionStrength = clampValue( rawValue, 0.00f, 1.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_WATER_PROFILE;
        break;
    case UICinematicParam::WaterGlint:
        cinematic.waterGlintStrength = clampValue( rawValue, 0.00f, 4.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_WATER_PROFILE;
        break;
    case UICinematicParam::BasinCenterX:
        cinematic.basinCenterX = clampValue( rawValue, 0.00f, 1200.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_BASIN_MASK;
        break;
    case UICinematicParam::BasinCenterZ:
        cinematic.basinCenterZ = clampValue( rawValue, 0.00f, 1200.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_BASIN_MASK;
        break;
    case UICinematicParam::BasinRadiusX:
        cinematic.basinRadiusX = clampValue( rawValue, 1.00f, 500.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_BASIN_MASK;
        break;
    case UICinematicParam::BasinRadiusZ:
        cinematic.basinRadiusZ = clampValue( rawValue, 1.00f, 500.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_BASIN_MASK;
        break;
    case UICinematicParam::BasinFeather:
        cinematic.basinFeather = clampValue( rawValue, 0.00f, 1.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_BASIN_MASK;
        break;
    case UICinematicParam::BasinDepth:
        cinematic.basinDepth = clampValue( rawValue, 0.00f, 80.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_BASIN_DEPTH;
        break;
    case UICinematicParam::BasinRimLift:
        cinematic.basinRimLift = clampValue( rawValue, 0.00f, 60.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_BASIN_RIM_LIFT;
        break;
    case UICinematicParam::FogDensity:
        cinematic.fogDensity = clampValue( rawValue, 0.00000f, 0.00600f );
        scene.cinematicOverrideMask |= SCENE_CINE_FOG_DENSITY;
        break;
    case UICinematicParam::FogOpacity:
        cinematic.fogMaxOpacity = clampValue( rawValue, 0.00f, 1.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_FOG_MAX_OPACITY;
        break;
    case UICinematicParam::FogStart:
        cinematic.fogStart = clampValue( rawValue, 0.00f, 500.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_FOG_START;
        break;
    case UICinematicParam::FogEnd:
        cinematic.fogEnd = clampValue( rawValue, 100.00f, 4000.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_FOG_END;
        break;
    case UICinematicParam::FogRed:
        cinematic.fogColorR = clampValue( rawValue, 0.00f, 1.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_FOG_COLOR_R;
        break;
    case UICinematicParam::FogGreen:
        cinematic.fogColorG = clampValue( rawValue, 0.00f, 1.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_FOG_COLOR_G;
        break;
    case UICinematicParam::FogBlue:
        cinematic.fogColorB = clampValue( rawValue, 0.00f, 1.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_FOG_COLOR_B;
        break;
    default:
        break;
    }

    const uint64_t touchedMask = CinematicOverrideMaskForUIParam( param );
    if ( touchedMask != 0 )
    {
        scene.cinematicOverrideMask |= touchedMask;
        scene.uiCinematicOverrideMask |= touchedMask;
    }
}


void SetCinematicShadowsEnabledFromUI( CinematicRenderConfig& cinematic, RunSceneState& scene, bool enabled )
{
    // Shadow maps are configured next to the cinematic controls because the
    // original implementation grew from that renderer work, but the depth pass
    // now feeds normal rendering too. Toggling shadows from either the Options
    // tab or the Cine tab must therefore only touch the shadow flag and scene
    // override bits; it must not silently enable the HDR/post-processing stack.
    cinematic.shadowsEnabled = enabled;
    scene.cinematicOverrideMask |= SCENE_CINE_SHADOWS;
    scene.uiCinematicOverrideMask |= SCENE_CINE_SHADOWS;
}

void ApplyOrdinaryRenderUIParam( OrdinaryRenderConfig& ordinary, UIRenderParam param, float rawValue )
{
    switch ( param )
    {
    case UIRenderParam::SunIntensity:
        ordinary.sunIntensity = std::clamp( rawValue, 0.0f, 4.0f );
        break;
    case UIRenderParam::SunRed:
        ordinary.sunColorR = std::clamp( rawValue, 0.0f, 2.0f );
        break;
    case UIRenderParam::SunGreen:
        ordinary.sunColorG = std::clamp( rawValue, 0.0f, 2.0f );
        break;
    case UIRenderParam::SunBlue:
        ordinary.sunColorB = std::clamp( rawValue, 0.0f, 2.0f );
        break;
    case UIRenderParam::AmbientStrength:
        ordinary.ambientStrength = std::clamp( rawValue, 0.0f, 1.5f );
        break;
    case UIRenderParam::SkyRed:
        ordinary.skyAmbientR = std::clamp( rawValue, 0.0f, 1.5f );
        break;
    case UIRenderParam::SkyGreen:
        ordinary.skyAmbientG = std::clamp( rawValue, 0.0f, 1.5f );
        break;
    case UIRenderParam::SkyBlue:
        ordinary.skyAmbientB = std::clamp( rawValue, 0.0f, 1.5f );
        break;
    case UIRenderParam::GroundRed:
        ordinary.groundAmbientR = std::clamp( rawValue, 0.0f, 1.5f );
        break;
    case UIRenderParam::GroundGreen:
        ordinary.groundAmbientG = std::clamp( rawValue, 0.0f, 1.5f );
        break;
    case UIRenderParam::GroundBlue:
        ordinary.groundAmbientB = std::clamp( rawValue, 0.0f, 1.5f );
        break;
    case UIRenderParam::ShadowStrength:
        ordinary.shadowStrength = std::clamp( rawValue, 0.0f, 1.0f );
        break;
    case UIRenderParam::ShadowSoftness:
        ordinary.shadowSoftness = std::clamp( rawValue, 0.25f, 4.0f );
        break;
    case UIRenderParam::ShadowDepthBias:
        ordinary.shadowDepthBias = std::clamp( rawValue, 0.0f, 0.005f );
        break;
    case UIRenderParam::ShadowSlopeBias:
        ordinary.shadowSlopeBias = std::clamp( rawValue, 0.0f, 0.005f );
        break;
    case UIRenderParam::WaterRed:
        ordinary.waterTintR = std::clamp( rawValue, 0.0f, 1.5f );
        break;
    case UIRenderParam::WaterGreen:
        ordinary.waterTintG = std::clamp( rawValue, 0.0f, 1.5f );
        break;
    case UIRenderParam::WaterBlue:
        ordinary.waterTintB = std::clamp( rawValue, 0.0f, 1.5f );
        break;
    case UIRenderParam::WaterAlpha:
        ordinary.waterAlpha = std::clamp( rawValue, 0.0f, 1.0f );
        break;
    case UIRenderParam::WaterReflection:
        ordinary.waterReflectionStrength = std::clamp( rawValue, 0.0f, 1.0f );
        break;
    case UIRenderParam::WaterFresnel:
        ordinary.waterFresnelF0 = std::clamp( rawValue, 0.0f, 0.12f );
        break;
    case UIRenderParam::BallRoughness:
        ordinary.ballRoughnessScale = std::clamp( rawValue, 0.25f, 2.0f );
        break;
    case UIRenderParam::BallSpecular:
        ordinary.ballSpecularScale = std::clamp( rawValue, 0.0f, 2.0f );
        break;
    case UIRenderParam::BoxRoughness:
        ordinary.boxRoughnessScale = std::clamp( rawValue, 0.25f, 2.0f );
        break;
    case UIRenderParam::BoxSpecular:
        ordinary.boxSpecularScale = std::clamp( rawValue, 0.0f, 2.0f );
        break;
    default:
        break;
    }
}


void ToggleCinematicUIFeature( CinematicRenderConfig& cinematic, RunSceneState& scene, UICinematicFeature feature )
{
    // Feature toggles are boolean pass switches: sky on/off, bloom on/off, etc.
    // Each toggle also marks the matching override bit for scene persistence.
    switch ( feature )
    {
    case UICinematicFeature::Sky:
        cinematic.skyAtmosphereEnabled = !cinematic.skyAtmosphereEnabled;
        scene.cinematicOverrideMask |= SCENE_CINE_SKY_ATMOSPHERE;
        break;
    case UICinematicFeature::Clouds:
        cinematic.cloudsEnabled = !cinematic.cloudsEnabled;
        scene.cinematicOverrideMask |= SCENE_CINE_CLOUDS;
        break;
    case UICinematicFeature::GodRays:
        cinematic.godRaysEnabled = !cinematic.godRaysEnabled;
        scene.cinematicOverrideMask |= SCENE_CINE_GOD_RAYS;
        break;
    case UICinematicFeature::VolumetricLight:
        cinematic.volumetricLightingEnabled = !cinematic.volumetricLightingEnabled;
        scene.cinematicOverrideMask |= SCENE_CINE_VOLUMETRIC_LIGHTING;
        break;
    case UICinematicFeature::Bloom:
        cinematic.bloomEnabled = !cinematic.bloomEnabled;
        scene.cinematicOverrideMask |= SCENE_CINE_BLOOM;
        break;
    case UICinematicFeature::Fog:
        cinematic.fogEnabled = !cinematic.fogEnabled;
        scene.cinematicOverrideMask |= SCENE_CINE_FOG;
        break;
    case UICinematicFeature::TerrainRelief:
        cinematic.terrainReliefEnabled = !cinematic.terrainReliefEnabled;
        scene.cinematicOverrideMask |= SCENE_CINE_TERRAIN_RELIEF_ENABLED;
        break;
    case UICinematicFeature::Shadows:
        SetCinematicShadowsEnabledFromUI( cinematic, scene, !cinematic.shadowsEnabled );
        break;
    default:
        break;
    }

    const uint64_t touchedMask = CinematicOverrideMaskForUIFeature( feature );
    if ( touchedMask != 0 )
    {
        scene.cinematicOverrideMask |= touchedMask;
        scene.uiCinematicOverrideMask |= touchedMask;
    }
}
} // namespace

RunEditorTracer::RunEditorTracer()
{
    m_lineData.reserve( 4096 );
}


void RunEditorTracer::Clear()
{
    m_lineData.clear();
}


void RunEditorTracer::EmitLine( const Vector3& a, const Vector3& b, float r, float g, float bl )
{
    m_lineData.insert( m_lineData.end(), { a.x, a.y, a.z, r, g, bl, b.x, b.y, b.z, r, g, bl } );
}


void RunEditorTracer::EmitArrow( const Vector3& a, const Vector3& b, float r, float g, float bl )
{
    EmitLine( a, b, r, g, bl );

    Vector3 dir = b - a;
    const float len = VectorMag( dir );
    if ( len <= TOLERANCE )
    {
        return;
    }
    dir /= len;

    Vector3 side = fabsf( dir.y ) < 0.8f ? CrossProduct( dir, Vector3( 0.0f, 1.0f, 0.0f ) ) : CrossProduct( dir, Vector3( 1.0f, 0.0f, 0.0f ) );
    const float sideLen = VectorMag( side );
    if ( sideLen <= TOLERANCE )
    {
        return;
    }
    side /= sideLen;

    const float head = (std::min)( len * 0.25f, 2.0f );
    const Vector3 base = b - dir * head;
    EmitLine( b, base + side * ( head * 0.45f ), r, g, bl );
    EmitLine( b, base - side * ( head * 0.45f ), r, g, bl );
}


void RunEditorTracer::EmitSphere( const Vector3& center, float radius, float r, float g, float bl )
{
    constexpr int segments = 32;
    for ( int plane = 0; plane < 3; ++plane )
    {
        Vector3 previous;
        for ( int i = 0; i <= segments; ++i )
        {
            const float theta = static_cast<float>( i ) * ( 2.0f * _PI / static_cast<float>( segments ) );
            const float c = cosf( theta ) * radius;
            const float s = sinf( theta ) * radius;
            Vector3 next = center;
            if ( plane == 0 )
            {
                next.x += c;
                next.z += s;
            }
            else if ( plane == 1 )
            {
                next.x += c;
                next.y += s;
            }
            else
            {
                next.y += c;
                next.z += s;
            }

            if ( i > 0 )
            {
                EmitLine( previous, next, r, g, bl );
            }
            previous = next;
        }
    }
}


void RunEditorTracer::EmitBox( const Vector3& center, const Vector3& xAxis, const Vector3& yAxis, const Vector3& zAxis, float r, float g, float bl )
{
    const Vector3 corners[8] = {
        center - xAxis - yAxis - zAxis,
        center + xAxis - yAxis - zAxis,
        center + xAxis + yAxis - zAxis,
        center - xAxis + yAxis - zAxis,
        center - xAxis - yAxis + zAxis,
        center + xAxis - yAxis + zAxis,
        center + xAxis + yAxis + zAxis,
        center - xAxis + yAxis + zAxis,
    };

    static constexpr int kEdges[12][2] = {
        { 0, 1 },
        { 1, 2 },
        { 2, 3 },
        { 3, 0 },
        { 4, 5 },
        { 5, 6 },
        { 6, 7 },
        { 7, 4 },
        { 0, 4 },
        { 1, 5 },
        { 2, 6 },
        { 3, 7 },
    };
    for ( const auto& edge : kEdges )
    {
        EmitLine( corners[edge[0]], corners[edge[1]], r, g, bl );
    }
}


void RunEditorTracer::AddPlacementRay( const Vector3& rayOrigin, const Vector3& hitPoint )
{
    EmitLine( rayOrigin, hitPoint, 0.25f, 0.80f, 1.0f );
}


void RunEditorTracer::AddPlacementGhost( int fixedObjectType, const Vector3& center )
{
    const int type = std::clamp( fixedObjectType, 0, SkullbonezCore::UI::EditorTab::FIXED_TYPE_COUNT - 1 );
    constexpr float ghostR = 0.25f;
    constexpr float ghostG = 1.0f;
    constexpr float ghostB = 0.85f;

    switch ( type )
    {
    case SkullbonezCore::UI::EditorTab::FIXED_BOX:
        EmitBox( center, Vector3( 6.0f, 0.0f, 0.0f ), Vector3( 0.0f, 6.0f, 0.0f ), Vector3( 0.0f, 0.0f, 6.0f ), ghostR, ghostG, ghostB );
        break;
    case SkullbonezCore::UI::EditorTab::FIXED_BALL:
        EmitSphere( center, 4.0f, ghostR, ghostG, ghostB );
        break;
    case SkullbonezCore::UI::EditorTab::FIXED_SPHERE:
        EmitSphere( center, 8.0f, ghostR, ghostG, ghostB );
        break;
    default:
    {
        const ConvexHullShape* hull = CachedFixedHullForType( type );
        if ( !hull )
        {
            return;
        }
        const Vector3 hullCenter = center + hull->GetPosition();
        for ( uint16_t edgeIndex = 0; edgeIndex < hull->GetEdgeCount(); ++edgeIndex )
        {
            const ConvexHullEdge& edge = hull->GetEdge( edgeIndex );
            EmitLine( hullCenter + hull->GetVertex( edge.vertexA ), hullCenter + hull->GetVertex( edge.vertexB ), ghostR, ghostG, ghostB );
        }
        break;
    }
    }
}


void RunEditorTracer::AddSelectionOutline( const GameModel& model )
{
    Quaternion orientation = model.GetOrientation();
    const RotationMatrix rot = orientation.GetOrientationMatrix();
    constexpr float outlineR = 1.0f;
    constexpr float outlineG = 1.0f;
    constexpr float outlineB = 0.55f;

    const CollisionShape& shape = model.GetCollisionShape();
    if ( const BoundingSphere* sphere = std::get_if<BoundingSphere>( &shape ) )
    {
        EmitSphere( model.GetPosition() + rot * sphere->GetPosition(), sphere->GetBoundingRadius(), outlineR, outlineG, outlineB );
        return;
    }
    if ( const BoundingBox* box = std::get_if<BoundingBox>( &shape ) )
    {
        const Vector3& he = box->GetHalfExtents();
        const Vector3 center = model.GetPosition() + rot * box->GetPosition();
        EmitBox( center,
                 rot * Vector3( he.x, 0.0f, 0.0f ),
                 rot * Vector3( 0.0f, he.y, 0.0f ),
                 rot * Vector3( 0.0f, 0.0f, he.z ),
                 outlineR,
                 outlineG,
                 outlineB );
        return;
    }
    if ( const ConvexHullShape* hull = std::get_if<ConvexHullShape>( &shape ) )
    {
        const Vector3 hullCenter = model.GetPosition() + rot * hull->GetPosition();
        for ( uint16_t edgeIndex = 0; edgeIndex < hull->GetEdgeCount(); ++edgeIndex )
        {
            const ConvexHullEdge& edge = hull->GetEdge( edgeIndex );
            EmitLine( hullCenter + rot * hull->GetVertex( edge.vertexA ), hullCenter + rot * hull->GetVertex( edge.vertexB ), outlineR, outlineG, outlineB );
        }
    }
}


void RunEditorTracer::AddGizmo( const Vector3& origin, float radius, int hotAxis, int activeAxis )
{
    const float length = EditorGizmoAxisLength( radius );
    for ( int axis = 0; axis < 3; ++axis )
    {
        float r = axis == 0 ? 1.0f : 0.08f;
        float g = axis == 1 ? 0.95f : 0.10f;
        float b = axis == 2 ? 1.0f : 0.08f;
        if ( activeAxis == axis )
        {
            r = 1.0f;
            g = 1.0f;
            b = 0.15f;
        }
        else if ( hotAxis == axis )
        {
            r = (std::min)( 1.0f, r + 0.45f );
            g = (std::min)( 1.0f, g + 0.45f );
            b = (std::min)( 1.0f, b + 0.45f );
        }

        const Vector3 axisVector = EditorAxisVector( axis );
        EmitArrow( origin, origin + axisVector * length, r, g, b );
    }
}


void RunEditorTracer::Render( const Matrix4& viewProjection )
{
    if ( m_lineData.empty() || !IsGfxReady() )
    {
        return;
    }
    Gfx().DrawLinesColored( m_lineData.data(), static_cast<int>( m_lineData.size() / 6 ), viewProjection.Data() );
}


void SkullbonezRun::StepPhysicsPipelineStage( int direction )
{
    const int stageCount = static_cast<int>( PhysicsPipelineStage::Count );
    if ( stageCount <= 0 || direction == 0 )
    {
        return;
    }

    m_debug.physicsDebugFlags |= PHYSICS_DEBUG_PIPELINE;
    int nextStage = ( m_debug.physicsDebugPipelineStageCursor + direction ) % stageCount;
    if ( nextStage < 0 )
    {
        nextStage += stageCount;
    }
    m_debug.physicsDebugPipelineStageCursor = nextStage;
}


void SkullbonezRun::TakeInput()
{
    if ( !Input::IsAppFocused() )
    {
        Input::SetSystemCursorVisible( true );
        m_editor.viewportLookActive = false;
        InputController::ResetUnfocusedInput( m_camera, m_leftSceneCycleWasDown, m_rightSceneCycleWasDown );
        m_UI.CancelInputCapture();
        RunUIStressActions();
        return;
    }

    const auto CameraMouseOwnsCursor = [&]() -> bool
    {
        return ( m_camera.isFlyMode && !m_UI.WantsNativeMouseCursor() && !m_UI.BlocksCameraMouse() ) ||
               ( m_editor.fixedPlacementEnabled && m_editor.viewportLookActive && !m_UI.BlocksCameraMouse() );
    };
    const auto ApplyCursorOwnership = [&]() -> void
    {
        Input::SetSystemCursorVisible( !CameraMouseOwnsCursor() );
    };
    const auto ReleaseMouseToUI = [&]() -> void
    {
        if ( !CameraMouseOwnsCursor() )
        {
            ReleaseCapture();
            InputController::ResetMouseLook( m_camera );
        }
    };

    ApplyCursorOwnership();

    const bool UIBlocksKeyboardBeforeInput = m_UI.BlocksKeyboard();
    if ( !UIBlocksKeyboardBeforeInput )
    {
        // Toggle fly mode with F (edge-detected so snapshot-loaded fly mode survives the next frame)
        bool prevFlyMode = m_camera.isFlyMode;
        const RuntimeKeyEdge fEdge = InputController::CaptureKeyEdge( m_camera.input, InputState::FWasDown, 'F' );
        if ( fEdge.wasPressed )
        {
            m_camera.isFlyMode = !m_camera.isFlyMode;
            m_camera.isNudgeMode = false; // F-key fly never implies nudge
        }

        // N key: toggle nudge mode — free camera with live simulation (edge-detected).
        // Nudge entering also enters fly mode; nudge exiting also exits fly mode.
        {
            const RuntimeKeyEdge nEdge = InputController::CaptureKeyEdge( m_camera.input, InputState::NWasDown, 'N' );
            if ( nEdge.wasPressed )
            {
                m_camera.isNudgeMode = !m_camera.isNudgeMode;
                m_camera.isFlyMode = m_camera.isNudgeMode;
            }
        }

#ifdef _DEBUG
        {
            const RuntimeKeyEdge enterEdge = InputController::CaptureKeyEdge( m_camera.input, InputState::EnterWasDown, VK_RETURN );
            if ( enterEdge.wasPressed && m_camera.isNudgeMode )
            {
                WriteNudgeReproSnapshot();
            }
        }
#endif

        if ( m_camera.isFlyMode != prevFlyMode )
        {
            if ( m_camera.isFlyMode )
            {
                // Entering fly mode: generated demo mode snaps to free camera; scene mode stays
                // on the current camera so fly controls work without requiring CAMERA_FREE
                if ( !SceneState().isSceneMode )
                {
                    m_systems.cameras->SelectCamera( CAMERA_FREE, false );
                }
                m_camera.cameraTime = 0.0f;
                XZBounds unbounded;
                unbounded.m_xMin = -99999.9f;
                unbounded.m_xMax = 99999.9f;
                unbounded.m_zMin = -99999.9f;
                unbounded.m_zMax = 99999.9f;
                uint32_t activeCam = SceneState().isSceneMode ? m_systems.cameras->GetSelectedCameraName() : CAMERA_FREE;
                m_systems.cameras->SetCameraXZBounds( activeCam, unbounded );
                if ( CameraMouseOwnsCursor() )
                {
                    Input::SetSystemCursorVisible( false );
                }
                else
                {
                    ReleaseMouseToUI();
                    Input::SetSystemCursorVisible( true );
                }
                InputController::ResetMouseLook( m_camera );
            }
            else
            {
                // Exiting fly mode restores terrain bounds, the camera-cycle clock, and
                // the stock Windows cursor.
                uint32_t activeCam = SceneState().isSceneMode ? m_systems.cameras->GetSelectedCameraName() : CAMERA_FREE;
                m_systems.cameras->SetCameraXZBounds( activeCam, m_systems.terrain->GetXZBounds() );
                Input::SetSystemCursorVisible( true );
                m_camera.cameraTime = 0.0f;
                // Exiting fly mode also exits nudge mode
                m_camera.isNudgeMode = false;
                InputController::ResetMouseLook( m_camera );
            }
        }

        // Water m_shader debug toggles
        const RuntimeKeyEdge key1Edge = InputController::CaptureKeyEdge( m_camera.input, InputState::Key1WasDown, '1' );
        if ( key1Edge.wasPressed )
        {
            m_debug.isWaterFreezeDebug = !m_debug.isWaterFreezeDebug;
            if ( m_debug.isWaterFreezeDebug )
            {
                m_debug.frozenWaterTime = static_cast<float>( m_timers.simulationTimer.GetTimeSinceLastStart() );
            }
        }
        // Key '2' cycles water reflection modes in a predictable loop:
        // FBO mirror rendering, then DXR raytraced reflection when supported,
        // then no reflection, then back to FBO. Machines without DXR skip the
        // unsupported mode instead of leaving the toggle in a dead state.
        {
            static bool s_key2WasDown = false;
            if ( InputController::CaptureKeyPress( s_key2WasDown, '2' ) )
            {
                if ( !m_debug.isWaterRTReflect && !m_debug.isWaterNoReflect )
                {
                    if ( Gfx().GetCapabilities().supportsDxrReflection )
                    {
                        m_debug.isWaterRTReflect = true;
                    }
                    else
                    {
                        m_debug.isWaterNoReflect = true;
                    }
                }
                else if ( m_debug.isWaterRTReflect )
                {
                    m_debug.isWaterRTReflect = false;
                    m_debug.isWaterNoReflect = true;
                }
                else
                {
                    m_debug.isWaterNoReflect = false;
                }
            }
        }
        {
            const RuntimeKeyEdge key3Edge = InputController::CaptureKeyEdge( m_camera.input, InputState::Key3WasDown, '3' );
            if ( key3Edge.wasPressed )
            {
                m_debug.isWaterFlatDebug = !m_debug.isWaterFlatDebug;
            }
        }
        {
            const RuntimeKeyEdge key4Edge = InputController::CaptureKeyEdge( m_camera.input, InputState::Key4WasDown, '4' );
            if ( key4Edge.wasPressed )
            {
                m_debug.isTerrainHidden = !m_debug.isTerrainHidden;
            }
        }
        {
            const RuntimeKeyEdge key5Edge = InputController::CaptureKeyEdge( m_camera.input, InputState::Key5WasDown, '5' );
            if ( key5Edge.wasPressed )
            {
                m_debug.isWaterHidden = !m_debug.isWaterHidden;
            }
        }
        // V key: collision visualizer. Renders balls and boxes as solid debug colours.
        {
            const RuntimeKeyEdge vEdge = InputController::CaptureKeyEdge( m_camera.input, InputState::VWasDown, 'V' );
            if ( vEdge.wasPressed )
            {
                m_debug.isCollisionVisualizer = !m_debug.isCollisionVisualizer;
            }
        }

        // C key: cycle physics debug overlay - None -> Axes -> Contacts -> Sleep -> All -> None.
        {
            const RuntimeKeyEdge cEdge = InputController::CaptureKeyEdge( m_camera.input, InputState::CKeyWasDown, 'C' );
            if ( cEdge.wasPressed )
            {
                switch ( m_debug.physicsDebugFlags )
                {
                case PHYSICS_DEBUG_NONE:
                    m_debug.physicsDebugFlags = PHYSICS_DEBUG_AXES;
                    break;
                case PHYSICS_DEBUG_AXES:
                    m_debug.physicsDebugFlags = PHYSICS_DEBUG_CONTACTS;
                    break;
                case PHYSICS_DEBUG_CONTACTS:
                    m_debug.physicsDebugFlags = PHYSICS_DEBUG_SLEEP;
                    break;
                case PHYSICS_DEBUG_SLEEP:
                    m_debug.physicsDebugFlags = PHYSICS_DEBUG_ALL;
                    break;
                default:
                    m_debug.physicsDebugFlags = PHYSICS_DEBUG_NONE;
                    break;
                }
            }
        }

        // O key: toggle the terrain polygon/contact probe. It is independent of
        // the C-key debug cycle so it can be layered over any other physics view.
        {
            const RuntimeKeyEdge oEdge = InputController::CaptureKeyEdge( m_camera.input, InputState::OKeyWasDown, 'O' );
            if ( oEdge.wasPressed )
            {
                m_debug.physicsDebugFlags ^= PHYSICS_DEBUG_TERRAIN_CONTACT;
            }
        }

        // F7/F8: step the physics pipeline visualizer through the bounded Catto
        // stage trace from the most recent physics tick. The simulation can be
        // paused with fly mode and advanced separately with Space.
        {
            static bool s_pipelinePrevWasDown = false;
            static bool s_pipelineNextWasDown = false;
            if ( InputController::CaptureKeyPress( s_pipelinePrevWasDown, VK_F7 ) )
            {
                StepPhysicsPipelineStage( -1 );
            }
            if ( InputController::CaptureKeyPress( s_pipelineNextWasDown, VK_F8 ) )
            {
                StepPhysicsPipelineStage( 1 );
            }
        }

        // 6 key: translucent debug collision volumes for inspecting axes/contact rows inside bodies.
        {
            const RuntimeKeyEdge key6Edge = InputController::CaptureKeyEdge( m_camera.input, InputState::Key6WasDown, '6' );
            if ( key6Edge.wasPressed )
            {
                m_debug.isPhysicsDebugTransparent = !m_debug.isPhysicsDebugTransparent;
            }
        }

        // Q key used to cycle legacy renderers; it now reports that DX12 is the only runtime renderer.
        {
            const RuntimeKeyEdge qEdge = InputController::CaptureKeyEdge( m_camera.input, InputState::QKeyWasDown, 'Q' );
            if ( qEdge.wasPressed )
            {
                fprintf( stderr, "Renderer switch ignored: DX12 is the only runtime renderer.\n" );
            }
        }

        // G key: toggle broadphase overlay, or cycle tracked ball if overlay is off.
        const RuntimeKeyEdge gEdge = InputController::CaptureKeyEdge( m_camera.input, InputState::GKeyWasDown, 'G' );
        if ( gEdge.wasPressed )
        {
            if ( SceneState().isSceneMode && m_camera.trackBallIndex >= 0 && !m_debug.isBroadphaseOverlay )
            {
                int count = m_cGameModelCollection.GetModelCount();
                if ( count > 0 )
                {
                    m_camera.trackBallIndex = ( m_camera.trackBallIndex + 1 ) % count;
                }
            }
            else
            {
                m_debug.isBroadphaseOverlay = !m_debug.isBroadphaseOverlay;
            }
        }

        // 0 key: toggle the in-game diagnostics window. Tabs replace the old overlay cycle.
        // Edge-detected in both scene and generated demo modes; one toggle per keypress.
        {
            const RuntimeKeyEdge key0Edge = InputController::CaptureKeyEdge( m_camera.input, InputState::Key0WasDown, '0' );
            if ( key0Edge.wasPressed )
            {
                EnterInteractiveSceneRun();
                m_UI.ToggleVisible( m_timers.simulationTimer.GetTotalTime() );
                m_debug.overlayMode = OverlayMode::None;
                ApplyCursorOwnership();
                ReleaseMouseToUI();
            }
        }

        if ( InputController::CaptureKeyPress( m_leftSceneCycleWasDown, VK_LEFT ) )
        {
            EnterInteractiveSceneRun();
            if ( !ApplyAdjacentCinematicMode( -1 ) )
            {
                LoadAdjacentSceneFromBrowser( -1 );
            }
        }
        if ( InputController::CaptureKeyPress( m_rightSceneCycleWasDown, VK_RIGHT ) )
        {
            EnterInteractiveSceneRun();
            if ( !ApplyAdjacentCinematicMode( 1 ) )
            {
                LoadAdjacentSceneFromBrowser( 1 );
            }
        }
    }
    else
    {
        m_leftSceneCycleWasDown = Input::IsKeyDown( VK_LEFT );
        m_rightSceneCycleWasDown = Input::IsKeyDown( VK_RIGHT );
    }

    bool suppressNudgeFireThisFrame = UIBlocksKeyboardBeforeInput;
    if ( m_systems.window )
    {
        const int selectedSceneBrowserIndex = CurrentSceneBrowserIndex();
        InGameUIInputResult UIResult = m_UI.UpdateInput( m_systems.window->m_sWindow,
                                                         static_cast<int>( m_systems.window->m_sWindowDimensions.x ),
                                                         static_cast<int>( m_systems.window->m_sWindowDimensions.y ),
                                                         m_timers.simulationTimer.GetTotalTime(),
                                                         m_sceneBrowserNamePtrs.empty() ? nullptr : m_sceneBrowserNamePtrs.data(),
                                                         static_cast<int>( m_sceneBrowserNamePtrs.size() ),
                                                         selectedSceneBrowserIndex );
        const InGameUICommands& uiCommands = UIResult.commands;
        if ( uiCommands.ui.userInteracted )
        {
            EnterInteractiveSceneRun();
        }
        suppressNudgeFireThisFrame = suppressNudgeFireThisFrame || uiCommands.ui.userInteracted || m_UI.BlocksCameraMouse();

        // ESC flicks the diagnostics window between minimized and expanded, with
        // a very fast double-tap escape hatch for quitting interactive runs.
        // Run it after UI input processing so focused controls keep their local ESC
        // behavior first, such as closing the scene filter combo without also
        // hiding the whole diagnostics surface on the same frame.
        const RuntimeKeyEdge escapeEdge = InputController::CaptureKeyEdge( m_camera.input, InputState::EscapeWasDown, VK_ESCAPE );
        if ( escapeEdge.wasPressed && !uiCommands.ui.userInteracted )
        {
            constexpr double ESC_QUICK_EXIT_SECONDS = 0.32;
            const double UINow = m_timers.simulationTimer.GetTotalTime();
            if ( UINow - m_lastEscapeTapTime <= ESC_QUICK_EXIT_SECONDS )
            {
                PostQuitMessage( 0 );
            }
            else
            {
                EnterInteractiveSceneRun();
                m_UI.ToggleVisible( UINow );
                m_debug.overlayMode = OverlayMode::None;
                m_lastEscapeTapTime = UINow;
                ApplyCursorOwnership();
                ReleaseMouseToUI();
            }
        }

        if ( uiCommands.renderer.toggleVsync )
        {
            m_runtimeSettings.isVsyncEnabled = !m_runtimeSettings.isVsyncEnabled;
            Gfx().SetVsyncEnabled( m_runtimeSettings.isVsyncEnabled );
        }
        if ( uiCommands.editor.requestedFixedObjectType >= 0 )
        {
            m_editor.fixedObjectType = std::clamp( uiCommands.editor.requestedFixedObjectType, 0, UI::EditorTab::FIXED_TYPE_COUNT - 1 );
        }
        if ( uiCommands.editor.toggleFixedPlacement )
        {
            EnterInteractiveSceneRun();
            m_editor.fixedPlacementEnabled = !m_editor.fixedPlacementEnabled;
            if ( !m_editor.fixedPlacementEnabled )
            {
                m_editor.viewportLookActive = false;
                m_editor.placementPreviewVisible = false;
                m_editor.gizmoDragActive = false;
                m_editor.activeGizmoAxis = -1;
                InputController::ResetMouseLook( m_camera );
            }
        }
        if ( uiCommands.physics.toggleCollisionVisualizer )
        {
            m_debug.isCollisionVisualizer = !m_debug.isCollisionVisualizer;
        }
        if ( uiCommands.physics.togglePhysicsSleepPolicy )
        {
            m_runtimeSettings.isPhysicsSleepEnabled = !m_runtimeSettings.isPhysicsSleepEnabled;
            m_cGameModelCollection.SetPhysicsSleepEnabled( m_runtimeSettings.isPhysicsSleepEnabled );
        }
        if ( uiCommands.physics.togglePhysicsDebugFlags != 0 )
        {
            m_debug.physicsDebugFlags ^= ( uiCommands.physics.togglePhysicsDebugFlags & PHYSICS_DEBUG_ALL );
        }
        if ( uiCommands.physics.stepPhysicsPipelinePrevious )
        {
            StepPhysicsPipelineStage( -1 );
        }
        if ( uiCommands.physics.stepPhysicsPipelineNext )
        {
            StepPhysicsPipelineStage( 1 );
        }
        if ( uiCommands.physics.togglePhysicsDebugTransparent )
        {
            m_debug.isPhysicsDebugTransparent = !m_debug.isPhysicsDebugTransparent;
        }
        if ( uiCommands.physics.toggleBroadphaseOverlay )
        {
            m_debug.isBroadphaseOverlay = !m_debug.isBroadphaseOverlay;
        }
        bool tornadoFieldChanged = false;
        if ( uiCommands.physics.toggleTornado )
        {
            m_runtimeSettings.tornadoField.enabled = !m_runtimeSettings.tornadoField.enabled;
            tornadoFieldChanged = true;
        }
        if ( uiCommands.physics.toggleTornadoFieldVectors )
        {
            m_runtimeSettings.tornadoField.visualizeVelocityField = !m_runtimeSettings.tornadoField.visualizeVelocityField;
            tornadoFieldChanged = true;
        }
        if ( uiCommands.physics.requestTornadoRadius )
        {
            m_runtimeSettings.tornadoField.radius = std::clamp( uiCommands.physics.requestedTornadoRadius, UI_TORNADO_RADIUS_MIN, UI_TORNADO_RADIUS_MAX );
            tornadoFieldChanged = true;
        }
        if ( uiCommands.physics.requestTornadoHeight )
        {
            m_runtimeSettings.tornadoField.height = std::clamp( uiCommands.physics.requestedTornadoHeight, UI_TORNADO_HEIGHT_MIN, UI_TORNADO_HEIGHT_MAX );
            tornadoFieldChanged = true;
        }
        if ( uiCommands.physics.requestTornadoInward )
        {
            m_runtimeSettings.tornadoField.inwardAcceleration = std::clamp( uiCommands.physics.requestedTornadoInward, UI_TORNADO_INWARD_MIN, UI_TORNADO_INWARD_MAX );
            tornadoFieldChanged = true;
        }
        if ( uiCommands.physics.requestTornadoSwirl )
        {
            m_runtimeSettings.tornadoField.swirlAcceleration = std::clamp( uiCommands.physics.requestedTornadoSwirl, UI_TORNADO_SWIRL_MIN, UI_TORNADO_SWIRL_MAX );
            tornadoFieldChanged = true;
        }
        if ( uiCommands.physics.requestTornadoLift )
        {
            m_runtimeSettings.tornadoField.liftAcceleration = std::clamp( uiCommands.physics.requestedTornadoLift, UI_TORNADO_LIFT_MIN, UI_TORNADO_LIFT_MAX );
            tornadoFieldChanged = true;
        }
        if ( tornadoFieldChanged )
        {
            SyncTornadoFieldToPhysics();
        }
        if ( uiCommands.physics.toggleTerrainContactProbe )
        {
            m_debug.physicsDebugFlags ^= PHYSICS_DEBUG_TERRAIN_CONTACT;
        }
        if ( uiCommands.sceneOptions.toggleTextOnly )
        {
            m_debug.isTextOnly = !m_debug.isTextOnly;
        }
        if ( uiCommands.sceneOptions.toggleFixedStep )
        {
            SceneState().isFixedStep = !SceneState().isFixedStep;
            m_simulation.Reset();
        }
        if ( uiCommands.sceneOptions.toggleTerrainHidden )
        {
            m_debug.isTerrainHidden = !m_debug.isTerrainHidden;
        }
        if ( uiCommands.sceneOptions.toggleWaterHidden )
        {
            m_debug.isWaterHidden = !m_debug.isWaterHidden;
        }
        if ( uiCommands.sceneOptions.toggleWaterFreeze )
        {
            m_debug.isWaterFreezeDebug = !m_debug.isWaterFreezeDebug;
            if ( m_debug.isWaterFreezeDebug )
            {
                m_debug.frozenWaterTime = static_cast<float>( m_timers.simulationTimer.GetTimeSinceLastStart() );
            }
        }
        if ( uiCommands.sceneOptions.toggleWaterFlat )
        {
            m_debug.isWaterFlatDebug = !m_debug.isWaterFlatDebug;
        }
        if ( uiCommands.sceneOptions.toggleShadows )
        {
            if ( IsCinematicRenderingEnabled() )
            {
                const bool shadowsActive = ActiveCinematicConfig().shadowsEnabled;
                m_cmdHasCinematicShadowsOverride = false;
                SetCinematicShadowsEnabledFromUI( ActiveCinematicConfig(), SceneState(), !shadowsActive );
            }
            else
            {
                Cfg().ordinaryRender.shadowsEnabled = !Cfg().ordinaryRender.shadowsEnabled;
            }
        }
        if ( uiCommands.renderTuning.toggleShadows )
        {
            Cfg().ordinaryRender.shadowsEnabled = !Cfg().ordinaryRender.shadowsEnabled;
        }
        if ( uiCommands.renderTuning.saveDefaults )
        {
            SaveRenderDefaults();
        }
        if ( uiCommands.renderTuning.requestedParam != UIRenderParam::None )
        {
            ApplyOrdinaryRenderUIParam( Cfg().ordinaryRender, uiCommands.renderTuning.requestedParam, uiCommands.renderTuning.requestedValue );
        }
        if ( uiCommands.water.toggleWaterReflection )
        {
            if ( m_debug.isWaterNoReflect )
            {
                m_debug.isWaterNoReflect = false;
            }
            else
            {
                m_debug.isWaterNoReflect = true;
                m_debug.isWaterRTReflect = false;
            }
        }
        if ( uiCommands.water.requestedWaterReflectionMode >= 0 )
        {
            const int mode = std::clamp( uiCommands.water.requestedWaterReflectionMode, 0, 2 );
            m_debug.isWaterRTReflect = mode == 1;
            m_debug.isWaterNoReflect = mode == 2;
        }
        if ( uiCommands.sceneOptions.requestedTimeScale > 0.0f )
        {
            m_UITimeScaleOverride = std::clamp( uiCommands.sceneOptions.requestedTimeScale, 0.10f, 10.00f );
            SceneState().timeScale = m_UITimeScaleOverride;
        }
        if ( uiCommands.run.requestedSeed > 0 )
        {
            SceneState().rngSeed = static_cast<unsigned int>( std::clamp( uiCommands.run.requestedSeed, 1, 999999 ) );
            SceneState().rngState = SceneState().rngSeed;
        }
        if ( uiCommands.physics.requestedPhysicsDebugAlpha >= 0.0f )
        {
            m_debug.physicsDebugAlpha = std::clamp( uiCommands.physics.requestedPhysicsDebugAlpha, 0.05f, 1.0f );
        }
        if ( uiCommands.physics.requestedPhysicsDebugContactLinger >= 0.0f )
        {
            m_debug.physicsDebugContactLinger = std::clamp( uiCommands.physics.requestedPhysicsDebugContactLinger, 0.0f, 5.0f );
        }
        if ( uiCommands.physics.requestedSpawnObjectType >= 0 )
        {
            EnterInteractiveSceneRun();
            SpawnPhysicsObjectFromCamera( uiCommands.physics.requestedSpawnObjectType );
        }
        if ( uiCommands.sceneOptions.requestedModelCount >= 0 )
        {
            ApplyUIModelCountOverride( uiCommands.sceneOptions.requestedModelCount );
        }
        if ( uiCommands.profiler.requestedWorkerThreads >= -1 )
        {
            ApplyWorkerThreadCountOverride( uiCommands.profiler.requestedWorkerThreads );
        }
        if ( uiCommands.run.requestedSolverBallCount >= 0 )
        {
            const int modelCapacity = ActiveGameModelCapacity();
            const int boxes = m_UISolverBoxCountOverride >= 0 ? m_UISolverBoxCountOverride : SceneState().solverBoxCount;
            ApplyUISolverObjectCounts( std::clamp( uiCommands.run.requestedSolverBallCount, 0, (std::max)( 0, modelCapacity - boxes ) ), boxes );
        }
        if ( uiCommands.run.requestedSolverBoxCount >= 0 )
        {
            const int modelCapacity = ActiveGameModelCapacity();
            const int balls = m_UISolverBallCountOverride >= 0 ? m_UISolverBallCountOverride : SceneState().solverBallCount;
            ApplyUISolverObjectCounts( balls, std::clamp( uiCommands.run.requestedSolverBoxCount, 0, (std::max)( 0, modelCapacity - balls ) ) );
        }
        if ( uiCommands.water.requestWorldGravity || uiCommands.water.requestWorldFluidHeight || uiCommands.water.requestWorldFluidDensity )
        {
            const float gravity = uiCommands.water.requestWorldGravity ? uiCommands.water.requestedWorldGravity : m_cWorldEnvironment.GetGravity();
            const float fluidHeight = uiCommands.water.requestWorldFluidHeight ? uiCommands.water.requestedWorldFluidHeight : m_cWorldEnvironment.GetFluidSurfaceHeight();
            const float fluidDensity = uiCommands.water.requestWorldFluidDensity ? uiCommands.water.requestedWorldFluidDensity : m_cWorldEnvironment.GetFluidDensity();
            ApplyUIWorldOverride( std::clamp( gravity, -100.0f, 0.0f ),
                                  std::clamp( fluidHeight, -100.0f, 200.0f ),
                                  std::clamp( fluidDensity, 0.0f, 5.0f ) );
        }
        if ( uiCommands.cinematic.toggleRendering )
        {
            // Master Cine switch. Clearing m_cmdHasCinematicRenderingOverride lets
            // the runtime toggle become the new source of truth after launch.
            CinematicRenderConfig& cinematic = ActiveCinematicConfig();
            const bool currentlyEnabled = m_cmdHasCinematicRenderingOverride ? m_cmdCinematicRendering : cinematic.enabled;
            cinematic.enabled = !currentlyEnabled;
            m_cmdHasCinematicRenderingOverride = false;
            if ( SceneState().isSceneMode )
            {
                SceneState().hasCinematicRenderingOverride = true;
                SceneState().isCinematicRenderingEnabled = cinematic.enabled;
                SceneState().cinematicOverrideMask |= SCENE_CINE_RENDERING;
                SceneState().uiCinematicOverrideMask |= SCENE_CINE_RENDERING;
            }
        }
        if ( uiCommands.cinematic.requestedModeSceneIndex >= -1 )
        {
            ApplyCinematicModeFromBrowserIndex( uiCommands.cinematic.requestedModeSceneIndex );
        }
        if ( uiCommands.cinematic.requestedFeature != UICinematicFeature::None )
        {
            CinematicRenderConfig& cinematic = ActiveCinematicConfig();
            if ( uiCommands.cinematic.requestedFeature == UICinematicFeature::Shadows )
            {
                m_cmdHasCinematicShadowsOverride = false;
            }
            ToggleCinematicUIFeature( cinematic, SceneState(), uiCommands.cinematic.requestedFeature );
        }
        if ( uiCommands.cinematic.requestedParam != UICinematicParam::None )
        {
            CinematicRenderConfig& cinematic = ActiveCinematicConfig();
            ApplyCinematicUIParam( cinematic, SceneState(), uiCommands.cinematic.requestedParam, uiCommands.cinematic.requestedValue );
        }
        if ( uiCommands.scene.resetScene )
        {
            EnterInteractiveSceneRun();
            ResetCurrentScene( true, true );
        }
        if ( uiCommands.scene.resetSceneDefaults )
        {
            EnterInteractiveSceneRun();
            ResetCurrentScene( false, true, false );
        }
        if ( uiCommands.scene.requestDemoScene )
        {
            LoadDemoSceneFromUI();
        }
        if ( uiCommands.scene.saveSceneDefaults )
        {
            SaveCurrentSceneDefaults();
        }
        if ( uiCommands.scene.requestedSceneIndex >= 0 )
        {
            LoadSceneFromBrowserIndex( uiCommands.scene.requestedSceneIndex );
        }

        RunUIStressActions();

        const bool editorViewportLookNow = m_editor.fixedPlacementEnabled && Input::IsRightMouseDown() && !m_UI.BlocksCameraMouse();
        if ( editorViewportLookNow != m_editor.viewportLookActive )
        {
            InputController::ResetMouseLook( m_camera );
        }
        m_editor.viewportLookActive = editorViewportLookNow;
        ApplyCursorOwnership();
    }

    UpdateEditorInteractionPreview();

    // Editor placement and nudge-fire both use world clicks. UI hover/capture
    // suppresses both so panel interaction never spawns bodies or bullets.
    {
        const bool leftMouseNow = Input::IsLeftMouseDown();
        const bool leftMouseWasDown = m_camera.input.Get( InputState::LeftMouseWasDown );
        const bool leftPressed = leftMouseNow && !leftMouseWasDown;
        const bool leftReleased = !leftMouseNow && leftMouseWasDown;
        bool consumedWorldClick = false;

        if ( m_editor.gizmoDragActive )
        {
            consumedWorldClick = true;
            if ( leftMouseNow && !suppressNudgeFireThisFrame )
            {
                Vector3 rayOrigin;
                Vector3 rayDirection;
                if ( TryBuildMouseWorldRay( rayOrigin, rayDirection ) )
                {
                    MoveSelectedEditorObjectAlongAxis( rayOrigin, rayDirection );
                }
            }
            if ( leftReleased || suppressNudgeFireThisFrame )
            {
                m_editor.gizmoDragActive = false;
                m_editor.activeGizmoAxis = -1;
            }
        }

        if ( !consumedWorldClick && leftPressed && !suppressNudgeFireThisFrame )
        {
            if ( !m_camera.isNudgeMode &&
                 !m_editor.fixedPlacementEnabled &&
                 m_editor.selectedModelIndex >= 0 &&
                 m_editor.hotGizmoAxis >= 0 )
            {
                Vector3 rayOrigin;
                Vector3 rayDirection;
                float axisT = 0.0f;
                if ( TryBuildMouseWorldRay( rayOrigin, rayDirection ) &&
                     TryEditorAxisRayParameter( m_editor.hotGizmoAxis, rayOrigin, rayDirection, axisT ) )
                {
                    EnterInteractiveSceneRun();
                    m_editor.gizmoDragActive = true;
                    m_editor.activeGizmoAxis = m_editor.hotGizmoAxis;
                    m_editor.gizmoDragStartAxisT = axisT;
                    m_editor.gizmoDragStartPosition = m_cGameModelCollection.Models()[static_cast<size_t>( m_editor.selectedModelIndex )].GetPosition();
                    consumedWorldClick = true;
                }
            }

            if ( !consumedWorldClick && m_editor.fixedPlacementEnabled )
            {
                if ( m_editor.placementPreviewVisible )
                {
                    const int previousModelCount = m_cGameModelCollection.GetModelCount();
                    PlaceFixedObjectAtTerrainPoint( m_editor.fixedObjectType, m_editor.placementTerrainPoint );
                    if ( m_cGameModelCollection.GetModelCount() > previousModelCount )
                    {
                        m_editor.selectedModelIndex = m_cGameModelCollection.GetModelCount() - 1;
                    }
                }
                consumedWorldClick = true;
            }

            if ( !consumedWorldClick && !m_camera.isNudgeMode )
            {
                Vector3 rayOrigin;
                Vector3 rayDirection;
                int pickedIndex = -1;
                if ( TryBuildMouseWorldRay( rayOrigin, rayDirection ) &&
                     TryPickEditorModel( rayOrigin, rayDirection, pickedIndex ) )
                {
                    m_editor.selectedModelIndex = pickedIndex;
                }
                else
                {
                    m_editor.selectedModelIndex = -1;
                }
                consumedWorldClick = true;
            }
        }

        if ( !consumedWorldClick &&
             m_camera.isNudgeMode &&
             leftPressed &&
             !suppressNudgeFireThisFrame )
        {
            FireProjectile();
        }
        m_camera.input.Set( InputState::LeftMouseWasDown, leftMouseNow );
    }

    if ( m_UI.BlocksKeyboard() )
    {
        InputController::ResetMouseLook( m_camera );
        m_camera.input.Set( InputState::Up, false );
        m_camera.input.Set( InputState::Down, false );
        m_camera.input.Set( InputState::Left, false );
        m_camera.input.Set( InputState::Right, false );
        ApplyCursorOwnership();
        return;
    }

    // F2: Save scene snapshot to Scenes/
    {
        const RuntimeKeyEdge f2Edge = InputController::CaptureKeyEdge( m_camera.input, InputState::F2WasDown, VK_F2 );
        if ( f2Edge.wasPressed )
        {
            CreateDirectoryA( "Scenes", nullptr );
            static int sSnapshotSeq = 0;
            bool saved = false;
            for ( int tries = 0; tries < 100 && !saved; ++tries )
            {
                char path[256];
                sprintf_s( path, sizeof( path ), "Scenes\\snapshot_%04d.scene", sSnapshotSeq++ );
                saved = m_cGameModelCollection.SaveSceneSnapshot(
                    path,
                    SceneState().isScenePhysics,
                    SceneState().isSceneText,
                    m_cWorldEnvironment,
                    m_systems.cameras->GetCameraTranslation(),
                    m_systems.cameras->GetCameraView(),
                    m_systems.cameras->GetCameraUp() );
            }
        }
    }

    // F3: Save screenshot to Screenshots/
    {
        const RuntimeKeyEdge f3Edge = InputController::CaptureKeyEdge( m_camera.input, InputState::F3WasDown, VK_F3 );
        if ( f3Edge.wasPressed )
        {
            CreateDirectoryA( "Screenshots", nullptr );
            static int sScreenshotSeq = 0;
            bool saved = false;
            for ( int tries = 0; tries < 100 && !saved; ++tries )
            {
                char path[256];
                sprintf_s( path, sizeof( path ), "Screenshots\\screenshot_%04d.bmp", sScreenshotSeq++ );
                if ( GetFileAttributesA( path ) == INVALID_FILE_ATTRIBUTES )
                {
                    SaveScreenshot( path );
                    saved = true;
                }
            }
        }
    }

    // R: reset/reload the current scene from scratch. Backspace remains as a scene-mode alias.
    {
        const RuntimeKeyEdge rEdge = InputController::CaptureKeyEdge( m_camera.input, InputState::RKeyWasDown, 'R' );
        if ( rEdge.wasPressed )
        {
            EnterInteractiveSceneRun();
            ResetCurrentScene( true, true );
        }
    }
    if ( SceneState().isSceneMode )
    {
        const RuntimeKeyEdge backspaceEdge = InputController::CaptureKeyEdge( m_camera.input, InputState::BackspaceWasDown, VK_BACK );
        if ( backspaceEdge.wasPressed )
        {
            EnterInteractiveSceneRun();
            ResetCurrentScene( true, true );
        }
    }

    const bool viewportCameraControlsActive = m_camera.isFlyMode || m_editor.viewportLookActive;
    if ( viewportCameraControlsActive )
    {
        // Diagnostics UI owns the native cursor; mouse-look hides it while
        // consuming raw Win32 deltas, with cursor-position deltas as a
        // remote-desktop friendly fallback when raw input is unavailable.
        if ( !Input::IsAppFocused() )
        {
            InputController::ResetMouseLook( m_camera );
        }
        else if ( !CameraMouseOwnsCursor() )
        {
            Input::SetSystemCursorVisible( true );
            InputController::ResetMouseLook( m_camera );
        }
        else
        {
            Input::SetSystemCursorVisible( false );
            long rawX = 0;
            long rawY = 0;
            const bool hasRawDelta = Input::ConsumeRawMouseDelta( rawX, rawY );
            POINT currentClient = Input::GetClientMouseCoordinates();

            if ( m_camera.needsMouseLookReset )
            {
                m_camera.input.xMove = 0;
                m_camera.input.yMove = 0;
                m_camera.mouseLookLastClient = currentClient;
                m_camera.hasMouseLookLastClient = true;
                m_camera.needsMouseLookReset = false;
            }
            else if ( hasRawDelta )
            {
                InputController::SetMouseLookDelta( m_camera, rawX, rawY );
                m_camera.mouseLookLastClient = currentClient;
                m_camera.hasMouseLookLastClient = true;
            }
            else if ( !m_camera.hasMouseLookLastClient )
            {
                m_camera.input.xMove = 0;
                m_camera.input.yMove = 0;
                m_camera.mouseLookLastClient = currentClient;
                m_camera.hasMouseLookLastClient = true;
            }
            else
            {
                InputController::SetMouseLookDelta( m_camera,
                                                    currentClient.x - m_camera.mouseLookLastClient.x,
                                                    currentClient.y - m_camera.mouseLookLastClient.y );
                m_camera.mouseLookLastClient = currentClient;
            }
        }

        // WASD movement
        m_camera.input.Set( InputState::Up, Input::IsKeyDown( 'W' ) );
        m_camera.input.Set( InputState::Left, Input::IsKeyDown( 'A' ) );
        m_camera.input.Set( InputState::Down, Input::IsKeyDown( 'S' ) );
        m_camera.input.Set( InputState::Right, Input::IsKeyDown( 'D' ) );
    }
    else
    {
        InputController::ResetMouseLook( m_camera );
        m_camera.input.Set( InputState::Up, false );
        m_camera.input.Set( InputState::Down, false );
        m_camera.input.Set( InputState::Left, false );
        m_camera.input.Set( InputState::Right, false );
    }
}


void SkullbonezRun::MoveCamera( float keyMovementQty, float mouseMovementQty )
{
    if ( m_camera.isFlyMode || m_editor.viewportLookActive )
    {
        // Shift held = 3x speed
        float speedMult = Input::IsKeyDown( VK_SHIFT ) ? 3.0f : 1.0f;

        // Mouse look
        if ( m_camera.input.xMove != 0 || m_camera.input.yMove != 0 )
        {
            m_systems.cameras->RotatePrimary( m_camera.input.xMove * mouseMovementQty,
                                              m_camera.input.yMove * mouseMovementQty );
        }

        // WASD movement
        if ( m_camera.input.Get( InputState::Up ) )
        {
            m_systems.cameras->MovePrimary( Camera::TravelDirection::Forward, keyMovementQty * speedMult );
        }
        if ( m_camera.input.Get( InputState::Left ) )
        {
            m_systems.cameras->MovePrimary( Camera::TravelDirection::Left, keyMovementQty * speedMult );
        }
        if ( m_camera.input.Get( InputState::Down ) )
        {
            m_systems.cameras->MovePrimary( Camera::TravelDirection::Backward, keyMovementQty * speedMult );
        }
        if ( m_camera.input.Get( InputState::Right ) )
        {
            m_systems.cameras->MovePrimary( Camera::TravelDirection::Right, keyMovementQty * speedMult );
        }

        m_systems.cameras->ApplyPrimaryMovementBuffer();
    }

    // Clamp camera Y between m_terrain surface and Cfg().maxCameraHeight (not in fly mode, not in scene mode)
    if ( !m_camera.isFlyMode && !m_editor.viewportLookActive && !SceneState().isSceneMode )
    {
        Vector3 translatedCameraPosition = m_systems.cameras->GetCameraTranslation();
        float minY = m_systems.terrain->GetTerrainHeightAt( translatedCameraPosition.x, translatedCameraPosition.z, true ) + Cfg().minCameraHeight;
        if ( minY > translatedCameraPosition.y )
        {
            m_systems.cameras->AmmendPrimaryY( minY );
        }
        else if ( translatedCameraPosition.y > Cfg().maxCameraHeight )
        {
            m_systems.cameras->AmmendPrimaryY( Cfg().maxCameraHeight );
        }
    }
}


void SkullbonezRun::ResetProjectilePool()
{
    m_fire.bulletIndices.fill( -1 );
    m_fire.bulletNext = 0;
    m_fire.bulletPoolReady = false;
}


bool SkullbonezRun::EnsureProjectilePool()
{
    if ( m_fire.bulletPoolReady )
    {
        return true;
    }

    if ( m_cGameModelCollection.GetModelCount() > ActiveGameModelCapacity() - RUNTIME_PROJECTILE_POOL_SIZE )
    {
        return false;
    }

    m_fire.bulletIndices.fill( -1 );
    for ( int i = 0; i < RUNTIME_PROJECTILE_POOL_SIZE; ++i )
    {
        const float parkOffset = static_cast<float>( i ) * ( CAMERA_PROJECTILE_RADIUS * 4.0f );
        GameModel bullet( &m_cWorldEnvironment,
                          Vector3( CAMERA_PROJECTILE_PARK_BASE, CAMERA_PROJECTILE_PARK_BASE - parkOffset, CAMERA_PROJECTILE_PARK_BASE ),
                          Vector3( CAMERA_PROJECTILE_MOMENT, CAMERA_PROJECTILE_MOMENT, CAMERA_PROJECTILE_MOMENT ),
                          CAMERA_PROJECTILE_MASS );
        bullet.SetTerrain( m_systems.terrain.get() );
        bullet.SetCoefficientRestitution( CAMERA_PROJECTILE_RESTITUTION );
        bullet.AddBoundingSphere( CAMERA_PROJECTILE_RADIUS );
        bullet.SetRenderTint( CAMERA_PROJECTILE_SILVER_R, CAMERA_PROJECTILE_SILVER_G, CAMERA_PROJECTILE_SILVER_B, 1.0f );
        bullet.SetFixed( true );

        char name[64];
        sprintf_s( name, sizeof( name ), "silver_bullet_%02d", i );
        bullet.SetName( name );

        const int bulletIndex = m_cGameModelCollection.GetModelCount();
        m_cGameModelCollection.AddGameModel( std::move( bullet ) );
        m_fire.bulletIndices[i] = bulletIndex;
    }

    m_fire.bulletNext = 0;
    m_fire.bulletPoolReady = true;
    return true;
}


void SkullbonezRun::FireProjectile()
{
    if ( !EnsureProjectilePool() )
    {
        return;
    }

    const int slot = m_fire.bulletNext;
    m_fire.bulletNext = ( m_fire.bulletNext + 1 ) % RUNTIME_PROJECTILE_POOL_SIZE;

    const int found = m_fire.bulletIndices[slot];
    if ( found < 0 || found >= m_cGameModelCollection.GetModelCount() )
    {
        ResetProjectilePool();
        return;
    }

    GameModel& model = m_cGameModelCollection.GetModelAtIndex( found );

    const Vector3& camPos = m_systems.cameras->GetCameraTranslation();
    const Vector3& camView = m_systems.cameras->GetCameraView();
    Vector3 forward = camView - camPos;
    const float lenSq = forward * forward;
    if ( lenSq < 1e-8f )
    {
        return;
    }
    forward = forward * ( 1.0f / sqrtf( lenSq ) );

    const Vector3 spawnPos = camPos + forward * CAMERA_PROJECTILE_SPAWN_CLEARANCE;

    const float speedMult = Input::IsKeyDown( VK_SHIFT ) ? CAMERA_PROJECTILE_SHIFT_MULTIPLIER : 1.0f;
    const float fireSpeed = CAMERA_PROJECTILE_SPEED * speedMult;

    model.SetFixed( false );
    m_cGameModelCollection.WakeModel( found );
    model.SetPosition( spawnPos );
    model.SetLinearVelocity( forward * fireSpeed );
    model.SetAngularVelocity( Vector3( 0.0f, 0.0f, 0.0f ) );
    model.SetRenderTint( CAMERA_PROJECTILE_SILVER_R, CAMERA_PROJECTILE_SILVER_G, CAMERA_PROJECTILE_SILVER_B, 1.0f );
}


void SkullbonezRun::SpawnPhysicsObjectFromCamera( int spawnType )
{
    const int modelCount = m_cGameModelCollection.GetModelCount();
    if ( modelCount >= ActiveGameModelCapacity() )
    {
        fprintf( stderr, "[ui] Cannot spawn physics object: model capacity reached.\n" );
        return;
    }

    const Vector3 camPos = m_systems.cameras->GetCameraTranslation();
    Vector3 forward = m_systems.cameras->GetCameraView() - camPos;
    const float forwardLenSq = SkullbonezCore::Math::Vector::VectorMagSquared( forward );
    if ( forwardLenSq > TOLERANCE * TOLERANCE )
    {
        forward = forward * ( 1.0f / sqrtf( forwardLenSq ) );
    }
    else
    {
        forward = Vector3( 0.0f, 0.0f, 1.0f );
    }

    const Vector3 spawnPos = camPos + forward * 28.0f;
    const int clampedSpawnType = std::clamp( spawnType, 0, UI::PhysicsTab::SPAWN_TYPE_COUNT - 1 );

    auto addSphere = [&]( const char* baseName, float radius, float mass, float restitution, float tintR, float tintG, float tintB )
    {
        const float moment = 0.4f * mass * radius * radius;
        GameModel model( &m_cWorldEnvironment,
                         spawnPos,
                         Vector3( moment, moment, moment ),
                         mass );
        model.SetTerrain( m_systems.terrain.get() );
        model.SetCoefficientRestitution( restitution );
        model.AddBoundingSphere( radius );
        model.SetLinearVelocity( forward * 8.0f );
        model.SetRenderTint( tintR, tintG, tintB, 1.0f );
        char name[64];
        sprintf_s( name, sizeof( name ), "%s_%03d", baseName, modelCount );
        model.SetName( name );
        const int index = m_cGameModelCollection.GetModelCount();
        m_cGameModelCollection.AddGameModel( std::move( model ) );
        m_cGameModelCollection.WakeModel( index );
    };

    auto addHull = [&]( const char* label, const char* path, float tintR, float tintG, float tintB )
    {
        constexpr float hullMass = 24.0f;
        const ConvexHullShape hull = ConvexHullShape::LoadFromFile( path );
        GameModel model( &m_cWorldEnvironment,
                         spawnPos,
                         hull.ComputeBoxApproxInertia( hullMass ),
                         hullMass );
        model.SetTerrain( m_systems.terrain.get() );
        model.SetCoefficientRestitution( 0.30f );
        model.AddConvexHull( hull );
        model.SetLinearVelocity( forward * 6.0f );
        model.SetRenderTint( tintR, tintG, tintB, 1.0f );
        char name[64];
        sprintf_s( name, sizeof( name ), "spawn_%s_%03d", label, modelCount );
        model.SetName( name );
        const int index = m_cGameModelCollection.GetModelCount();
        m_cGameModelCollection.AddGameModel( std::move( model ) );
        m_cGameModelCollection.WakeModel( index );
    };

    switch ( clampedSpawnType )
    {
    case UI::PhysicsTab::SPAWN_BALL:
        addSphere( "spawn_ball", 4.0f, 6.0f, 0.45f, 0.35f, 0.75f, 1.0f );
        break;
    case UI::PhysicsTab::SPAWN_SPHERE:
        addSphere( "spawn_sphere", 8.0f, 24.0f, 0.35f, 0.95f, 0.92f, 0.82f );
        break;
    case UI::PhysicsTab::SPAWN_HULL_WEDGE:
        addHull( "wedge", "SkullbonezData/hulls/wedge.hull", 0.92f, 0.65f, 0.30f );
        break;
    case UI::PhysicsTab::SPAWN_HULL_TRI_PRISM:
        addHull( "tri_prism", "SkullbonezData/hulls/tri_prism.hull", 0.45f, 0.95f, 0.62f );
        break;
    case UI::PhysicsTab::SPAWN_HULL_TAPERED_BLOCK:
        addHull( "tapered", "SkullbonezData/hulls/tapered_block.hull", 0.95f, 0.52f, 0.76f );
        break;
    case UI::PhysicsTab::SPAWN_HULL_PYRAMID:
        addHull( "pyramid", "SkullbonezData/hulls/pyramid.hull", 0.78f, 0.62f, 1.0f );
        break;
    case UI::PhysicsTab::SPAWN_HULL_HEX_PRISM:
        addHull( "hex_prism", "SkullbonezData/hulls/hex_prism.hull", 0.35f, 0.95f, 0.90f );
        break;
    case UI::PhysicsTab::SPAWN_HULL_DIAMOND:
        addHull( "diamond", "SkullbonezData/hulls/diamond.hull", 1.0f, 0.86f, 0.40f );
        break;
    default:
        break;
    }

    SceneState().modelCount = m_cGameModelCollection.GetModelCount();
}


bool SkullbonezRun::TryBuildMouseWorldRay( Vector3& outOrigin, Vector3& outDirection ) const
{
    if ( !m_systems.window || !m_systems.cameras )
    {
        return false;
    }

    const POINT mouse = Input::GetClientMouseCoordinates();
    const int screenW = (std::max)( 1, static_cast<int>( m_systems.window->m_sWindowDimensions.x ) );
    const int screenH = (std::max)( 1, static_cast<int>( m_systems.window->m_sWindowDimensions.y ) );
    if ( mouse.x < 0 || mouse.y < 0 || mouse.x >= screenW || mouse.y >= screenH )
    {
        return false;
    }

    const float ndcX = ( static_cast<float>( mouse.x ) / static_cast<float>( screenW ) ) * 2.0f - 1.0f;
    const float ndcY = 1.0f - ( static_cast<float>( mouse.y ) / static_cast<float>( screenH ) ) * 2.0f;

    const Vector3 eye = m_systems.cameras->GetCameraTranslation();
    const Vector3 view = m_systems.cameras->GetCameraView();
    const Vector3 up = m_systems.cameras->GetCameraUp();
    const Matrix4 viewMatrix = Matrix4::LookAt( eye, view, up );
    const Matrix4 inverseViewProjection = ( m_systems.window->GetProjectionMatrix() * viewMatrix ).Inverse();

    Vector3 rayNear;
    Vector3 rayFar;
    if ( !TransformClipPointToWorld( inverseViewProjection, ndcX, ndcY, 0.0f, rayNear ) ||
         !TransformClipPointToWorld( inverseViewProjection, ndcX, ndcY, 1.0f, rayFar ) )
    {
        return false;
    }

    Vector3 rayDirection = rayFar - rayNear;
    const float dirLenSq = VectorMagSquared( rayDirection );
    if ( dirLenSq <= TOLERANCE * TOLERANCE )
    {
        return false;
    }
    outOrigin = rayNear;
    outDirection = rayDirection * ( 1.0f / sqrtf( dirLenSq ) );
    return true;
}


bool SkullbonezRun::TryGetMouseTerrainPlacement( Vector3& outPosition ) const
{
    return TryGetMouseTerrainPlacement( outPosition, nullptr, nullptr );
}


bool SkullbonezRun::TryGetMouseTerrainPlacement( Vector3& outPosition, Vector3* outRayOrigin, Vector3* outRayDirection ) const
{
    if ( !m_systems.terrain )
    {
        return false;
    }

    Vector3 rayNear;
    Vector3 rayDirection;
    if ( !TryBuildMouseWorldRay( rayNear, rayDirection ) )
    {
        return false;
    }
    if ( outRayOrigin )
    {
        *outRayOrigin = rayNear;
    }
    if ( outRayDirection )
    {
        *outRayDirection = rayDirection;
    }

    constexpr float MAX_RAY_DISTANCE = 5000.0f;
    constexpr int RAY_STEPS = 192;
    bool hasPrevious = false;
    float previousT = 0.0f;
    float previousDiff = 0.0f;

    for ( int step = 0; step <= RAY_STEPS; ++step )
    {
        const float t = MAX_RAY_DISTANCE * static_cast<float>( step ) / static_cast<float>( RAY_STEPS );
        const Vector3 sample = rayNear + rayDirection * t;
        if ( !m_systems.terrain->IsInBounds( sample.x, sample.z ) )
        {
            continue;
        }

        const float terrainY = m_systems.terrain->GetTerrainHeightAt( sample.x, sample.z );
        const float diff = sample.y - terrainY;
        if ( fabsf( diff ) <= 0.01f )
        {
            outPosition = Vector3( sample.x, terrainY, sample.z );
            return true;
        }

        if ( hasPrevious && previousDiff > 0.0f && diff <= 0.0f )
        {
            float lowT = previousT;
            float highT = t;
            Vector3 hit = sample;
            float hitY = terrainY;
            for ( int refine = 0; refine < 12; ++refine )
            {
                const float midT = ( lowT + highT ) * 0.5f;
                const Vector3 mid = rayNear + rayDirection * midT;
                if ( !m_systems.terrain->IsInBounds( mid.x, mid.z ) )
                {
                    lowT = midT;
                    continue;
                }
                const float midTerrainY = m_systems.terrain->GetTerrainHeightAt( mid.x, mid.z );
                const float midDiff = mid.y - midTerrainY;
                hit = mid;
                hitY = midTerrainY;
                if ( midDiff > 0.0f )
                {
                    lowT = midT;
                }
                else
                {
                    highT = midT;
                }
            }
            outPosition = Vector3( hit.x, hitY, hit.z );
            return true;
        }

        hasPrevious = true;
        previousT = t;
        previousDiff = diff;
    }

    return false;
}


bool SkullbonezRun::TryComputeEditorObjectCenter( int fixedObjectType, const Vector3& terrainPoint, Vector3& outCenter ) const
{
    const int type = std::clamp( fixedObjectType, 0, UI::EditorTab::FIXED_TYPE_COUNT - 1 );
    switch ( type )
    {
    case UI::EditorTab::FIXED_BOX:
        outCenter = Vector3( terrainPoint.x, terrainPoint.y + 6.0f + EDITOR_PLACEMENT_SURFACE_EPSILON, terrainPoint.z );
        return true;
    case UI::EditorTab::FIXED_BALL:
        outCenter = Vector3( terrainPoint.x, terrainPoint.y + 4.0f + EDITOR_PLACEMENT_SURFACE_EPSILON, terrainPoint.z );
        return true;
    case UI::EditorTab::FIXED_SPHERE:
        outCenter = Vector3( terrainPoint.x, terrainPoint.y + 8.0f + EDITOR_PLACEMENT_SURFACE_EPSILON, terrainPoint.z );
        return true;
    default:
    {
        const ConvexHullShape* hull = CachedFixedHullForType( type );
        if ( !hull )
        {
            return false;
        }
        outCenter = Vector3( terrainPoint.x, terrainPoint.y + HullBottomOffset( *hull ) + EDITOR_PLACEMENT_SURFACE_EPSILON, terrainPoint.z );
        return true;
    }
    }
}


bool SkullbonezRun::TryComputeEditorPlacementPreview( int fixedObjectType )
{
    Vector3 terrainPoint;
    Vector3 rayOrigin;
    Vector3 rayDirection;
    if ( !TryGetMouseTerrainPlacement( terrainPoint, &rayOrigin, &rayDirection ) )
    {
        return false;
    }

    if ( m_systems.terrain && EDITOR_PLACEMENT_SNAP > 0.0f )
    {
        const float snappedX = roundf( terrainPoint.x / EDITOR_PLACEMENT_SNAP ) * EDITOR_PLACEMENT_SNAP;
        const float snappedZ = roundf( terrainPoint.z / EDITOR_PLACEMENT_SNAP ) * EDITOR_PLACEMENT_SNAP;
        if ( m_systems.terrain->IsInBounds( snappedX, snappedZ ) )
        {
            terrainPoint.x = snappedX;
            terrainPoint.z = snappedZ;
            terrainPoint.y = m_systems.terrain->GetTerrainHeightAt( snappedX, snappedZ );
        }
    }

    Vector3 center;
    if ( !TryComputeEditorObjectCenter( fixedObjectType, terrainPoint, center ) )
    {
        return false;
    }

    m_editor.placementTerrainPoint = terrainPoint;
    m_editor.placementCenter = center;
    m_editor.placementRayOrigin = rayOrigin;
    m_editor.placementRayHit = terrainPoint;
    return true;
}


bool SkullbonezRun::TryPickEditorModel( const Vector3& rayOrigin, const Vector3& rayDirection, int& outIndex ) const
{
    outIndex = -1;
    float bestT = FLT_MAX;
    const std::vector<GameModel>& models = m_cGameModelCollection.Models();
    for ( int i = 0; i < static_cast<int>( models.size() ); ++i )
    {
        const GameModel& model = models[i];
        const float radius = EditorModelRadius( model ) + 1.0f;
        const Vector3 toCenter = model.GetPosition() - rayOrigin;
        const float rayT = toCenter * rayDirection;
        if ( rayT < 0.0f || rayT >= bestT )
        {
            continue;
        }

        const Vector3 closest = rayOrigin + rayDirection * rayT;
        if ( VectorMagSquared( model.GetPosition() - closest ) <= radius * radius )
        {
            bestT = rayT;
            outIndex = i;
        }
    }
    return outIndex >= 0;
}


int SkullbonezRun::HitEditorGizmoAxis( const Vector3& rayOrigin, const Vector3& rayDirection ) const
{
    if ( m_editor.selectedModelIndex < 0 || m_editor.selectedModelIndex >= m_cGameModelCollection.GetModelCount() )
    {
        return -1;
    }

    const std::vector<GameModel>& models = m_cGameModelCollection.Models();
    const GameModel& model = models[static_cast<size_t>( m_editor.selectedModelIndex )];
    const Vector3 origin = model.GetPosition();
    const float radius = EditorModelRadius( model );
    const float length = EditorGizmoAxisLength( radius );
    const float threshold = (std::max)( 1.25f, length * 0.06f );
    const float thresholdSq = threshold * threshold;

    int bestAxis = -1;
    float bestDistanceSq = FLT_MAX;
    for ( int axis = 0; axis < 3; ++axis )
    {
        const Vector3 axisVector = EditorAxisVector( axis );
        const float distanceSq = DistanceRayToSegmentSquared( rayOrigin, rayDirection, origin, origin + axisVector * length );
        if ( distanceSq <= thresholdSq && distanceSq < bestDistanceSq )
        {
            bestDistanceSq = distanceSq;
            bestAxis = axis;
        }
    }
    return bestAxis;
}


bool SkullbonezRun::TryEditorAxisRayParameter( int axis, const Vector3& rayOrigin, const Vector3& rayDirection, float& outAxisT ) const
{
    if ( axis < 0 || axis > 2 || m_editor.selectedModelIndex < 0 || m_editor.selectedModelIndex >= m_cGameModelCollection.GetModelCount() )
    {
        return false;
    }

    const Vector3 axisOrigin = m_cGameModelCollection.Models()[static_cast<size_t>( m_editor.selectedModelIndex )].GetPosition();
    const Vector3 axisVector = EditorAxisVector( axis );
    const Vector3 w = axisOrigin - rayOrigin;
    const float b = axisVector * rayDirection;
    const float d = axisVector * w;
    const float e = rayDirection * w;
    const float denom = 1.0f - b * b;
    if ( fabsf( denom ) <= 1e-5f )
    {
        return false;
    }

    outAxisT = ( b * e - d ) / denom;
    return true;
}


void SkullbonezRun::MoveSelectedEditorObjectAlongAxis( const Vector3& rayOrigin, const Vector3& rayDirection )
{
    if ( !m_editor.gizmoDragActive || m_editor.activeGizmoAxis < 0 )
    {
        return;
    }

    float axisT = 0.0f;
    if ( !TryEditorAxisRayParameter( m_editor.activeGizmoAxis, rayOrigin, rayDirection, axisT ) )
    {
        return;
    }

    const int index = m_editor.selectedModelIndex;
    if ( index < 0 || index >= m_cGameModelCollection.GetModelCount() )
    {
        m_editor.gizmoDragActive = false;
        m_editor.activeGizmoAxis = -1;
        return;
    }

    GameModel& model = m_cGameModelCollection.GetModelAtIndex( index );
    const Vector3 axisVector = EditorAxisVector( m_editor.activeGizmoAxis );
    const Vector3 newPosition = m_editor.gizmoDragStartPosition + axisVector * ( axisT - m_editor.gizmoDragStartAxisT );
    model.SetPosition( newPosition );
    model.SetLinearVelocity( Vector3( 0.0f, 0.0f, 0.0f ) );
    model.SetAngularVelocity( Vector3( 0.0f, 0.0f, 0.0f ) );
    if ( !model.IsFixed() )
    {
        m_cGameModelCollection.WakeModel( index );
    }
}


void SkullbonezRun::UpdateEditorInteractionPreview()
{
    m_editor.placementPreviewVisible = false;
    m_editor.hotGizmoAxis = -1;

    if ( m_UI.BlocksCameraMouse() || m_editor.viewportLookActive )
    {
        return;
    }

    if ( m_editor.fixedPlacementEnabled )
    {
        m_editor.placementPreviewVisible = TryComputeEditorPlacementPreview( m_editor.fixedObjectType );
        return;
    }

    if ( m_editor.selectedModelIndex >= m_cGameModelCollection.GetModelCount() )
    {
        m_editor.selectedModelIndex = -1;
        m_editor.gizmoDragActive = false;
        m_editor.activeGizmoAxis = -1;
    }

    if ( m_editor.selectedModelIndex >= 0 && !m_editor.gizmoDragActive )
    {
        Vector3 rayOrigin;
        Vector3 rayDirection;
        if ( TryBuildMouseWorldRay( rayOrigin, rayDirection ) )
        {
            m_editor.hotGizmoAxis = HitEditorGizmoAxis( rayOrigin, rayDirection );
        }
    }
}


void SkullbonezRun::RenderEditorOverlay( const Matrix4& viewProjection )
{
    m_editorTracer.Clear();
    if ( m_editor.placementPreviewVisible )
    {
        m_editorTracer.AddPlacementRay( m_editor.placementRayOrigin, m_editor.placementRayHit );
        m_editorTracer.AddPlacementGhost( m_editor.fixedObjectType, m_editor.placementCenter );
    }

    if ( !m_editor.fixedPlacementEnabled &&
         m_editor.selectedModelIndex >= 0 &&
         m_editor.selectedModelIndex < m_cGameModelCollection.GetModelCount() )
    {
        const GameModel& selected = m_cGameModelCollection.Models()[static_cast<size_t>( m_editor.selectedModelIndex )];
        const float radius = EditorModelRadius( selected );
        m_editorTracer.AddSelectionOutline( selected );
        m_editorTracer.AddGizmo( selected.GetPosition(), radius, m_editor.hotGizmoAxis, m_editor.activeGizmoAxis );
    }
    m_editorTracer.Render( viewProjection );
}


void SkullbonezRun::PlaceFixedObjectAtMouse( int fixedObjectType )
{
    Vector3 terrainPoint;
    if ( !TryGetMouseTerrainPlacement( terrainPoint ) )
    {
        return;
    }

    PlaceFixedObjectAtTerrainPoint( fixedObjectType, terrainPoint );
}


void SkullbonezRun::PlaceFixedObjectAtTerrainPoint( int fixedObjectType, const Vector3& terrainPoint )
{
    const int modelCount = m_cGameModelCollection.GetModelCount();
    if ( modelCount >= ActiveGameModelCapacity() )
    {
        fprintf( stderr, "[editor] Cannot place fixed object: model capacity reached.\n" );
        return;
    }

    EnterInteractiveSceneRun();
    const int type = std::clamp( fixedObjectType, 0, UI::EditorTab::FIXED_TYPE_COUNT - 1 );
    const int serial = m_editor.placedObjectSerial++;

    auto addSphere = [&]( const char* baseName, float radius, float mass, float restitution, float tintR, float tintG, float tintB )
    {
        const float moment = 0.4f * mass * radius * radius;
        const Vector3 center( terrainPoint.x, terrainPoint.y + radius + EDITOR_PLACEMENT_SURFACE_EPSILON, terrainPoint.z );
        GameModel model( &m_cWorldEnvironment,
                         center,
                         Vector3( moment, moment, moment ),
                         mass );
        model.SetTerrain( m_systems.terrain.get() );
        model.SetCoefficientRestitution( restitution );
        model.AddBoundingSphere( radius );
        model.SetRenderTint( tintR, tintG, tintB, 1.0f );
        model.SetFixed( true );
        char name[64];
        sprintf_s( name, sizeof( name ), "%s_%03d", baseName, serial );
        model.SetName( name );
        m_cGameModelCollection.AddGameModel( std::move( model ) );
    };

    auto addBox = [&]()
    {
        const Vector3 halfExtents( 6.0f, 6.0f, 6.0f );
        constexpr float mass = 500.0f;
        const Vector3 center( terrainPoint.x, terrainPoint.y + halfExtents.y + EDITOR_PLACEMENT_SURFACE_EPSILON, terrainPoint.z );
        GameModel model( &m_cWorldEnvironment,
                         center,
                         BoxInertiaForHalfExtents( halfExtents, mass ),
                         mass );
        model.SetTerrain( m_systems.terrain.get() );
        model.SetCoefficientRestitution( 0.25f );
        model.AddBoundingBox( halfExtents );
        model.SetRenderTint( 0.75f, 0.86f, 0.95f, 1.0f );
        model.SetFixed( true );
        char name[64];
        sprintf_s( name, sizeof( name ), "fixed_box_%03d", serial );
        model.SetName( name );
        m_cGameModelCollection.AddGameModel( std::move( model ) );
    };

    auto addHull = [&]( const char* label, const char* path, float tintR, float tintG, float tintB )
    {
        constexpr float mass = 500.0f;
        const ConvexHullShape hull = ConvexHullShape::LoadFromFile( path );
        const Vector3 center( terrainPoint.x, terrainPoint.y + HullBottomOffset( hull ) + EDITOR_PLACEMENT_SURFACE_EPSILON, terrainPoint.z );
        GameModel model( &m_cWorldEnvironment,
                         center,
                         hull.ComputeBoxApproxInertia( mass ),
                         mass );
        model.SetTerrain( m_systems.terrain.get() );
        model.SetCoefficientRestitution( 0.25f );
        model.AddConvexHull( hull );
        model.SetRenderTint( tintR, tintG, tintB, 1.0f );
        model.SetFixed( true );
        char name[64];
        sprintf_s( name, sizeof( name ), "fixed_%s_%03d", label, serial );
        model.SetName( name );
        m_cGameModelCollection.AddGameModel( std::move( model ) );
    };

    switch ( type )
    {
    case UI::EditorTab::FIXED_BOX:
        addBox();
        break;
    case UI::EditorTab::FIXED_BALL:
        addSphere( "fixed_ball", 4.0f, 120.0f, 0.30f, 0.35f, 0.75f, 1.0f );
        break;
    case UI::EditorTab::FIXED_SPHERE:
        addSphere( "fixed_sphere", 8.0f, 500.0f, 0.25f, 0.95f, 0.92f, 0.82f );
        break;
    case UI::EditorTab::FIXED_HULL_WEDGE:
        addHull( "wedge", "SkullbonezData/hulls/wedge.hull", 0.92f, 0.65f, 0.30f );
        break;
    case UI::EditorTab::FIXED_HULL_TRI_PRISM:
        addHull( "tri_prism", "SkullbonezData/hulls/tri_prism.hull", 0.45f, 0.95f, 0.62f );
        break;
    case UI::EditorTab::FIXED_HULL_TAPERED_BLOCK:
        addHull( "tapered", "SkullbonezData/hulls/tapered_block.hull", 0.95f, 0.52f, 0.76f );
        break;
    case UI::EditorTab::FIXED_HULL_PYRAMID:
        addHull( "pyramid", "SkullbonezData/hulls/pyramid.hull", 0.78f, 0.62f, 1.0f );
        break;
    case UI::EditorTab::FIXED_HULL_HEX_PRISM:
        addHull( "hex_prism", "SkullbonezData/hulls/hex_prism.hull", 0.35f, 0.95f, 0.90f );
        break;
    case UI::EditorTab::FIXED_HULL_DIAMOND:
        addHull( "diamond", "SkullbonezData/hulls/diamond.hull", 1.0f, 0.86f, 0.40f );
        break;
    default:
        break;
    }

    SceneState().modelCount = m_cGameModelCollection.GetModelCount();
}
