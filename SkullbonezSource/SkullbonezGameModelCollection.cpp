// --- Includes ---
#include "SkullbonezGameModelCollection.h"
#include "SkullbonezProfiler.h"
#include "SkullbonezObjectContactManifold.h"
#include "SkullbonezHelper.h"
#include "SkullbonezIRenderBackend.h"
#include "SkullbonezContactSolverCommon.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <type_traits>


// --- Usings ---
using namespace SkullbonezCore::GameObjects;
using namespace SkullbonezCore::Environment;
using namespace SkullbonezCore::Basics;
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Physics;
using SkullbonezCore::Math::Orientation::Quaternion;
using SkullbonezCore::Math::Transformation::Matrix4;
using SkullbonezCore::Math::Vector::Vector3;
using SkullbonezCore::Math::Vector::ZERO_VECTOR;
using SkullbonezCore::Rendering::Gfx;
using SkullbonezCore::Rendering::ShadowFrameData;
namespace Vector = SkullbonezCore::Math::Vector;


// Per-instance data layout: mat4 (16 floats) + alpha (1 float)
static constexpr int SHADOW_INSTANCE_FLOATS = 17;
static constexpr size_t MAX_PIPELINE_TRACE_RECORDS = 4096;
static constexpr int TERRAIN_BODY_INDEX = -1;
static constexpr int PINE_VISUAL_MATERIAL_MODE = 13;

static bool IsPineVisualMaterial( float colorOverride )
{
    return colorOverride > 1.25f && static_cast<int>( std::floor( colorOverride + 0.5f ) ) == PINE_VISUAL_MATERIAL_MODE;
}

// High-level physics pipeline in this file:
//   1. RunPhysics clears per-step scratch buffers and preserves persistent state.
//   2. RunSolverPhysics applies forces, finds broadphase pairs, handles CCD hit
//      timing, gathers terrain manifolds, and then calls the shared row solver.
//   3. SolvePersistentObjectContacts turns object and terrain manifolds into
//      Catto-style rows, warm-starts them, iterates impulses, writes velocity
//      back, fixes residual penetration, and stores next-frame cache entries.
//   4. Sleep support is propagated through contact islands after response, so a
//      quiet stack can sleep only when it is rooted in credible terrain/fixed
//      support.

GameModelCollection::GameModelCollection()
    : m_spatialGrid( Cfg().broadphaseCell )
{
    m_gameModels.reserve( MAX_GAME_MODELS );
    m_timeRemaining.reserve( MAX_GAME_MODELS );
    m_sleepSupportedThisFrame.reserve( MAX_GAME_MODELS );
    m_sleepInhibitedThisFrame.reserve( MAX_GAME_MODELS );
    m_collisionVisualContacts.reserve( MAX_GAME_MODELS );
    m_sleepIslandVisualId.reserve( MAX_GAME_MODELS );
    m_sleepIslandAssignedVisualId.reserve( MAX_GAME_MODELS );
    m_sleepSupportEdges.reserve( MAX_GAME_MODELS * 4 );
    m_sleepIslandParent.reserve( MAX_GAME_MODELS );
    m_sleepIslandRank.reserve( MAX_GAME_MODELS );
    m_sleepIslandHasAwake.reserve( MAX_GAME_MODELS );
    m_sleepIslandHasSupportAnchor.reserve( MAX_GAME_MODELS );
    m_sleepIslandEligible.reserve( MAX_GAME_MODELS );
    m_sleepIslandCanSleep.reserve( MAX_GAME_MODELS );
    m_persistentContacts.reserve( MAX_GAME_MODELS * 4 );
    m_persistentContactCache.reserve( MAX_GAME_MODELS * 4 );
    m_persistentContactCounts.reserve( MAX_GAME_MODELS );
    m_solverBodies.reserve( MAX_GAME_MODELS );
    m_physicsDebugContacts.reserve( MAX_GAME_MODELS * 4 );
    m_physicsPipelineTrace.reserve( MAX_PIPELINE_TRACE_RECORDS );
    m_terrainContactManifolds.reserve( MAX_GAME_MODELS );
    m_shadowInstanceData.reserve( MAX_GAME_MODELS * SHADOW_INSTANCE_FLOATS );
};

void GameModelCollection::AddGameModel( GameModel gameModel )
{
    assert( static_cast<int>( m_gameModels.size() ) < MAX_GAME_MODELS && "Exceeded MAX_GAME_MODELS" );
    m_gameModels.push_back( std::move( gameModel ) );
    InvalidateSoA();
}


void GameModelCollection::Clear()
{
    m_gameModels.clear();
    m_timeRemaining.clear();
    m_soaActiveCount = 0;
    InvalidateSoA();
    m_sleepSupportedThisFrame.clear();
    m_sleepInhibitedThisFrame.clear();
    m_sleepState.clear();
    m_sleepCounter.clear();
    m_collisionVisualContacts.clear();
    m_sleepIslandVisualId.clear();
    m_sleepIslandAssignedVisualId.clear();
    m_nextSleepIslandVisualId = 1;
    m_collisionVisualFrameActive = false;
    m_sleepSupportEdges.clear();
    m_sleepIslandParent.clear();
    m_sleepIslandRank.clear();
    m_sleepIslandHasAwake.clear();
    m_sleepIslandHasSupportAnchor.clear();
    m_sleepIslandEligible.clear();
    m_sleepIslandCanSleep.clear();
    m_persistentContacts.clear();
    m_persistentContactCache.clear();
    m_persistentContactSolverStats = PersistentContactSolverStats();
    m_persistentContactCounts.clear();
    m_solverBodies.clear();
    m_physicsDebugContacts.clear();
    m_terrainContactManifolds.clear();
}


void GameModelCollection::InvalidateSoA()
{
    // GameModel is the authoritative state store.  SoA arrays are rebuilt lazily
    // for render/broadphase hot paths and must be invalidated after any add,
    // clear, wake, integration, or external state edit.
    m_soaBodyDataValid = false;
    m_soaModelMatricesValid = false;
}


void GameModelCollection::RefreshSoABodyData()
{
    PROFILE_SCOPED( "Frame/SoA" );
    PROFILE_SCOPED( "Frame/SoA/RefreshBodyData" );

    const int modelCount = static_cast<int>( m_gameModels.size() );

    for ( int i = 0; i < modelCount; ++i )
    {
        m_soaPositions[i] = m_gameModels[i].GetPosition();
        m_soaBoundingRadii[i] = m_gameModels[i].GetBoundingRadius();
        m_soaIsBox[i] = m_gameModels[i].IsBox() ? 1 : 0;
        m_soaIsFixed[i] = m_gameModels[i].IsFixed() ? 1 : 0;
    }

    m_soaActiveCount = modelCount;
    m_soaBodyDataValid = true;
}


void GameModelCollection::EnsureSoAModelMatrices()
{
    if ( !m_soaBodyDataValid )
    {
        RefreshSoABodyData();
    }

    const int modelCount = static_cast<int>( m_gameModels.size() );
    if ( m_soaModelMatricesValid && m_soaActiveCount == modelCount )
    {
        return;
    }

    for ( int i = 0; i < modelCount; ++i )
    {
        m_soaModelMatrices[i] = m_gameModels[i].GetModelMatrix();
    }

    m_soaModelMatricesValid = true;
}


void GameModelCollection::PrepareRenderStreams()
{
    EnsureSoAModelMatrices();
}


void GameModelCollection::RenderModels( const Matrix4& view, const Matrix4& proj, const float lightPos[4], const CinematicRenderConfig* cinematic, const ShadowFrameData* shadow )
{
    if ( m_gameModels.empty() )
    {
        return;
    }

    EnsureSoAModelMatrices();
    const int modelCount = static_cast<int>( m_gameModels.size() );

    // Render non-box models through the sphere batch.
    SkullbonezHelper::DrawSphereBatchBegin( view, proj, lightPos, Cfg().runtimeRender.renderCollisionVolumes, cinematic, shadow );
    for ( int x = 0; x < modelCount; ++x )
    {
        if ( !m_soaIsBox[x] )
        {
            float tintR = 1.0f;
            float tintG = 1.0f;
            float tintB = 1.0f;
            float colorOverride = 0.0f;
            m_gameModels[x].GetRenderTint( tintR, tintG, tintB, colorOverride );
            if ( m_soaIsFixed[x] )
            {
                const float hit = m_gameModels[x].GetFixedContactHighlightAlpha();
                if ( hit > 0.0f )
                {
                    tintR = tintR + ( 1.0f - tintR ) * hit;
                    tintG = tintG * ( 1.0f - hit );
                    tintB = tintB * ( 1.0f - hit );
                }
            }
            SkullbonezHelper::DrawSphereBatchModel( m_soaModelMatrices[x], tintR, tintG, tintB, colorOverride );
        }
    }
    SkullbonezHelper::DrawSphereBatchEnd();

    bool hasPineVisualModels = false;
    auto appendBoxLikeModels = [&]( bool pineVisualPass )
    {
        for ( int x = 0; x < modelCount; ++x )
        {
            if ( m_soaIsBox[x] )
            {
                float tintR = 1.0f;
                float tintG = 1.0f;
                float tintB = 1.0f;
                float colorOverride = 0.0f;
                m_gameModels[x].GetRenderTint( tintR, tintG, tintB, colorOverride );
                const bool isPineVisual = IsPineVisualMaterial( colorOverride );
                if ( isPineVisual )
                {
                    hasPineVisualModels = true;
                }
                if ( isPineVisual != pineVisualPass )
                {
                    continue;
                }
                if ( m_soaIsFixed[x] )
                {
                    float hit = m_gameModels[x].GetFixedContactHighlightAlpha();
                    if ( colorOverride <= 0.5f && colorOverride >= -0.5f )
                    {
                        constexpr float fixedBase = 241.0f / 255.0f; // #F1F1F1
                        tintR = fixedBase + ( 1.0f - fixedBase ) * hit;
                        tintG = fixedBase * ( 1.0f - hit );
                        tintB = fixedBase * ( 1.0f - hit );
                        colorOverride = 1.0f;
                    }
                    else if ( hit > 0.0f )
                    {
                        tintR = tintR + ( 1.0f - tintR ) * hit;
                        tintG = tintG * ( 1.0f - hit );
                        tintB = tintB * ( 1.0f - hit );
                    }
                }
                if ( pineVisualPass )
                {
                    SkullbonezHelper::DrawPineBatchModel( m_soaModelMatrices[x], tintR, tintG, tintB, colorOverride );
                }
                else
                {
                    SkullbonezHelper::DrawBoxBatchModel( m_soaModelMatrices[x], tintR, tintG, tintB, colorOverride );
                }
            }
        }
    };

    SkullbonezHelper::DrawBoxBatchBegin( view, proj, lightPos, Cfg().runtimeRender.renderCollisionVolumes, cinematic, shadow );
    appendBoxLikeModels( false );
    SkullbonezHelper::DrawBoxBatchEnd();

    if ( hasPineVisualModels )
    {
        SkullbonezHelper::DrawPineBatchBegin( view, proj, lightPos, Cfg().runtimeRender.renderCollisionVolumes, cinematic, shadow );
        appendBoxLikeModels( true );
        SkullbonezHelper::DrawPineBatchEnd();
    }
}


void GameModelCollection::RenderShadowCasters( const Matrix4& view, const Matrix4& proj, const CinematicRenderConfig* cinematic )
{
    if ( m_gameModels.empty() )
    {
        return;
    }

    // The shadow pass writes only depth, but it still needs to draw the same
    // silhouettes as the visible forward pass. Keep the caster batches split by
    // mesh type (sphere, box, pine-style box visual) so each shape uses its real
    // vertex data instead of falling back to an approximate blob. This is the
    // path that makes rectangular boxes cast rectangular/oriented shadows.
    EnsureSoAModelMatrices();
    const int modelCount = static_cast<int>( m_gameModels.size() );

    // Non-box bodies are rendered with the sphere mesh. The helper may choose
    // the high-poly or low-poly sphere variant from the active visual style so
    // the shadow silhouette matches the visible object.
    SkullbonezHelper::DrawShadowDepthSphereBatchBegin( view, proj, cinematic );
    for ( int x = 0; x < modelCount; ++x )
    {
        if ( !m_soaIsBox[x] )
        {
            SkullbonezHelper::DrawShadowDepthSphereBatchModel( m_soaModelMatrices[x] );
        }
    }
    SkullbonezHelper::DrawShadowDepthSphereBatchEnd();

    bool hasPineVisualModels = false;
    auto appendBoxLikeModels = [&]( bool pineVisualPass )
    {
        // Boxes and pine visuals both start from box physics objects, but some
        // authored materials display a low-poly pine mesh instead of the cube.
        // Split them into two depth batches so tree-like visuals cast a tree-like
        // silhouette and ordinary boxes cast a box silhouette.
        for ( int x = 0; x < modelCount; ++x )
        {
            if ( !m_soaIsBox[x] )
            {
                continue;
            }
            float tintR = 1.0f;
            float tintG = 1.0f;
            float tintB = 1.0f;
            float colorOverride = 0.0f;
            m_gameModels[x].GetRenderTint( tintR, tintG, tintB, colorOverride );
            const bool isPineVisual = IsPineVisualMaterial( colorOverride );
            if ( isPineVisual )
            {
                hasPineVisualModels = true;
            }
            if ( isPineVisual != pineVisualPass )
            {
                continue;
            }
            if ( pineVisualPass )
            {
                SkullbonezHelper::DrawShadowDepthPineBatchModel( m_soaModelMatrices[x] );
            }
            else
            {
                SkullbonezHelper::DrawShadowDepthBoxBatchModel( m_soaModelMatrices[x] );
            }
        }
    };

    SkullbonezHelper::DrawShadowDepthBoxBatchBegin( view, proj );
    appendBoxLikeModels( false );
    SkullbonezHelper::DrawShadowDepthBoxBatchEnd();

    if ( hasPineVisualModels )
    {
        // Only pay for the pine depth batch when at least one box was styled as
        // a pine/tree visual. Most physics and benchmark scenes are boxes/balls
        // only, so they skip this extra draw call.
        SkullbonezHelper::DrawShadowDepthPineBatchBegin( view, proj );
        appendBoxLikeModels( true );
        SkullbonezHelper::DrawShadowDepthPineBatchEnd();
    }
}


void GameModelCollection::RenderShadows( Geometry::Terrain* m_terrain,
                                         const Matrix4& view,
                                         const Matrix4& proj,
                                         float waterSurfaceY )
{
    if ( !m_terrain )
    {
        return;
    }

    if ( !m_shadowInstMesh )
    {
        BuildShadowMesh();
    }

    // Build per-instance data: model matrix (16 floats) + alpha (1 float).
    // Pre-size once and write by index so we avoid repeated end-insert growth work.
    if ( !m_soaBodyDataValid )
    {
        RefreshSoABodyData();
    }
    int modelCount = static_cast<int>( m_gameModels.size() );
    m_shadowInstanceData.resize( modelCount * SHADOW_INSTANCE_FLOATS );
    int writeOffset = 0;
    for ( int i = 0; i < modelCount; ++i )
    {
        const Vector3& pos = m_soaPositions[i];
        float radius = m_soaBoundingRadii[i];

        if ( !m_terrain->IsInBounds( pos.x, pos.z ) )
        {
            continue;
        }

        float groundY;
        Vector3 N;
        m_terrain->GetTerrainHeightAndNormalAt( pos.x, pos.z, groundY, N );

        // Fast-out: model origin is over fully submerged terrain, so no visible shadow is needed.
        if ( groundY <= waterSurfaceY )
        {
            continue;
        }

        float height = pos.y - groundY - radius;
        if ( height < 0.0f )
        {
            height = 0.0f;
        }
        if ( height >= Cfg().shadowMaxHeight )
        {
            continue;
        }

        float alpha = Cfg().shadowMaxAlpha * ( 1.0f - height / Cfg().shadowMaxHeight );
        float shadowRadius = radius * Cfg().shadowScale;

        // Fused T(pos)*RotFromUpToN*Scale(shadowRadius) — no acosf/cosf/sinf, no Matrix4 products
        Matrix4 model = Matrix4::ShadowFromNormal( pos.x, groundY + Cfg().shadowOffset, pos.z, N, shadowRadius );

        // Write mat4 (16 floats) + alpha (1 float) at the current packed slot.
        const float* md = model.Data();
        memcpy( &m_shadowInstanceData[writeOffset], md, sizeof( float ) * 16 );
        m_shadowInstanceData[writeOffset + 16] = alpha;
        writeOffset += SHADOW_INSTANCE_FLOATS;
    }

    if ( writeOffset == 0 )
    {
        m_shadowInstanceData.clear();
        return;
    }

    m_shadowInstanceData.resize( writeOffset );
    int instanceCount = writeOffset / SHADOW_INSTANCE_FLOATS;
    if ( instanceCount == 0 )
    {
        return;
    }

    // Upload instance data
    Gfx().UploadInstanceData( m_shadowInstMesh, m_shadowInstanceData.data(), static_cast<int>( m_shadowInstanceData.size() ) );

    // Render all shadows in one instanced draw call
    Gfx().SetBlend( true );
    Gfx().SetBlendFunc( SkullbonezCore::Rendering::BlendFactor::SrcAlpha, SkullbonezCore::Rendering::BlendFactor::OneMinusSrcAlpha );
    Gfx().SetPolygonOffset( true, -1.0f, -1.0f );
    Gfx().SetCullFace( false );

    m_shadowShader->Use();
    m_shadowShader->SetMat4( "uView", view );
    m_shadowShader->SetMat4( "uProjection", proj );

    // Disable depth writes for shadow rendering. Shadow discs sit at groundY + offset which can
    // be above the water plane near shorelines. If they wrote to the depth buffer, the water
    // (drawn after) would fail the depth test at those pixels and disappear. With depth writes off,
    // terrain depth values are preserved and water renders correctly over the shoreline.
    // Depth testing remains ON so shadows still respect terrain occlusion.
    Gfx().SetDepthWrite( false );
    Gfx().DrawInstancedMesh( m_shadowInstMesh, m_shadowDiscVertexCount, instanceCount );
    Gfx().SetDepthWrite( true );

    Gfx().SetPolygonOffset( false );
    Gfx().SetCullFace( true );
    Gfx().SetBlend( false );
}


Vector3 GameModelCollection::GetModelPosition( int index )
{
    if ( index < 0 || index >= static_cast<int>( m_gameModels.size() ) )
    {
        throw std::runtime_error( "No game model exists at the specified index.  (GameModelCollection::GetModelPosition)" );
    }

    return m_gameModels[index].GetPosition();
}


int GameModelCollection::GetModelCount() const
{
    return static_cast<int>( m_gameModels.size() );
}


GameModel& GameModelCollection::GetModelAtIndex( int index )
{
    InvalidateSoA();
    return m_gameModels[index];
}


double GameModelCollection::GetSceneKineticEnergy()
{
    constexpr double REST_LINEAR_SPEED_SQ = 0.5 * 0.5;
    constexpr double REST_ANGULAR_SPEED_SQ = 0.3 * 0.3;
    double totalEnergy = 0.0;
    for ( GameModel& model : m_gameModels )
    {
        if ( model.IsFixed() )
        {
            continue;
        }

        const Vector3& vel = model.GetVelocity();
        const Vector3& omega = model.GetAngularVelocity();
        const Vector3& inertia = model.GetRotationalInertia();
        const double speedSq = static_cast<double>( vel.x ) * vel.x +
                               static_cast<double>( vel.y ) * vel.y +
                               static_cast<double>( vel.z ) * vel.z;
        const double omegaSq = static_cast<double>( omega.x ) * omega.x +
                               static_cast<double>( omega.y ) * omega.y +
                               static_cast<double>( omega.z ) * omega.z;
        if ( speedSq < REST_LINEAR_SPEED_SQ && omegaSq < REST_ANGULAR_SPEED_SQ )
        {
            continue;
        }
        const double angularEnergy = 0.5 *
                                     ( static_cast<double>( inertia.x ) * omega.x * omega.x +
                                       static_cast<double>( inertia.y ) * omega.y * omega.y +
                                       static_cast<double>( inertia.z ) * omega.z * omega.z );
        totalEnergy += 0.5 * static_cast<double>( model.GetMass() ) * speedSq + angularEnergy;
    }
    return totalEnergy;
}


void GameModelCollection::EnsureCollisionVisualBuffers( int modelCount )
{
    if ( static_cast<int>( m_collisionVisualContacts.size() ) != modelCount )
    {
        m_collisionVisualContacts.assign( modelCount, 0 );
    }
    if ( static_cast<int>( m_sleepIslandVisualId.size() ) != modelCount )
    {
        m_sleepIslandVisualId.assign( modelCount, 0 );
    }
}


void GameModelCollection::MarkCollisionVisualContact( int index )
{
    if ( index < 0 || index >= static_cast<int>( m_collisionVisualContacts.size() ) )
    {
        return;
    }
    m_collisionVisualContacts[index] = 1;
}


void GameModelCollection::MarkFixedContact( int index )
{
    if ( index < 0 || index >= static_cast<int>( m_gameModels.size() ) )
    {
        return;
    }
    if ( m_gameModels[index].IsFixed() )
    {
        m_gameModels[index].NotifyFixedContact( 0.5f );
    }
}


void GameModelCollection::RecordPhysicsPipelineStage( const Physics::PhysicsPipelineRecord& record )
{
    if ( m_physicsPipelineTrace.size() < MAX_PIPELINE_TRACE_RECORDS )
    {
        m_physicsPipelineTrace.push_back( record );
    }
}


void GameModelCollection::BeginCollisionVisualFrame()
{
    const int modelCount = static_cast<int>( m_gameModels.size() );
    m_collisionVisualContacts.assign( modelCount, 0 );
    if ( static_cast<int>( m_sleepIslandVisualId.size() ) != modelCount )
    {
        m_sleepIslandVisualId.assign( modelCount, 0 );
    }
    m_collisionVisualFrameActive = true;
}


void GameModelCollection::EndCollisionVisualFrame()
{
    m_collisionVisualFrameActive = false;
}


void GameModelCollection::RunPhysics( float fChangeInTime )
{
    // One physics tick owns all per-step scratch buffers.  Persistent sleep
    // state and warm-start caches survive across ticks, while contact visuals,
    // support flags, and time remainders are rebuilt for this exact dt.
    const int modelCount = static_cast<int>( m_gameModels.size() );
    EnsureCollisionVisualBuffers( modelCount );
    if ( !m_collisionVisualFrameActive )
    {
        m_collisionVisualContacts.assign( modelCount, 0 );
    }
    m_timeRemaining.assign( modelCount, fChangeInTime );
    m_sleepSupportedThisFrame.assign( modelCount, 0 );
    m_sleepInhibitedThisFrame.assign( modelCount, 0 );
    m_physicsDebugContacts.clear();
    m_physicsPipelineTrace.clear();
    m_terrainContactManifolds.clear();
    m_sleepSupportEdges.clear();

    for ( int i = 0; i < modelCount; ++i )
    {
        m_gameModels[i].TickFixedContactHighlight( fChangeInTime );
    }

    // Ensure sleep state vectors are sized (persists across frames)
    if ( static_cast<int>( m_sleepState.size() ) != modelCount )
    {
        m_sleepState.assign( modelCount, 0 );
        m_sleepCounter.assign( modelCount, 0 );
    }
    if ( !m_sleepEnabled )
    {
        std::fill( m_sleepState.begin(), m_sleepState.end(), static_cast<uint8_t>( 0 ) );
        std::fill( m_sleepCounter.begin(), m_sleepCounter.end(), static_cast<uint8_t>( 0 ) );
        std::fill( m_sleepIslandVisualId.begin(), m_sleepIslandVisualId.end(), 0 );
    }
    for ( int i = 0; i < modelCount; ++i )
    {
        if ( m_gameModels[i].IsFixed() )
        {
            m_sleepState[i] = 0;
            m_sleepCounter[i] = 0;
            m_sleepSupportedThisFrame[i] = 1;
            m_sleepIslandVisualId[i] = 0;
            continue;
        }
        if ( !m_sleepState[i] )
        {
            m_sleepIslandVisualId[i] = 0;
        }
    }

    RefreshSoABodyData();

    // All bodies now run through the unified solver path. Keeping the dispatch
    // narrow here makes it obvious that removed legacy modes cannot bypass the
    // same sleep, diagnostics, broadphase, and visualization bookkeeping.
    RunSolverPhysics( fChangeInTime );

    // Per-frame deterministic regression CSV. Active only when a path is set by
    // --physics-regression-log. SkullScope is the model-facing diagnostics path;
    // this CSV remains only as the byte-exact validation artifact until that
    // baseline is migrated.
#ifdef _DEBUG
    if ( m_physicsRegressionLogPath[0] != '\0' )
    {
        if ( m_physicsRegressionLogFrame == 0 )
        {
            Log().Writef( m_physicsRegressionLogPath, "frame,idx,name,posX,posY,posZ,velX,velY,velZ,speed,omegaX,omegaY,omegaZ,omegaMag,qX,qY,qZ,qW,grounded,sleeping,sleepInhibited\n" );
        }
        for ( int i = 0; i < modelCount; ++i )
        {
            const char* name = m_gameModels[i].GetName();
            const Vector3& pos = m_gameModels[i].GetPosition();
            const Vector3& vel = m_gameModels[i].GetVelocity();
            const Vector3& omega = m_gameModels[i].GetAngularVelocity();
            float qx = 0.0f;
            float qy = 0.0f;
            float qz = 0.0f;
            float qw = 1.0f;
            m_gameModels[i].GetOrientation().GetComponents( qx, qy, qz, qw );
            float speed = sqrtf( vel.x * vel.x + vel.y * vel.y + vel.z * vel.z );
            float omegaMag = sqrtf( omega.x * omega.x + omega.y * omega.y + omega.z * omega.z );
            // The CSV header still says "grounded" for existing tooling, but
            // this value is now the stricter sleep-support signal: terrain-backed
            // or supported-stack contact, not just "some contact happened."
            int sleepSupported = m_sleepSupportedThisFrame[i];
            int sleeping = ( i < static_cast<int>( m_sleepState.size() ) ) ? m_sleepState[i] : 0;
            int sleepInhibited = ( i < static_cast<int>( m_sleepInhibitedThisFrame.size() ) ) ? m_sleepInhibitedThisFrame[i] : 0;
            Log().Writef( m_physicsRegressionLogPath, "%d,%d,%s,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.6f,%.6f,%.6f,%.6f,%d,%d,%d\n", m_physicsRegressionLogFrame, i, name, pos.x, pos.y, pos.z, vel.x, vel.y, vel.z, speed, omega.x, omega.y, omega.z, omegaMag, qx, qy, qz, qw, sleepSupported, sleeping, sleepInhibited );
        }
        ++m_physicsRegressionLogFrame;
    }
    if ( m_physicsCollisionTimeLogPath[0] != '\0' )
    {
        ++m_physicsCollisionTimeLogFrame;
    }
    EmitPhysicsDiagnosticsFrame( fChangeInTime );
#endif

    InvalidateSoA();
}


void GameModelCollection::WakeModel( int index )
{
    if ( index >= 0 &&
         index < static_cast<int>( m_gameModels.size() ) &&
         m_gameModels[index].IsFixed() )
    {
        return;
    }

    // Size the sleep vectors if RunPhysics hasn't been called yet.
    if ( static_cast<int>( m_sleepState.size() ) != static_cast<int>( m_gameModels.size() ) )
    {
        m_sleepState.assign( m_gameModels.size(), 0 );
        m_sleepCounter.assign( m_gameModels.size(), 0 );
    }
    if ( index >= 0 && index < static_cast<int>( m_sleepState.size() ) )
    {
        InvalidateSoA();
        m_sleepState[index] = 0;
        m_sleepCounter[index] = 0;
        if ( index < static_cast<int>( m_sleepIslandVisualId.size() ) )
        {
            m_sleepIslandVisualId[index] = 0;
        }
    }
}


void GameModelCollection::SetPhysicsSleepEnabled( bool enabled )
{
    m_sleepEnabled = enabled;
    if ( enabled )
    {
        return;
    }

    std::fill( m_sleepState.begin(), m_sleepState.end(), static_cast<uint8_t>( 0 ) );
    std::fill( m_sleepCounter.begin(), m_sleepCounter.end(), static_cast<uint8_t>( 0 ) );
    std::fill( m_sleepIslandVisualId.begin(), m_sleepIslandVisualId.end(), 0 );
    std::fill( m_sleepIslandAssignedVisualId.begin(), m_sleepIslandAssignedVisualId.end(), 0 );
}


#ifdef _DEBUG
void GameModelCollection::SetPhysicsRegressionLogPath( const char* path )
{
    strcpy_s( m_physicsRegressionLogPath, sizeof( m_physicsRegressionLogPath ), path );
    m_physicsRegressionLogFrame = 0;
}


void GameModelCollection::SetPhysicsCollisionTimeLogPath( const char* path )
{
    strcpy_s( m_physicsCollisionTimeLogPath, sizeof( m_physicsCollisionTimeLogPath ), path );
    m_physicsCollisionTimeLogFrame = 0;
    m_physicsCollisionTimeHeaderWritten = false;
}


void GameModelCollection::SetPhysicsDiagnosticsPath( const char* path )
{
    m_skullScope.SetPath( path );
}


void GameModelCollection::SetPhysicsDiagnosticsRunId( const char* runId )
{
    m_skullScope.SetRunId( runId );
}


void GameModelCollection::EmitPhysicsDiagnosticsFrame( float dt )
{
    m_skullScope.EmitFrame( *this, dt );
}
#endif


void GameModelCollection::EmitPhysicsCollisionTime( const char* type, int bodyA, int bodyB, float collisionTime, float availableTime )
{
#ifdef _DEBUG
    if ( m_physicsCollisionTimeLogPath[0] == '\0' )
    {
        return;
    }

    if ( !m_physicsCollisionTimeHeaderWritten )
    {
        Log().Writef( m_physicsCollisionTimeLogPath, "frame,type,bodyA,bodyB,nameA,nameB,collisionTime,availableTime\n" );
        m_physicsCollisionTimeHeaderWritten = true;
    }

    const char* nameA = ( bodyA >= 0 && bodyA < static_cast<int>( m_gameModels.size() ) ) ? m_gameModels[bodyA].GetName() : "";
    const char* nameB = ( bodyB >= 0 && bodyB < static_cast<int>( m_gameModels.size() ) ) ? m_gameModels[bodyB].GetName() : "terrain";

    Log().Writef( m_physicsCollisionTimeLogPath,
                  "%d,%s,%d,%d,%s,%s,%.6f,%.6f\n",
                  m_physicsCollisionTimeLogFrame,
                  type,
                  bodyA,
                  bodyB,
                  nameA,
                  nameB,
                  collisionTime,
                  availableTime );
#else
    (void)type;
    (void)bodyA;
    (void)bodyB;
    (void)collisionTime;
    (void)availableTime;
#endif
}


void GameModelCollection::SolvePersistentObjectContacts( float dt )
{
    PROFILE_SCOPED( "Frame/Physics/Narrowphase/PersistentContacts" );

    // CATTO REF:
    //   This whole pass is the engine's closest match to Catto 2005,
    //   Agentic/Reference/ErinCatto_IterativeDynamics_GDC2005.pdf:
    //     - Section 4, PDF p. 9: contact point + normal model.
    //     - Section 6, PDF p. 14, Equations 34-35: time-stepped constraint
    //       system JB*lambda = eta.
    //     - Section 7.2, PDF pp. 16-17, Algorithm 4: Projected Gauss-Seidel
    //       over bounded lambda values.
    //     - Section 8, PDF pp. 18-19, Algorithm 5: cache lambda per contact
    //       identifier and reuse it as the next frame's initial guess.
    // REASON:
    //   One-shot collision impulses are good for impacts but poor at quiet
    //   support. Catto's temporal coherence lets resting contacts remember the
    //   support impulse they converged to last frame, so stacks and touching
    //   bodies do not rediscover support from zero every tick.
    //
    // ENGINE-SPECIFIC / NOVEL:
    //   Object-object narrowphase uses Skullbonez shape-pair manifold builders
    //   for the row geometry. The cache and PGS row shape are Catto; the exact
    //   sphere/box/OBB feature encodings are local engine policy.
    // This pass handles the quiet case that one-shot impact impulses are bad at:
    // dynamic bodies already touching each other, especially one body resting on another.
    // Instead of waiting for a fresh "impact", we build contact rules for pairs
    // that are touching or nearly touching, then solve those rules like tiny springs
    // with hard limits: push apart along the normal, resist sliding along tangents.
    const int modelCount = static_cast<int>( m_gameModels.size() );
    m_persistentContactSolverStats = PersistentContactSolverStats();
    m_persistentContactSolverStats.cachePreviousRows = static_cast<int>( m_persistentContactCache.size() );
    m_persistentContactCounts.assign( modelCount, 0 );
    if ( modelCount <= 0 ||
         ( m_candidatePairs.empty() && m_terrainContactManifolds.empty() ) )
    {
        m_persistentContacts.clear();
        m_persistentContactCache.clear();
        m_physicsDebugContacts.clear();
        return;
    }

    m_persistentContacts.clear();

    // ENGINE-SPECIFIC:
    //   Catto's normal constraint allows penetration and uses bias to resolve
    //   it (PDF p. 9 Section 4; PDF p. 10 Equation 20). This slop is local
    //   tolerance policy: tiny residual overlap is ignored so resting contacts
    //   do not jitter while chasing floating-point dust.
    // Small allowed overlap. Without this tolerance, floating-point noise makes
    // the solver chase microscopic errors and resting bodies visibly tremble.
    const float contactSlop = (std::max)( 0.0f, Cfg().persistentContactSlop );

    // CATTO REF:
    //   Catto 2005, PDF p. 8, Section 3.6, Equation 15 and PDF p. 10,
    //   Section 4.2, Equation 20. Reason: convert penetration error into a
    //   target separating velocity so overlap decays over several frames.
    // Baumgarte bias is a gentle "please separate" velocity for bodies that are
    // already interpenetrating. It removes overlap over several ticks instead of
    // teleporting everything apart in one harsh correction.
    const float baumgarteBeta = (std::max)( 0.0f, Cfg().persistentContactBaumgarteBeta );

    // ENGINE-SPECIFIC / NOVEL:
    //   Catto uses the bias term for penetration correction. This partial
    //   post-solve nudge is local visual cleanup for the current approximate
    //   object manifolds; it is intentionally partial so stacks do not pop.
    // A final direct positional nudge catches the remaining overlap after the
    // velocity solve. The percent is deliberately partial so stacks do not pop.
    const float positionCorrectionPercent = (std::max)( 0.0f, (std::min)( Cfg().persistentContactPositionCorrectionPercent, 1.0f ) );

    // CATTO REF:
    //   Catto 2005, PDF p. 15, Section 7, and PDF pp. 16-17, Section 7.2,
    //   Algorithm 4. Reason: repeat cheap row solves until the coupled contact
    //   system is visually good enough.
    // Projected Gauss-Seidel works by revisiting every contact repeatedly. Each
    // visit improves the answer a little; twelve passes is a compromise between
    // stack stability and keeping the physics hot path affordable.
    const int solverIterations = (std::max)( 1, Cfg().persistentContactSolverIterations );
    const float invDt = ( dt > TOLERANCE ) ? ( 1.0f / dt ) : 120.0f;

    // CATTO REF:
    //   Catto 2005, PDF pp. 18-19, Section 8.1/8.2 and Algorithm 5 store lambda
    //   with a contact identifier and retrieve it for matching contacts next
    //   frame.
    // ENGINE-SPECIFIC:
    //   This key is a compact pair+feature id. Manifold rows assign deterministic
    //   feature ids so warm starting survives multi-point box contacts.
    // Catto's cache needs a stable name for "body A touching body B at this
    // contact feature".  Box manifolds assign distinct feature ids per row.
    auto makeKey = []( int a, int b, uint32_t featureId ) -> int64_t
    {
        if ( b == TERRAIN_BODY_INDEX )
        {
            uint64_t packed = ( uint64_t( 0xffffu ) << 48 ) |
                              ( static_cast<uint64_t>( static_cast<uint32_t>( a ) ) << 16 ) |
                              static_cast<uint64_t>( featureId & 0xffffu );
            return static_cast<int64_t>( packed );
        }

        int lo = ( a < b ) ? a : b;
        int hi = ( a < b ) ? b : a;
        uint64_t packed = ( static_cast<uint64_t>( static_cast<uint32_t>( lo ) ) << 40 ) |
                          ( static_cast<uint64_t>( static_cast<uint32_t>( hi ) ) << 16 ) |
                          static_cast<uint64_t>( featureId & 0xffffu );
        return static_cast<int64_t>( packed );
    };

    {
        PROFILE_SCOPED( "Frame/Physics/Narrowphase/PersistentContacts/BodySetup" );
        m_solverBodies.assign( modelCount, SolverBodyState() );

        // CATTO REF:
        //   Catto 2005, PDF p. 7, Algorithms 1-2 and PDF p. 16, Algorithm 4 work on
        //   sparse body velocity blocks. Algorithm 4 names the mutable velocity-like
        //   work vector "a".
        // ENGINE-SPECIFIC / NOVEL:
        //   We keep compact per-body solver state here and write back once after PGS.
        //   That preserves Catto's sparse-row shape while avoiding repeated GameModel
        //   getter/setter churn inside the row loop.
        for ( int i = 0; i < modelCount; ++i )
        {
            GameModel& model = m_gameModels[i];
            SolverBodyState& body = m_solverBodies[i];
            if ( m_sleepState[i] || m_soaIsFixed[i] )
            {
                // Sleeping bodies still provide persistent support to awake bodies,
                // but they behave as static anchors until deliberately woken.
                body.linearVelocity = ZERO_VECTOR;
                body.angularVelocity = ZERO_VECTOR;
                body.invMass = 0.0f;
                body.invInertia = ZERO_VECTOR;
                body.useWorldInertia = false;
            }
            else
            {
                body.linearVelocity = model.GetVelocity();
                body.angularVelocity = model.GetAngularVelocity();
                body.invMass = model.GetInvertedMass();
                body.invInertia = model.GetInvertedRotationalInertia();
                body.useWorldInertia = model.IsBox();
            }
            if ( body.useWorldInertia )
            {
                Quaternion orientation = model.GetOrientation();
                body.orientation = orientation.GetOrientationMatrix();
            }
        }
    }

    if ( m_persistentContactCache.size() > 1 )
    {
        std::sort( m_persistentContactCache.begin(),
                   m_persistentContactCache.end(),
                   []( const PersistentContactCacheEntry& lhs, const PersistentContactCacheEntry& rhs )
                   {
                       return lhs.key < rhs.key;
                   } );
#ifdef _DEBUG
        assert( std::is_sorted( m_persistentContactCache.begin(),
                                m_persistentContactCache.end(),
                                []( const PersistentContactCacheEntry& lhs, const PersistentContactCacheEntry& rhs )
                                {
                                    return lhs.key < rhs.key;
                                } ) &&
                "persistent contact cache must be sorted before lower_bound lookup" );
#endif
    }

    // CATTO REF:
    //   Catto 2005, PDF p. 12, Section 5, unnumbered inertia transform before
    //   Equations 26-28: I_world^-1 = R * I_body^-1 * R^T.
    // Inertia is rotational mass. Boxes need world-space inertia because their
    // local inertia axes rotate with orientation; spheres remain isotropic.
    auto applyInvInertia = [&]( int body, const Vector3& v ) -> Vector3
    {
        if ( body == TERRAIN_BODY_INDEX )
        {
            return ZERO_VECTOR;
        }

        const SolverBodyState& solverBody = m_solverBodies[body];
        if ( !solverBody.useWorldInertia )
        {
            return Vector::VectorMultiply( solverBody.invInertia, v );
        }

        Vector3 bodyV = solverBody.orientation.TransposeMultiply( v );
        return solverBody.orientation * Vector::VectorMultiply( solverBody.invInertia, bodyV );
    };

    // CATTO REF:
    //   Catto 2005, PDF p. 5, Section 3.3, Equation 7 says constraint forces are
    //   Fc = J^T*lambda. PDF p. 8, Algorithm 2 shows accumulating those row
    //   contributions into body force/torque blocks.
    // REASON:
    //   Applying an impulse to a contact row changes linear velocity by
    //   invMass*impulse and angular velocity by I^-1*(r cross impulse). Body A
    //   receives the opposite impulse from body B.
    // Apply one impulse to both bodies using Newton's third law: equal and
    // opposite pushes. A receives -impulse, B receives +impulse. The cross
    // products turn off-center pushes into spin changes.
    auto applyImpulse = [&]( const PersistentContact& c, const Vector3& impulse )
    {
        SolverBodyState& a = m_solverBodies[c.bodyA];

        a.linearVelocity -= impulse * a.invMass;
        a.angularVelocity -= applyInvInertia( c.bodyA, Vector::CrossProduct( c.rA, impulse ) );
        if ( c.bodyB != TERRAIN_BODY_INDEX )
        {
            SolverBodyState& b = m_solverBodies[c.bodyB];
            b.linearVelocity += impulse * b.invMass;
            b.angularVelocity += applyInvInertia( c.bodyB, Vector::CrossProduct( c.rB, impulse ) );
        }
    };

    auto conservativeContactRadius = []( const GameModel& model ) -> float
    {
        // Broadphase radii must include any local shape offset. If a shape is
        // not centered on the body origin, the "safe maybe touching" sphere has
        // to reach from the origin all the way to the farthest shifted point.
        const CollisionShape& shape = model.GetCollisionShape();
        float radius = GetShapeBoundingRadius( shape );
        const Vector3& offset = GetShapePosition( shape );
        float offsetSq = Vector::VectorMagSquared( offset );
        if ( offsetSq > TOLERANCE * TOLERANCE )
        {
            radius += sqrtf( offsetSq );
        }
        return radius;
    };

    auto appendSleepSupportEdge = [&]( int aIndex, int bIndex, const Vector3& normal )
    {
        constexpr float supportNormalY = 0.25f;
        // This records only a possible vertical support relationship. It does
        // not grant sleep support by itself; support must propagate later from
        // terrain or a body that already passed the full sleep gate. That keeps
        // mid-air object-object impacts from becoming false "grounded" evidence.
        if ( normal.y > supportNormalY )
        {
            m_sleepSupportEdges.emplace_back( aIndex, bIndex );
            Physics::PhysicsPipelineRecord record;
            record.stage = Physics::PhysicsPipelineStage::SleepSupportEdge;
            record.bodyA = aIndex;
            record.bodyB = bIndex;
            record.normal = normal;
            record.point = ( m_gameModels[aIndex].GetPosition() + m_gameModels[bIndex].GetPosition() ) * 0.5f;
            record.scalarA = normal.y;
            RecordPhysicsPipelineStage( record );
        }
        else if ( normal.y < -supportNormalY )
        {
            m_sleepSupportEdges.emplace_back( bIndex, aIndex );
            Physics::PhysicsPipelineRecord record;
            record.stage = Physics::PhysicsPipelineStage::SleepSupportEdge;
            record.bodyA = bIndex;
            record.bodyB = aIndex;
            record.normal = -normal;
            record.point = ( m_gameModels[aIndex].GetPosition() + m_gameModels[bIndex].GetPosition() ) * 0.5f;
            record.scalarA = -normal.y;
            RecordPhysicsPipelineStage( record );
        }
    };

    // CATTO REF:
    //   Catto 2005, PDF p. 9, Section 4 "Contact Model" and Equation 16 require
    //   a contact point, a normal, and separation/penetration for each row.
    // ENGINE-SPECIFIC / NOVEL:
    //   Broadphase still uses conservative bounding radii, but the authoritative
    //   object contact geometry now comes from shape-pair manifolds: exact
    //   sphere/sphere, closest-point sphere/box, and SAT/clipped OBB contacts.
    // First pass: turn broadphase candidate pairs into Catto-style contact rows.
    // Each manifold point becomes one persistent row with its own feature id so
    // warm starting can remember face contacts instead of one pair-wide fallback.
    {
        PROFILE_SCOPED( "Frame/Physics/Narrowphase/PersistentContacts/BuildManifolds" );
        m_persistentContacts.reserve( m_candidatePairs.size() * 4 );
        for ( const auto& cp : m_candidatePairs )
        {
            int aIndex = cp.first;
            int bIndex = cp.second;
            if ( aIndex == bIndex || ( m_sleepState[aIndex] && m_sleepState[bIndex] ) )
            {
                continue;
            }

            if ( bIndex < aIndex )
            {
                std::swap( aIndex, bIndex );
            }

            GameModel& a = m_gameModels[aIndex];
            GameModel& b = m_gameModels[bIndex];

            Vector3 centerDelta = b.GetPosition() - a.GetPosition();
            float contactDistance = conservativeContactRadius( a ) + conservativeContactRadius( b ) + Cfg().contactEpsilon;
            if ( Vector::VectorMagSquared( centerDelta ) > contactDistance * contactDistance )
            {
                continue;
            }

            Vector3 contactNormal = ZERO_VECTOR;
            bool hasContact = false;
            ObjectContactManifold manifold;
            if ( BuildObjectContactManifold( a, b, aIndex, bIndex, Cfg().contactEpsilon, manifold ) )
            {
                contactNormal = manifold.normal;
                for ( uint8_t pointIndex = 0; pointIndex < manifold.pointCount; ++pointIndex )
                {
                    const ObjectContactPoint& point = manifold.points[pointIndex];

                    // CATTO REF:
                    //   rA/rB are the r1/r2 contact arms in Catto 2005, PDF p. 6,
                    //   Equations 9-11 and PDF p. 9, Equations 16-18.
                    PersistentContact c;
                    c.bodyA = aIndex;
                    c.bodyB = bIndex;
                    c.featureId = point.featureId;
                    c.key = makeKey( aIndex, bIndex, c.featureId );
                    c.normal = manifold.normal;
                    c.rA = point.rA;
                    c.rB = point.rB;
                    c.penetration = point.penetration;
                    m_persistentContacts.push_back( c );
                    ++m_persistentContactCounts[aIndex];
                    ++m_persistentContactCounts[bIndex];

                    Physics::PhysicsPipelineRecord record;
                    record.stage = Physics::PhysicsPipelineStage::ManifoldRow;
                    record.bodyA = aIndex;
                    record.bodyB = bIndex;
                    record.featureId = point.featureId;
                    record.point = point.point;
                    record.normal = manifold.normal;
                    record.scalarA = point.penetration;
                    record.scalarB = static_cast<float>( pointIndex );
                    record.scalarC = static_cast<float>( manifold.pointCount );
                    RecordPhysicsPipelineStage( record );
                }
                hasContact = manifold.pointCount > 0;
            }

            if ( !hasContact )
            {
                continue;
            }

            MarkCollisionVisualContact( aIndex );
            MarkCollisionVisualContact( bIndex );
            appendSleepSupportEdge( aIndex, bIndex, contactNormal );
        }
    }

    {
        PROFILE_SCOPED( "Frame/Physics/Terrain" );
        PROFILE_SCOPED( "Frame/Physics/Terrain/Rows" );

        // Convert terrain manifolds into the same PersistentContact rows used by
        // object/object contacts. Terrain uses TERRAIN_BODY_INDEX for body B, so
        // later solver phases treat it as infinite mass, zero velocity, and no
        // writeback. From this point on, terrain response is ordinary shared-row
        // normal/friction solving.
        size_t terrainRowCount = 0;
        for ( const Physics::TerrainContactManifold& manifold : m_terrainContactManifolds )
        {
            terrainRowCount += manifold.pointCount;
        }
        m_persistentContacts.reserve( m_persistentContacts.size() + terrainRowCount );

        for ( const Physics::TerrainContactManifold& manifold : m_terrainContactManifolds )
        {
            // Skip invalid/no-op manifolds before they affect profiler counts,
            // pipeline records, or the warm-start cache. Sleeping bodies do not
            // need fresh terrain rows; their accepted support state is already
            // represented by the sleep island data.
            if ( manifold.bodyA < 0 ||
                 manifold.bodyA >= modelCount ||
                 manifold.pointCount == 0 ||
                 ( manifold.bodyA < static_cast<int>( m_sleepState.size() ) && m_sleepState[manifold.bodyA] ) )
            {
                continue;
            }

            Physics::PhysicsPipelineRecord manifoldRecord;
            manifoldRecord.stage = Physics::PhysicsPipelineStage::TerrainManifold;
            manifoldRecord.bodyA = manifold.bodyA;
            manifoldRecord.bodyB = TERRAIN_BODY_INDEX;
            manifoldRecord.point = manifold.points[0].point;
            manifoldRecord.normal = manifold.normal;
            manifoldRecord.scalarA = static_cast<float>( manifold.pointCount );
            manifoldRecord.scalarB = manifold.supportsRestingPolicy ? 1.0f : 0.0f;
            manifoldRecord.scalarC = manifold.timeOfImpact;
            RecordPhysicsPipelineStage( manifoldRecord );

            // Stable terrain support receives a gravity-sized normal seed so a
            // resting body does not sink a little before the solver rediscovers
            // the support force. Edge/point terrain contacts deliberately get
            // zero here: they still resolve impact and penetration, but cannot
            // become sleep anchors or rest-friction anchors.
            const float warmStartTotal = manifold.supportsRestingPolicy
                                             ? m_gameModels[manifold.bodyA].GetMass() * fabsf( Cfg().gravity ) * fabsf( manifold.normal.y ) * dt
                                             : 0.0f;
            const float warmStartPerContact = warmStartTotal / static_cast<float>( manifold.pointCount );

            for ( uint8_t pointIndex = 0; pointIndex < manifold.pointCount; ++pointIndex )
            {
                const Physics::TerrainContactPoint& point = manifold.points[pointIndex];

                PersistentContact c;
                c.bodyA = manifold.bodyA;
                c.bodyB = TERRAIN_BODY_INDEX;
                c.featureId = point.featureId;
                c.key = makeKey( c.bodyA, c.bodyB, c.featureId );

                // PersistentContact normals point from body A toward body B.
                // Terrain manifold normals point out of the terrain and into
                // body A, so flip them to match the shared solver convention.
                c.normal = -manifold.normal;
                c.tangent1 = manifold.tangent1;
                c.tangent2 = manifold.tangent2;
                c.rA = point.rA;
                c.rB = ZERO_VECTOR;
                c.penetration = point.penetration;
                c.isTerrain = true;
                c.supportsRestingPolicy = manifold.supportsRestingPolicy;
                c.inhibitsSleep = manifold.inhibitsSleep;
                c.manifoldPointCount = manifold.pointCount;
                c.terrainNormal = manifold.normal;
                c.terrainWarmStart = warmStartPerContact;
                m_persistentContacts.push_back( c );

                Physics::PhysicsPipelineRecord rowRecord;
                rowRecord.stage = Physics::PhysicsPipelineStage::TerrainRow;
                rowRecord.bodyA = c.bodyA;
                rowRecord.bodyB = TERRAIN_BODY_INDEX;
                rowRecord.featureId = c.featureId;
                rowRecord.point = point.point;
                rowRecord.normal = manifold.normal;
                rowRecord.scalarA = point.penetration;
                rowRecord.scalarB = warmStartPerContact;
                rowRecord.scalarC = static_cast<float>( pointIndex );
                RecordPhysicsPipelineStage( rowRecord );
            }
        }
    }

    if ( m_persistentContacts.empty() )
    {
        m_persistentContactCache.clear();
        m_physicsDebugContacts.clear();
        return;
    }
    m_persistentContactSolverStats.rowCount = static_cast<int>( m_persistentContacts.size() );
    const SolverBodyState staticTerrainBody;

    // Second pass: precompute each row. This is the "setup" part of the paper:
    // CATTO REF:
    //   Catto 2005, PDF p. 17, Algorithm 4 initializes d_i from Jsp*Bsp before
    //   iteration. PDF p. 14, Equations 34-35 define B = M^-1*J^T. The code
    //   below expands that sparse matrix math into scalar effective masses.
    // The setup below builds friction axes, effective masses, bias, friction
    // limits, and pulls the previous frame's accumulated impulses from the cache.
    {
        PROFILE_SCOPED( "Frame/Physics/Narrowphase/PersistentContacts/Precompute" );
        for ( PersistentContact& c : m_persistentContacts )
        {
            GameModel& a = m_gameModels[c.bodyA];
            const SolverBodyState& bodyA = m_solverBodies[c.bodyA];
            const SolverBodyState& bodyB = c.isTerrain ? staticTerrainBody : m_solverBodies[c.bodyB];

            // CATTO REF:
            //   Catto 2005, PDF pp. 11-12, Section 4.3, Equations 21-23 use two
            //   tangent directions named u1/u2 perpendicular to the contact normal.
            // ENGINE MAPPING:
            //   Skullbonez stores Catto's u1/u2 basis as c.tangent1/c.tangent2.
            //   The normal covers push-apart motion; the two tangent axes cover
            //   sideways sliding in the contact plane.
            Physics::ContactSolver::BuildContactTangents( c.normal, c.tangent1, c.tangent2 );

            // CATTO REF:
            //   Catto 2005, PDF p. 17, Algorithm 4 computes d_i = J_i*B_i. With
            //   B = M^-1*J^T from PDF p. 14, Equations 34-35, this becomes the
            //   familiar point-contact effective mass:
            //       axis dot ((I^-1 * (r cross axis)) cross r) plus invMass terms.
            // Effective mass says how stubborn this contact is. A light body pushed
            // through its center moves easily; a heavy or off-center body resists more
            // because some of the push also has to rotate it.
            auto applyInvInertiaA = [&]( const Vector3& v ) -> Vector3
            {
                return applyInvInertia( c.bodyA, v );
            };
            auto applyInvInertiaB = [&]( const Vector3& v ) -> Vector3
            {
                return c.isTerrain ? ZERO_VECTOR : applyInvInertia( c.bodyB, v );
            };
            c.normalMass = Physics::ContactSolver::ComputeTwoBodyEffectiveMass(
                bodyA.invMass,
                bodyB.invMass,
                c.normal,
                c.rA,
                c.rB,
                applyInvInertiaA,
                applyInvInertiaB );
            c.tangentMass1 = Physics::ContactSolver::ComputeTwoBodyEffectiveMass(
                bodyA.invMass,
                bodyB.invMass,
                c.tangent1,
                c.rA,
                c.rB,
                applyInvInertiaA,
                applyInvInertiaB );
            c.tangentMass2 = Physics::ContactSolver::ComputeTwoBodyEffectiveMass(
                bodyA.invMass,
                bodyB.invMass,
                c.tangent2,
                c.rA,
                c.rB,
                applyInvInertiaA,
                applyInvInertiaB );

            Vector3 velA = bodyA.linearVelocity + Vector::CrossProduct( bodyA.angularVelocity, c.rA );
            Vector3 velB = c.isTerrain ? ZERO_VECTOR : bodyB.linearVelocity + Vector::CrossProduct( bodyB.angularVelocity, c.rB );
            float vn = ( velB - velA ) * c.normal;

            // CATTO REF:
            //   Catto 2005, PDF p. 8, Section 3.6, Equation 15 and PDF p. 10,
            //   Section 4.2, Equation 20 provide the contact bias idea.
            // ENGINE NOTE:
            //   Object/object swept detection no longer applies a competing
            //   immediate impulse. Dynamic bounce therefore belongs in the same
            //   persistent Catto rows as fixed-body impact and resting support.
            c.bias = 0.0f;
            if ( c.isTerrain )
            {
                const float terrainSlop = (std::max)( 0.0f, Cfg().terrainContactSlop );
                if ( !c.supportsRestingPolicy &&
                     c.penetration <= terrainSlop &&
                     vn > -Cfg().contactRestitutionThreshold )
                {
                    c.normalMass = 0.0f;
                    c.tangentMass1 = 0.0f;
                    c.tangentMass2 = 0.0f;
                }
                else if ( fabsf( vn ) < Cfg().contactRestitutionThreshold )
                {
                    float penetrationError = c.penetration - terrainSlop;
                    if ( penetrationError > 0.0f )
                    {
                        const float terrainBeta = (std::max)( 0.0f, Cfg().terrainContactBaumgarteBeta );
                        const float maxTerrainBias = (std::max)( 0.0f, Cfg().terrainMaxBaumgarteBias );
                        c.bias = terrainBeta * penetrationError * invDt;
                        if ( c.bias > maxTerrainBias )
                        {
                            c.bias = maxTerrainBias;
                        }
                    }
                }
                else if ( vn < -Cfg().contactRestitutionThreshold )
                {
                    const uint8_t pointCount = c.manifoldPointCount > 0 ? c.manifoldPointCount : 1;
                    c.bias = ( -a.GetCoefficientRestitution() * vn ) / static_cast<float>( pointCount );
                }
            }
            else if ( vn < -Cfg().contactRestitutionThreshold )
            {
                GameModel& b = m_gameModels[c.bodyB];
                float restitution = sqrtf( a.GetCoefficientRestitution() * b.GetCoefficientRestitution() );
                c.bias = -restitution * vn;
            }
            else if ( vn >= -Cfg().contactRestitutionThreshold )
            {
                float penetrationError = c.penetration - contactSlop;
                if ( penetrationError > 0.0f )
                {
                    c.bias = baumgarteBeta * penetrationError * invDt;
                }
            }

            uint16_t countA = ( m_persistentContactCounts[c.bodyA] > 0 ) ? m_persistentContactCounts[c.bodyA] : 1;
            float contactMass = a.GetMass() / static_cast<float>( countA );
            if ( !c.isTerrain )
            {
                GameModel& b = m_gameModels[c.bodyB];
                uint16_t countB = ( m_persistentContactCounts[c.bodyB] > 0 ) ? m_persistentContactCounts[c.bodyB] : 1;
                float contactMassB = b.GetMass() / static_cast<float>( countB );
                if ( contactMassB < contactMass )
                {
                    contactMass = contactMassB;
                }
            }
            // CATTO REF:
            //   Catto 2005, PDF p. 12, Section 4.3, Equations 24-25 bound tangent
            //   lambdas by +/-mu*m_c*g. Reason: avoid coupling tangent friction to
            //   solved normal force while keeping static friction usable in games.
            c.frictionLimit = c.isTerrain
                                  ? Cfg().frictionCoeff * c.terrainWarmStart
                                  : Cfg().frictionCoeff * contactMass * fabsf( Cfg().gravity ) * dt;

            // CATTO REF:
            //   Catto 2005, PDF pp. 18-19, Section 8.1 and Algorithm 5. Reason:
            //   retrieve cached lambda for matching contact identifiers and use it
            //   as the initial lambda_0 for Algorithm 4.
            // Warm starting: if this same pair+feature was touching last frame,
            // start from the cached solution instead of zero.  The cache is sorted so
            // lookup does not linearly scan every previous-frame contact.
            const bool canUseCachedWarmStart = !c.isTerrain || c.supportsRestingPolicy;
            auto cachedIt = canUseCachedWarmStart
                                ? std::lower_bound(
                                      m_persistentContactCache.begin(),
                                      m_persistentContactCache.end(),
                                      c.key,
                                      []( const PersistentContactCacheEntry& entry, int64_t key )
                                      {
                                          return entry.key < key;
                                      } )
                                : m_persistentContactCache.end();
            if ( canUseCachedWarmStart && cachedIt != m_persistentContactCache.end() && cachedIt->key == c.key )
            {
                ++m_persistentContactSolverStats.cacheHits;
                c.accN = ( cachedIt->accN > 0.0f ) ? cachedIt->accN : 0.0f;
                c.accT1 = cachedIt->accT1;
                c.accT2 = cachedIt->accT2;
                const float cachedFrictionLimit = c.isTerrain
                                                      ? Cfg().frictionCoeff * ( ( c.accN > c.terrainWarmStart ) ? c.accN : c.terrainWarmStart )
                                                      : c.frictionLimit;
                Physics::ContactSolver::ClampFrictionVector( c.accT1, c.accT2, cachedFrictionLimit );
                c.warmStarted = c.accN > 0.0f || fabsf( c.accT1 ) > 0.0f || fabsf( c.accT2 ) > 0.0f;
            }
            else if ( canUseCachedWarmStart )
            {
                ++m_persistentContactSolverStats.cacheMisses;
            }

            if ( c.isTerrain && c.terrainWarmStart > c.accN )
            {
                c.accN = c.terrainWarmStart;
                c.warmStarted = c.accN > 0.0f || c.warmStarted;
            }

            if ( c.warmStarted )
            {
                ++m_persistentContactSolverStats.warmStartedRows;
            }

            {
                Physics::PhysicsPipelineRecord record;
                record.stage = Physics::PhysicsPipelineStage::WarmStart;
                record.bodyA = c.bodyA;
                record.bodyB = c.bodyB;
                record.featureId = c.featureId;
                record.point = m_gameModels[c.bodyA].GetPosition() + c.rA;
                record.normal = c.normal;
                record.scalarA = c.warmStarted ? 1.0f : 0.0f;
                record.scalarB = c.accN;
                record.scalarC = c.frictionLimit;
                RecordPhysicsPipelineStage( record );
            }

            if ( c.accN > 0.0f || fabsf( c.accT1 ) > 0.0f || fabsf( c.accT2 ) > 0.0f )
            {
                // CATTO REF:
                //   Catto 2005, PDF p. 17, Algorithm 4 initializes a = B*lambda.
                //   In this implementation, "a" is represented by the mutable solver
                //   velocities, so cached lambda must be applied before iteration.
                // Cached impulses are not just bookkeeping: they must be applied to
                // the bodies before iteration starts, otherwise the solver would clamp
                // against a pretend push that never actually happened.
                Vector3 warmImpulse = c.normal * c.accN + c.tangent1 * c.accT1 + c.tangent2 * c.accT2;
                applyImpulse( c, warmImpulse );
            }
        }
    }

    // Third pass: Projected Gauss-Seidel.
    // CATTO REF:
    //   Catto 2005, PDF pp. 16-17, Section 7.2, Algorithm 4. Reason: compute a
    //   lambda increment per row, clamp accumulated lambda to the row's bounds,
    //   then apply only the actual delta so the running velocity state remains
    //   consistent with B*lambda.
    // In engine terms, each contact computes the extra impulse needed to reduce
    // its current violation, adds that to the accumulated total, clamps the total
    // to valid bounds, then applies only the difference.
    {
        PROFILE_SCOPED( "Frame/Physics/Narrowphase/PersistentContacts/SolveRows" );
        for ( int iter = 0; iter < solverIterations; ++iter )
        {
            m_persistentContactSolverStats.solverIterations = iter + 1;
            float iterImpulseSq = 0.0f;
            for ( PersistentContact& c : m_persistentContacts )
            {
                SolverBodyState& a = m_solverBodies[c.bodyA];
                const SolverBodyState& b = c.isTerrain ? staticTerrainBody : m_solverBodies[c.bodyB];

                Vector3 velA = a.linearVelocity + Vector::CrossProduct( a.angularVelocity, c.rA );
                Vector3 velB = c.isTerrain ? ZERO_VECTOR : b.linearVelocity + Vector::CrossProduct( b.angularVelocity, c.rB );
                float vn = ( velB - velA ) * c.normal;
                float lambdaN = c.normalMass * ( c.bias - vn );
                float oldAccN = c.accN;

                // CATTO REF:
                //   Catto 2005, PDF p. 8, Section 3.5, Equation 14 and PDF p. 9,
                //   Equation 19 set the normal lower bound to zero.
                // Normal impulses are one-way. Contacts can push bodies apart, but
                // they cannot glue bodies together, so the accumulated value is >= 0.
                c.accN = ( oldAccN + lambdaN > 0.0f ) ? oldAccN + lambdaN : 0.0f;
                float deltaN = c.accN - oldAccN;
                applyImpulse( c, c.normal * deltaN );

                velA = a.linearVelocity + Vector::CrossProduct( a.angularVelocity, c.rA );
                velB = c.isTerrain ? ZERO_VECTOR : b.linearVelocity + Vector::CrossProduct( b.angularVelocity, c.rB );
                float vt1 = ( velB - velA ) * c.tangent1;
                float vt2 = ( velB - velA ) * c.tangent2;
                float lambdaT1 = c.tangentMass1 * ( -vt1 );
                float lambdaT2 = c.tangentMass2 * ( -vt2 );
                float oldAccT1 = c.accT1;
                float oldAccT2 = c.accT2;

                // ENGINE-SPECIFIC / NOVEL:
                //   Catto clamps tangent lambdas independently in PDF p. 12,
                //   Equations 24-25. Skullbonez instead clamps the two accumulated
                //   tangent lambdas as a vector so diagonal friction cannot exceed
                //   the intended budget.
                // Clamp the two tangent accumulators as one 2D friction cone.  The
                // old per-axis clamp allowed diagonal friction to exceed the budget.
                c.accT1 = oldAccT1 + lambdaT1;
                c.accT2 = oldAccT2 + lambdaT2;
                const float frictionLimit = c.isTerrain
                                                ? Cfg().frictionCoeff * ( ( c.accN > c.terrainWarmStart ) ? c.accN : c.terrainWarmStart )
                                                : c.frictionLimit;
                Physics::ContactSolver::ClampFrictionVector( c.accT1, c.accT2, frictionLimit );
                float deltaT1 = c.accT1 - oldAccT1;
                float deltaT2 = c.accT2 - oldAccT2;
                applyImpulse( c, c.tangent1 * deltaT1 + c.tangent2 * deltaT2 );

                iterImpulseSq += deltaN * deltaN + deltaT1 * deltaT1 + deltaT2 * deltaT2;

                Physics::PhysicsPipelineRecord record;
                record.stage = Physics::PhysicsPipelineStage::SolverIteration;
                record.bodyA = c.bodyA;
                record.bodyB = c.bodyB;
                record.iteration = iter;
                record.featureId = c.featureId;
                record.point = m_gameModels[c.bodyA].GetPosition() + c.rA;
                record.normal = c.normal;
                record.scalarA = deltaN;
                record.scalarB = c.accN;
                record.scalarC = sqrtf( c.accT1 * c.accT1 + c.accT2 * c.accT2 );
                RecordPhysicsPipelineStage( record );
            }

            // ENGINE-SPECIFIC / NOVEL:
            //   Catto lists residual/delta-based termination as a possible
            //   Gauss-Seidel criterion on PDF p. 15, Section 7.1, then uses fixed
            //   iterations for simplicity. This deterministic early-out is a local
            //   optimization using total squared impulse delta.
            if ( iterImpulseSq < 1.0e-6f )
            {
                break;
            }
        }
    }

    {
        PROFILE_SCOPED( "Frame/Physics/Terrain" );
        PROFILE_SCOPED( "Frame/Physics/Terrain/RestPolicy" );

        // This is intentionally separate from the row solver. The rows above
        // handle physical contact response; this pass applies engine rest policy
        // only for manifolds that the terrain classifier marked as stable
        // support. That separation keeps unstable edge/corner terrain contacts
        // from gaining rolling damping or sleep privileges just because their
        // impact rows solved successfully.
        std::vector<uint8_t> terrainRestApplied( modelCount, 0 );
        for ( const Physics::TerrainContactManifold& manifold : m_terrainContactManifolds )
        {
            const int bodyIndex = manifold.bodyA;
            if ( bodyIndex < 0 ||
                 bodyIndex >= modelCount ||
                 terrainRestApplied[bodyIndex] ||
                 !manifold.supportsRestingPolicy ||
                 m_sleepState[bodyIndex] ||
                 m_soaIsFixed[bodyIndex] )
            {
                continue;
            }

            terrainRestApplied[bodyIndex] = 1;
            GameModel& model = m_gameModels[bodyIndex];
            SolverBodyState& body = m_solverBodies[bodyIndex];
            float normalForce = model.GetMass() * fabsf( Cfg().gravity ) * fabsf( manifold.normal.y );
            float omegaMagSq = body.angularVelocity * body.angularVelocity;
            if ( omegaMagSq > TOLERANCE * TOLERANCE )
            {
                // Approximate rolling friction as a torque opposite angular
                // velocity. The effective radius is exact for spheres and a
                // conservative average extent for boxes, enough to bleed tiny
                // residual spin without adding a shape-specific response path.
                float omegaMag = sqrtf( omegaMagSq );
                float rEff = std::visit( []( const auto& shape ) -> float
                                         {
                    using ShapeT = std::decay_t<decltype( shape )>;
                    if constexpr ( std::is_same_v<ShapeT, BoundingSphere> )
                    {
                        return shape.GetRadius();
                    }
                    else
                    {
                        const Vector3& he = shape.GetHalfExtents();
                        return ( he.x + he.y + he.z ) / 3.0f;
                    } },
                                         model.GetCollisionShape() );

                constexpr float muRolling = 0.02f;
                float rollingTorqueMag = muRolling * normalForce * rEff;
                const Vector3& inertia = model.GetRotationalInertia();
                float avgInertia = ( inertia.x + inertia.y + inertia.z ) / 3.0f;
                if ( avgInertia < TOLERANCE )
                {
                    avgInertia = 1.0f;
                }

                float deltaOmega = ( rollingTorqueMag / avgInertia ) * dt;
                if ( deltaOmega >= omegaMag )
                {
                    body.angularVelocity = ZERO_VECTOR;
                }
                else
                {
                    body.angularVelocity -= ( body.angularVelocity / omegaMag ) * deltaOmega;
                }
            }

            constexpr float sleepLinear = 0.05f;
            constexpr float sleepAngular = 0.02f;
            if ( ( body.linearVelocity * body.linearVelocity ) < sleepLinear * sleepLinear &&
                 ( body.angularVelocity * body.angularVelocity ) < sleepAngular * sleepAngular )
            {
                // Snap only near-zero supported motion. This avoids tiny solver
                // residue keeping a legitimately settled terrain body awake,
                // while leaving unsupported impacts and sliding bodies untouched.
                body.linearVelocity = ZERO_VECTOR;
                body.angularVelocity = ZERO_VECTOR;
            }
        }
    }

    {
        PROFILE_SCOPED( "Frame/Physics/Narrowphase/PersistentContacts/WriteBack" );
        for ( int i = 0; i < modelCount; ++i )
        {
            if ( m_sleepState[i] || m_soaIsFixed[i] )
            {
                continue;
            }

            Physics::PhysicsPipelineRecord record;
            record.stage = Physics::PhysicsPipelineStage::VelocityWriteback;
            record.bodyA = i;
            record.point = m_gameModels[i].GetPosition();
            record.scalarA = Vector::VectorMag( m_solverBodies[i].linearVelocity );
            record.scalarB = Vector::VectorMag( m_solverBodies[i].angularVelocity );
            RecordPhysicsPipelineStage( record );

            m_gameModels[i].SetLinearVelocity( m_solverBodies[i].linearVelocity );
            m_gameModels[i].SetAngularVelocity( m_solverBodies[i].angularVelocity );
        }
    }

    {
        PROFILE_SCOPED( "Frame/Physics/Narrowphase/PersistentContacts/DebugContacts" );
        m_physicsDebugContacts.clear();
        m_physicsDebugContacts.reserve( m_persistentContacts.size() );
        for ( const PersistentContact& c : m_persistentContacts )
        {
            if ( c.accN > 0.0f )
            {
                if ( m_gameModels[c.bodyA].IsFixed() )
                {
                    MarkFixedContact( c.bodyA );
                }
                if ( c.bodyB != TERRAIN_BODY_INDEX && m_gameModels[c.bodyB].IsFixed() )
                {
                    MarkFixedContact( c.bodyB );
                }
            }

            Physics::PhysicsDebugContact out;
            out.bodyA = c.bodyA;
            out.bodyB = c.bodyB;
            out.featureId = c.featureId;
            out.point = m_gameModels[c.bodyA].GetPosition() + c.rA;
            out.normal = c.isTerrain ? c.terrainNormal : c.normal;
            out.tangent1 = c.tangent1;
            out.tangent2 = c.tangent2;
            out.penetration = c.penetration;
            out.normalImpulse = c.accN;
            m_physicsDebugContacts.push_back( out );
        }
    }

    // ENGINE-SPECIFIC / NOVEL:
    //   Catto's Baumgarte bias handles overlap through velocity-level constraint
    //   correction. This partial positional correction is local cleanup for the
    //   current approximate object contacts.
    // Fourth pass: remove any visible leftover overlap. The velocity solver does
    // most of the work, but this direct correction keeps persistent contacts from
    // sinking deeper into each other over many frames.
    {
        PROFILE_SCOPED( "Frame/Physics/Narrowphase/PersistentContacts/PositionCorrection" );
        for ( const PersistentContact& c : m_persistentContacts )
        {
            const float rowContactSlop = c.isTerrain ? (std::max)( 0.0f, Cfg().terrainContactSlop ) : contactSlop;
            if ( c.penetration <= rowContactSlop )
            {
                continue;
            }

            GameModel& a = m_gameModels[c.bodyA];
            float invMassA = ( m_sleepState[c.bodyA] || a.IsFixed() ) ? 0.0f : a.GetInvertedMass();
            float invMassB = 0.0f;
            GameModel* b = nullptr;
            if ( c.bodyB != TERRAIN_BODY_INDEX )
            {
                b = &m_gameModels[c.bodyB];
                invMassB = ( m_sleepState[c.bodyB] || b->IsFixed() ) ? 0.0f : b->GetInvertedMass();
            }
            float totalInvMass = invMassA + invMassB;
            if ( totalInvMass <= TOLERANCE )
            {
                continue;
            }

            const float rowPositionCorrectionPercent = c.isTerrain ? 0.4f : positionCorrectionPercent;
            Vector3 correction = c.normal * ( ( c.penetration - rowContactSlop ) * rowPositionCorrectionPercent / totalInvMass );
            float correctionMagnitude = Vector::VectorMag( correction );
            ++m_persistentContactSolverStats.positionCorrectionRows;
            m_persistentContactSolverStats.positionCorrectionTotal += correctionMagnitude;
            if ( correctionMagnitude > m_persistentContactSolverStats.positionCorrectionMax )
            {
                m_persistentContactSolverStats.positionCorrectionMax = correctionMagnitude;
            }
            Physics::PhysicsPipelineRecord record;
            record.stage = Physics::PhysicsPipelineStage::PositionCorrection;
            record.bodyA = c.bodyA;
            record.bodyB = c.bodyB;
            record.featureId = c.featureId;
            record.point = a.GetPosition() + c.rA;
            record.normal = c.normal;
            record.scalarA = correctionMagnitude;
            record.scalarB = c.penetration;
            record.scalarC = rowContactSlop;
            RecordPhysicsPipelineStage( record );
            a.SetPosition( a.GetPosition() - correction * invMassA );
            if ( b )
            {
                b->SetPosition( b->GetPosition() + correction * invMassB );
            }
        }
    }

    // CATTO REF:
    //   Catto 2005, PDF pp. 18-19, Section 8.1 and Algorithm 5: destroy the old
    //   contact cache, create a new one, and store lambda plus the contact
    //   identifier for the next frame.
    // Final pass: store this frame's accumulated pushes for next frame. This is
    // why a settled stack can remain settled; it does not have to rediscover from
    // scratch how much support force each contact needs every tick.
    {
        PROFILE_SCOPED( "Frame/Physics/Narrowphase/PersistentContacts/CacheStore" );
        m_persistentContactCache.clear();
        for ( const PersistentContact& c : m_persistentContacts )
        {
            if ( c.isTerrain && !c.supportsRestingPolicy )
            {
                continue;
            }

            if ( c.accN <= 0.0f && fabsf( c.accT1 ) <= TOLERANCE && fabsf( c.accT2 ) <= TOLERANCE )
            {
                continue;
            }

            PersistentContactCacheEntry cached;
            cached.key = c.key;
            cached.accN = c.accN;
            cached.accT1 = c.accT1;
            cached.accT2 = c.accT2;
            m_persistentContactCache.push_back( cached );

            Physics::PhysicsPipelineRecord record;
            record.stage = Physics::PhysicsPipelineStage::CacheStore;
            record.bodyA = c.bodyA;
            record.bodyB = c.bodyB;
            record.featureId = c.featureId;
            record.point = m_gameModels[c.bodyA].GetPosition() + c.rA;
            record.normal = c.normal;
            record.scalarA = c.accN;
            record.scalarB = c.accT1;
            record.scalarC = c.accT2;
            RecordPhysicsPipelineStage( record );
        }

        if ( m_persistentContactCache.size() > 1 )
        {
            std::sort( m_persistentContactCache.begin(),
                       m_persistentContactCache.end(),
                       []( const PersistentContactCacheEntry& lhs, const PersistentContactCacheEntry& rhs )
                       {
                           return lhs.key < rhs.key;
                       } );
        }
    }
}


void GameModelCollection::PropagateSleepSupport()
{
    const int modelCount = static_cast<int>( m_gameModels.size() );
    if ( modelCount <= 0 || m_sleepSupportEdges.empty() )
    {
        return;
    }

    // Terrain seeds support. Object contacts only pass that support upward
    // through stack-like edges, keeping mid-air dynamic contacts out of sleep.
    // Sleeping bodies are treated as proven supports because they could only
    // enter sleep after a previous island pass found them quiet, uninhibited, and
    // connected to credible support.
    for ( int pass = 0; pass < modelCount; ++pass )
    {
        // The graph is frame-local and small, so bounded relaxation is simpler
        // than building another traversal structure. The early-out keeps common
        // one- and two-box stacks cheap.
        bool changed = false;
        for ( const auto& edge : m_sleepSupportEdges )
        {
            const int supporter = edge.first;
            const int supported = edge.second;
            if ( supporter < 0 || supporter >= modelCount || supported < 0 || supported >= modelCount )
            {
                continue;
            }

            bool supporterHasSupport = m_sleepSupportedThisFrame[supporter] != 0;
            if ( !supporterHasSupport && m_gameModels[supporter].IsFixed() )
            {
                supporterHasSupport = true;
            }
            if ( !supporterHasSupport &&
                 supporter < static_cast<int>( m_sleepState.size() ) &&
                 m_sleepState[supporter] != 0 )
            {
                supporterHasSupport = true;
            }

            if ( supporterHasSupport && m_sleepSupportedThisFrame[supported] == 0 )
            {
                m_sleepSupportedThisFrame[supported] = 1;
                changed = true;
            }
        }

        if ( !changed )
        {
            break;
        }
    }
}


// Physics tick: unified impulse solver for all object types (spheres and boxes).
// Object/object swept tests only create or advance contact candidates. Velocity
// response for object pairs is owned by SolvePersistentObjectContacts so no
// legacy one-shot impulse can compete with the Catto row pipeline.
void GameModelCollection::RunSolverPhysics( float dt )
{
    const int modelCount = static_cast<int>( m_gameModels.size() );

    // Sleep thresholds are config-backed because they directly trade CPU cost
    // against visible settling behavior. Higher thresholds keep bodies awake
    // longer, which is useful while validating the solver but expensive in
    // sleeping-heavy scenes. Lower thresholds save broadphase/narrowphase work
    // sooner, but if set too aggressively they can freeze objects before the
    // persistent contact solver has converged to a stable support impulse.
    //
    // The counter storage is still uint8_t, so physics_sleep_frames is clamped
    // to 1..255 here. Widening that storage is a separate data-layout change and
    // should be measured before doing it in a hot per-body array.
    const float sleepLinear = (std::max)( 0.0f, Cfg().physicsSleepLinearSpeed );
    const float sleepAngular = (std::max)( 0.0f, Cfg().physicsSleepAngularSpeed );
    const float SLEEP_LINEAR_SQ = sleepLinear * sleepLinear;
    const float SLEEP_ANGULAR_SQ = sleepAngular * sleepAngular;
    const uint8_t SLEEP_FRAMES = static_cast<uint8_t>( (std::max)( 1, (std::min)( Cfg().physicsSleepFrames, 255 ) ) );

    // Apply forces to awake models only
    PROFILE_BEGIN( "Frame/Physics/ApplyForces" );
    for ( int x = 0; x < modelCount; ++x )
    {
        if ( m_soaIsFixed[x] )
        {
            continue;
        }
        if ( m_sleepState[x] )
        {
            m_timeRemaining[x] = 0.0f;
            continue;
        }
        m_gameModels[x].ApplyForces( dt );
    }
    PROFILE_END( "Frame/Physics/ApplyForces" );

    // Broadphase: build spatial grid from all object positions (include sleeping for wake detection)
    PROFILE_BEGIN( "Frame/Physics/Broadphase" );
    m_spatialGrid.Clear();
    m_collisionCellKeys.clear();
    for ( int i = 0; i < modelCount; ++i )
    {
        const float radius = m_soaBoundingRadii[i];
        const Vector3 displacement = m_gameModels[i].GetVelocity() * dt;
        const float displacementSq = Vector::VectorMagSquared( displacement );
        if ( !m_soaIsFixed[i] && displacementSq > radius * radius )
        {
            m_spatialGrid.InsertSwept( i, m_soaPositions[i], displacement, radius );
        }
        else
        {
            m_spatialGrid.Insert( i, m_soaPositions[i], radius );
        }
    }
    std::vector<std::pair<int, int>>& candidatePairs = m_candidatePairs;
    m_spatialGrid.GetCandidatePairs( candidatePairs );
    for ( const auto& pair : candidatePairs )
    {
        if ( pair.first < 0 || pair.second < 0 || pair.first >= modelCount || pair.second >= modelCount )
        {
            continue;
        }

        Physics::PhysicsPipelineRecord record;
        record.stage = Physics::PhysicsPipelineStage::BroadphaseCandidate;
        record.bodyA = pair.first;
        record.bodyB = pair.second;
        record.point = ( m_gameModels[pair.first].GetPosition() + m_gameModels[pair.second].GetPosition() ) * 0.5f;
        Vector3 delta = m_gameModels[pair.second].GetPosition() - m_gameModels[pair.first].GetPosition();
        float deltaMag = Vector::VectorMag( delta );
        record.normal = deltaMag > TOLERANCE ? delta / deltaMag : Vector3( 0.0f, 1.0f, 0.0f );
        record.scalarA = static_cast<float>( candidatePairs.size() );
        RecordPhysicsPipelineStage( record );
    }
    {
        PROFILE_SCOPED( "Frame/Physics/Broadphase/PruneSleepPairs" );
        // The spatial grid is still populated with sleeping bodies because an
        // awake body must be able to find and wake a sleeping neighbor. What we
        // do not need is sleep/sleep work: two sleeping dynamic bodies cannot
        // generate a new wake event because neither has wake energy, and their
        // previous support relationship is already represented by sleep state
        // and island visual ids. Pruning these pairs immediately keeps both the
        // swept narrowphase and the persistent contact manifold builder from
        // re-checking pairs that would only be skipped later.
        //
        // This is deliberately narrower than a separate awake/sleeping grid.
        // The full partition is still a valid future optimization, but this
        // single pass removes the common dead work without changing pair
        // generation order for any pair that can affect simulation behavior.
        candidatePairs.erase(
            std::remove_if( candidatePairs.begin(),
                            candidatePairs.end(),
                            [&]( const std::pair<int, int>& pair )
                            {
                                const int a = pair.first;
                                const int b = pair.second;
                                const bool prune = a >= 0 && b >= 0 &&
                                                   a < static_cast<int>( m_sleepState.size() ) &&
                                                   b < static_cast<int>( m_sleepState.size() ) &&
                                                   m_sleepState[a] != 0 &&
                                                   m_sleepState[b] != 0;
                                if ( prune )
                                {
                                    Physics::PhysicsPipelineRecord record;
                                    record.stage = Physics::PhysicsPipelineStage::SleepPrunedPair;
                                    record.bodyA = a;
                                    record.bodyB = b;
                                    record.point = ( m_gameModels[a].GetPosition() + m_gameModels[b].GetPosition() ) * 0.5f;
                                    record.scalarA = 1.0f;
                                    RecordPhysicsPipelineStage( record );
                                }
                                return prune;
                            } ),
            candidatePairs.end() );
    }
    PROFILE_END( "Frame/Physics/Broadphase" );

    auto hasWakeEnergy = [&]( int awakeIndex ) -> bool
    {
        const Vector3& vel = m_gameModels[awakeIndex].GetVelocity();
        const Vector3& omega = m_gameModels[awakeIndex].GetAngularVelocity();
        float speedSq = vel.x * vel.x + vel.y * vel.y + vel.z * vel.z;
        float omegaSq = omega.x * omega.x + omega.y * omega.y + omega.z * omega.z;
        return speedSq >= SLEEP_LINEAR_SQ || omegaSq >= SLEEP_ANGULAR_SQ;
    };

    auto wakeSleepingModel = [&]( int sleepingIndex )
    {
        // Waking re-enters the body into this frame rather than waiting for the
        // next tick. Applying forces immediately keeps gravity and other forces
        // consistent with an awake body that was never asleep.
        if ( sleepingIndex < 0 || sleepingIndex >= modelCount || m_soaIsFixed[sleepingIndex] || !m_sleepState[sleepingIndex] )
        {
            return;
        }

        m_sleepState[sleepingIndex] = 0;
        m_sleepCounter[sleepingIndex] = 0;
        m_sleepIslandVisualId[sleepingIndex] = 0;
        m_timeRemaining[sleepingIndex] = dt;
        m_gameModels[sleepingIndex].ApplyForces( dt );
    };

    auto hasPersistentWakeContact = [&]( int awakeIndex, int sleepingIndex ) -> bool
    {
        // A swept test can miss a sleeper that is already overlapping after an
        // awake body's correction step. This fresh manifold test catches that
        // persistent contact so the sleeper cannot remain frozen inside the
        // awake body until a later frame happens to generate a swept hit.
        ObjectContactManifold manifold;
        return BuildObjectContactManifold( m_gameModels[awakeIndex],
                                           m_gameModels[sleepingIndex],
                                           awakeIndex,
                                           sleepingIndex,
                                           Cfg().contactEpsilon,
                                           manifold );
    };

    auto hasObjectContactAtTime = [&]( int a, int b, float time ) -> bool
    {
        // Temporarily place both bodies at a candidate time, ask the exact
        // narrowphase whether they touch there, then restore positions. This is
        // a query only; it must leave the world exactly as it found it.
        const Vector3 startA = m_gameModels[a].GetPosition();
        const Vector3 startB = m_gameModels[b].GetPosition();
        m_gameModels[a].SetPosition( startA + m_gameModels[a].GetVelocity() * time );
        m_gameModels[b].SetPosition( startB + m_gameModels[b].GetVelocity() * time );

        ObjectContactManifold manifold;
        const bool hit = BuildObjectContactManifold( m_gameModels[a],
                                                     m_gameModels[b],
                                                     a,
                                                     b,
                                                     Cfg().contactEpsilon,
                                                     manifold );

        m_gameModels[a].SetPosition( startA );
        m_gameModels[b].SetPosition( startB );
        return hit;
    };

    auto refineObjectSweepContactTime = [&]( int a, int b, float coarseTime, float availableTime ) -> float
    {
        // The broad sweep can give a conservative first time. Refinement walks
        // forward until exact manifold contact appears, then binary-searches the
        // edge of that contact window. This keeps fast objects from advancing
        // too far into each other before persistent rows solve the response.
        if ( coarseTime <= 0.0f || coarseTime >= availableTime )
        {
            return coarseTime;
        }

        if ( hasObjectContactAtTime( a, b, coarseTime ) )
        {
            return coarseTime;
        }

        float lo = coarseTime;
        float hi = coarseTime;
        bool foundContactWindow = false;
        for ( int step = 1; step <= 48; ++step )
        {
            const float t = coarseTime + ( availableTime - coarseTime ) * ( static_cast<float>( step ) / 48.0f );
            if ( hasObjectContactAtTime( a, b, t ) )
            {
                hi = t;
                foundContactWindow = true;
                break;
            }
            lo = t;
        }

        if ( !foundContactWindow )
        {
            return coarseTime;
        }

        for ( int iter = 0; iter < 12; ++iter )
        {
            const float mid = ( lo + hi ) * 0.5f;
            if ( hasObjectContactAtTime( a, b, mid ) )
            {
                hi = mid;
            }
            else
            {
                lo = mid;
            }
        }
        return hi;
    };

    // Object/object CCD front-end: wake sleepers and advance swept hits to a
    // contact candidate, but leave velocity response to the persistent rows.
    PROFILE_BEGIN( "Frame/Physics/Narrowphase" );
    float invCellSize = 1.0f / m_spatialGrid.GetCellSize();
    for ( const auto& cp : candidatePairs )
    {
        int x = cp.first;
        int y = cp.second;

        // Wake a sleeping object only after an energetic awake neighbor proves
        // an actual swept hit or persistent overlap.
        if ( m_sleepState[x] || m_sleepState[y] )
        {
            // Quiet awake bodies cannot wake sleepers just by sharing a broadphase cell.
            if ( m_sleepState[x] && !m_sleepState[y] )
            {
                if ( !hasWakeEnergy( y ) )
                {
                    continue;
                }
                // Swept impact wakes immediately when time remains; persistent
                // overlap wakes too so sleepers cannot stay frozen after a hit.
                bool wokeBySweptImpact = false;
                if ( m_timeRemaining[y] > 0.0f )
                {
                    GameModel::ObjectSweepResult sweep = m_gameModels[y].SweepGameModel( m_gameModels[x], m_timeRemaining[y] );
                    if ( sweep.hit )
                    {
                        float colTime = refineObjectSweepContactTime( y, x, sweep.collisionTime, m_timeRemaining[y] );
                        Physics::PhysicsPipelineRecord record;
                        record.stage = Physics::PhysicsPipelineStage::SweptObjectHit;
                        record.bodyA = y;
                        record.bodyB = x;
                        record.point = ( m_gameModels[y].GetPosition() + m_gameModels[x].GetPosition() ) * 0.5f;
                        record.scalarA = colTime;
                        record.scalarB = m_timeRemaining[y];
                        RecordPhysicsPipelineStage( record );
                        EmitPhysicsCollisionTime( "object", y, x, colTime, m_timeRemaining[y] );

                        m_gameModels[y].UpdatePosition( colTime );
                        m_timeRemaining[y] = (std::max)( 0.0f, m_timeRemaining[y] - colTime );
                        wakeSleepingModel( x );
                        wokeBySweptImpact = true;
                        MarkCollisionVisualContact( x );
                        MarkCollisionVisualContact( y );
                    }
                }
                if ( !wokeBySweptImpact && hasPersistentWakeContact( y, x ) )
                {
                    Physics::PhysicsPipelineRecord record;
                    record.stage = Physics::PhysicsPipelineStage::WakeDecision;
                    record.bodyA = y;
                    record.bodyB = x;
                    record.point = ( m_gameModels[y].GetPosition() + m_gameModels[x].GetPosition() ) * 0.5f;
                    record.scalarA = 1.0f;
                    RecordPhysicsPipelineStage( record );

                    wakeSleepingModel( x );
                    MarkCollisionVisualContact( x );
                    MarkCollisionVisualContact( y );
                }
                continue;
            }
            else if ( m_sleepState[y] && !m_sleepState[x] )
            {
                if ( !hasWakeEnergy( x ) )
                {
                    continue;
                }
                bool wokeBySweptImpact = false;
                if ( m_timeRemaining[x] > 0.0f )
                {
                    GameModel::ObjectSweepResult sweep = m_gameModels[x].SweepGameModel( m_gameModels[y], m_timeRemaining[x] );
                    if ( sweep.hit )
                    {
                        float colTime = refineObjectSweepContactTime( x, y, sweep.collisionTime, m_timeRemaining[x] );
                        Physics::PhysicsPipelineRecord record;
                        record.stage = Physics::PhysicsPipelineStage::SweptObjectHit;
                        record.bodyA = x;
                        record.bodyB = y;
                        record.point = ( m_gameModels[x].GetPosition() + m_gameModels[y].GetPosition() ) * 0.5f;
                        record.scalarA = colTime;
                        record.scalarB = m_timeRemaining[x];
                        RecordPhysicsPipelineStage( record );
                        EmitPhysicsCollisionTime( "object", x, y, colTime, m_timeRemaining[x] );

                        m_gameModels[x].UpdatePosition( colTime );
                        m_timeRemaining[x] = (std::max)( 0.0f, m_timeRemaining[x] - colTime );
                        wakeSleepingModel( y );
                        wokeBySweptImpact = true;
                        MarkCollisionVisualContact( x );
                        MarkCollisionVisualContact( y );
                    }
                }
                if ( !wokeBySweptImpact && hasPersistentWakeContact( x, y ) )
                {
                    Physics::PhysicsPipelineRecord record;
                    record.stage = Physics::PhysicsPipelineStage::WakeDecision;
                    record.bodyA = x;
                    record.bodyB = y;
                    record.point = ( m_gameModels[x].GetPosition() + m_gameModels[y].GetPosition() ) * 0.5f;
                    record.scalarA = 1.0f;
                    RecordPhysicsPipelineStage( record );

                    wakeSleepingModel( y );
                    MarkCollisionVisualContact( x );
                    MarkCollisionVisualContact( y );
                }
                continue;
            }
            else
            {
                // Both bodies are sleeping; there is no awake energy to produce a wake event.
                continue;
            }
        }

        if ( m_timeRemaining[x] <= 0.0f || m_timeRemaining[y] <= 0.0f )
        {
            continue;
        }

        float availableTime = (std::min)( m_timeRemaining[x], m_timeRemaining[y] );
        GameModel::ObjectSweepResult sweep = m_gameModels[x].SweepGameModel( m_gameModels[y], availableTime );

        if ( sweep.hit )
        {
            float colTime = refineObjectSweepContactTime( x, y, sweep.collisionTime, availableTime );
            Physics::PhysicsPipelineRecord record;
            record.stage = Physics::PhysicsPipelineStage::SweptObjectHit;
            record.bodyA = x;
            record.bodyB = y;
            record.point = ( m_gameModels[x].GetPosition() + m_gameModels[y].GetPosition() ) * 0.5f;
            record.scalarA = colTime;
            record.scalarB = availableTime;
            RecordPhysicsPipelineStage( record );
            EmitPhysicsCollisionTime( "object", x, y, colTime, availableTime );

            m_gameModels[x].UpdatePosition( colTime );
            m_gameModels[y].UpdatePosition( colTime );
            m_timeRemaining[x] = (std::max)( 0.0f, m_timeRemaining[x] - colTime );
            m_timeRemaining[y] = (std::max)( 0.0f, m_timeRemaining[y] - colTime );

            // Object/object CCD only advances to the contact candidate. The
            // persistent Catto rows below own velocity response and cache storage.
            MarkCollisionVisualContact( x );
            MarkCollisionVisualContact( y );

            // Record collision cell for broadphase visualizer
            Vector3 midpoint = ( m_gameModels[x].GetPosition() + m_gameModels[y].GetPosition() ) * 0.5f;
            int16_t cx = (int16_t)floorf( midpoint.x * invCellSize );
            int16_t cy = (int16_t)floorf( midpoint.y * invCellSize );
            int16_t cz = (int16_t)floorf( midpoint.z * invCellSize );
            int64_t key = ( int64_t( cx ) * 73856093 ) ^ ( int64_t( cy ) * 19349663 ) ^ ( int64_t( cz ) * 83492791 );
            m_collisionCellKeys.push_back( key );
        }
        else
        {
            Physics::PhysicsPipelineRecord record;
            record.stage = Physics::PhysicsPipelineStage::SweptObjectMiss;
            record.bodyA = x;
            record.bodyB = y;
            record.point = ( m_gameModels[x].GetPosition() + m_gameModels[y].GetPosition() ) * 0.5f;
            record.scalarA = availableTime;
            RecordPhysicsPipelineStage( record );
        }
    }
    PROFILE_END( "Frame/Physics/Narrowphase" );

    // Terrain phase ownership:
    //   1. Keep swept terrain detection here so fast bodies still stop at the
    //      correct time of impact.
    //   2. Convert the hit into a terrain manifold only. Do not apply impulses
    //      or terrain-only velocity response in this phase.
    //   3. Leave remaining-time integration and all normal/friction response to
    //      the shared persistent contact rows below.
    PROFILE_BEGIN( "Frame/Physics/Terrain" );
    PROFILE_BEGIN( "Frame/Physics/Terrain/Detect" );
    for ( int x = 0; x < modelCount; ++x )
    {
        if ( m_soaIsFixed[x] )
        {
            continue;
        }
        if ( m_sleepState[x] || m_timeRemaining[x] <= 0.0f )
        {
            continue;
        }

        float availableTime = m_timeRemaining[x];
        float colTime = m_gameModels[x].CollisionDetectTerrain( availableTime );

        if ( m_gameModels[x].IsResponseRequired() )
        {
            m_gameModels[x].UpdatePosition( colTime );
            const float remainingTime = (std::max)( 0.0f, availableTime - colTime );
            // BuildTerrainContactManifold is the handoff from terrain-specific
            // collision data to solver-neutral contact geometry. The old
            // response-required flag is now just a detection latch; clear it
            // once the manifold is captured so no later path can replay terrain
            // response work.
            Physics::TerrainContactManifold manifold;
            const bool hasManifold = m_gameModels[x].BuildTerrainContactManifold( x, colTime, availableTime, manifold );
            m_gameModels[x].ClearResponseRequired();

            Physics::PhysicsPipelineRecord record;
            record.stage = Physics::PhysicsPipelineStage::TerrainHit;
            record.bodyA = x;
            record.bodyB = TERRAIN_BODY_INDEX;
            record.point = hasManifold ? manifold.points[0].point : m_gameModels[x].GetPosition();
            record.normal = hasManifold ? manifold.normal : ZERO_VECTOR;
            record.scalarA = colTime;
            record.scalarB = hasManifold && manifold.supportsRestingPolicy ? 1.0f : 0.0f;
            record.scalarC = hasManifold ? static_cast<float>( manifold.pointCount ) : 0.0f;
            RecordPhysicsPipelineStage( record );
            EmitPhysicsCollisionTime( "terrain", x, -1, colTime, availableTime );

            if ( hasManifold )
            {
                m_terrainContactManifolds.push_back( manifold );
                if ( manifold.supportsRestingPolicy )
                {
                    m_sleepSupportedThisFrame[x] = 1;
                }
                else
                {
                    m_sleepInhibitedThisFrame[x] = 1;
                }
            }
            else
            {
                m_sleepInhibitedThisFrame[x] = 1;
            }
            MarkCollisionVisualContact( x );
            m_timeRemaining[x] = remainingTime;
        }
    }
    PROFILE_END( "Frame/Physics/Terrain/Detect" );
    PROFILE_END( "Frame/Physics/Terrain" );

    SolvePersistentObjectContacts( dt );
    // Object contacts are converted into stack support only after terrain
    // response has had a chance to seed true support for this frame.
    PropagateSleepSupport();

    // Integrate remaining time for awake models
    PROFILE_BEGIN( "Frame/Physics/Integrate" );
    for ( int x = 0; x < modelCount; ++x )
    {
        if ( m_soaIsFixed[x] )
        {
            continue;
        }
        if ( m_sleepState[x] )
        {
            continue;
        }

        if ( m_timeRemaining[x] > 0.0f )
        {
            m_gameModels[x].UpdatePosition( m_timeRemaining[x] );
        }
    }

    // Build sleep islands from the persistent contact graph. Sleep counters are
    // tracked per body, but the final transition is island-level: connected awake
    // bodies deactivate together only if the whole island is quiet and rooted in
    // credible support.
    //
    // Important nuance:
    //   "Supported" is an island property, not a demand that every body directly
    //   touch terrain. A box can be quiet and physically constrained by the side
    //   of a grounded pile. Requiring that specific box to also pass terrain
    //   support classification creates the bad varied-scene wedge: terrain says
    //   "not a stable footprint", object contacts keep the box from falling, and
    //   the sleep gate has no way out. The anchor pass below keeps the original
    //   safety rule for floating/mid-air islands: at least one member must still
    //   be terrain-supported, fixed, or already sleeping from a previous proven
    //   support state.
    m_sleepIslandParent.assign( modelCount, 0 );
    m_sleepIslandRank.assign( modelCount, 0 );
    m_sleepIslandHasAwake.assign( modelCount, 0 );
    m_sleepIslandHasSupportAnchor.assign( modelCount, 0 );
    m_sleepIslandEligible.assign( modelCount, 1 );
    m_sleepIslandCanSleep.assign( modelCount, 1 );
    for ( int i = 0; i < modelCount; ++i )
    {
        m_sleepIslandParent[i] = i;
    }

    auto findIsland = [&]( int index ) -> int
    {
        // Union-find lookup with path compression. In plain terms: every body in
        // a connected contact group points to the same representative root, so
        // the sleep system can make one decision for the whole group.
        int root = index;
        while ( m_sleepIslandParent[root] != root )
        {
            root = m_sleepIslandParent[root];
        }
        while ( m_sleepIslandParent[index] != index )
        {
            int parent = m_sleepIslandParent[index];
            m_sleepIslandParent[index] = root;
            index = parent;
        }
        return root;
    };

    auto unionIslands = [&]( int a, int b )
    {
        // Merge two contact groups. Rank keeps the tree shallow so repeated
        // findIsland calls stay cheap during large stacks.
        int rootA = findIsland( a );
        int rootB = findIsland( b );
        if ( rootA == rootB )
        {
            return;
        }

        if ( m_sleepIslandRank[rootA] < m_sleepIslandRank[rootB] )
        {
            std::swap( rootA, rootB );
        }
        m_sleepIslandParent[rootB] = rootA;
        if ( m_sleepIslandRank[rootA] == m_sleepIslandRank[rootB] )
        {
            ++m_sleepIslandRank[rootA];
        }
    };

    for ( const PersistentContact& c : m_persistentContacts )
    {
        // Persistent contacts are the solver's current dynamic contact graph, so
        // they are the natural edges for island sleep. Sleeping bodies still act
        // as graph anchors, but only awake bodies below participate in the current
        // eligibility and counter checks.
        if ( c.bodyA >= 0 && c.bodyA < modelCount && c.bodyB >= 0 && c.bodyB < modelCount )
        {
            unionIslands( c.bodyA, c.bodyB );
        }
    }

    for ( int x = 0; x < modelCount; ++x )
    {
        const int root = findIsland( x );

        // A support anchor is evidence that this island is not a free-floating
        // collection of bodies that merely became numerically quiet. Terrain
        // support remains the usual anchor. Fixed objects and sleeping bodies are
        // also valid anchors: fixed objects are immovable world geometry, and a
        // sleeping dynamic body could only have reached sleep after satisfying the
        // same support gate in an earlier frame.
        if ( m_soaIsFixed[x] ||
             ( x < static_cast<int>( m_sleepState.size() ) && m_sleepState[x] != 0 ) ||
             ( x < static_cast<int>( m_sleepSupportedThisFrame.size() ) && m_sleepSupportedThisFrame[x] != 0 ) )
        {
            m_sleepIslandHasSupportAnchor[root] = 1;
        }
    }

    for ( int x = 0; x < modelCount; ++x )
    {
        if ( m_soaIsFixed[x] )
        {
            continue;
        }
        if ( m_sleepState[x] )
        {
            continue;
        }

        const int root = findIsland( x );
        m_sleepIslandHasAwake[root] = 1;

        const Vector3& vel = m_gameModels[x].GetVelocity();
        const Vector3& omega = m_gameModels[x].GetAngularVelocity();
        float speedSq = vel.x * vel.x + vel.y * vel.y + vel.z * vel.z;
        float omegaSq = omega.x * omega.x + omega.y * omega.y + omega.z * omega.z;
        bool quiet = speedSq < SLEEP_LINEAR_SQ && omegaSq < SLEEP_ANGULAR_SQ;
        bool supported = x < static_cast<int>( m_sleepSupportedThisFrame.size() ) && m_sleepSupportedThisFrame[x] != 0;
        bool hasObjectContact = x < static_cast<int>( m_persistentContactCounts.size() ) && m_persistentContactCounts[x] > 0;
        bool islandHasSupportAnchor = m_sleepIslandHasSupportAnchor[root] != 0;

        // A quiet body in a grounded object-contact island is supported even if
        // the body itself is side-wedged or touching terrain on an edge/point.
        // This is deliberately narrower than "any contact means support":
        //
        //   * quiet keeps active impacts and real toppling awake;
        //   * hasObjectContact requires the body to be constrained by the island;
        //   * islandHasSupportAnchor keeps floating piles from becoming sleepers.
        //
        // Marking the body supported here also keeps SkullScope diagnostics honest:
        // the body is not terrain-supported, but it is supported for deactivation
        // by a contact island rooted in credible support.
        if ( !supported && quiet && hasObjectContact && islandHasSupportAnchor )
        {
            m_sleepSupportedThisFrame[x] = 1;
            supported = true;
        }

        // Terrain can still inhibit sleep for edge/point contacts when that
        // contact is the only apparent support. In a quiet anchored island,
        // though, the same terrain rejection must not be an infinite veto: the
        // object solver may have wedged the body against neighbors so it cannot
        // fall into a more stable footprint. The island anchor and object-contact
        // checks above are the escape hatch for that exact low-energy state.
        bool terrainInhibitBlocksSleep = m_sleepInhibitedThisFrame[x] != 0 &&
                                         !( quiet && hasObjectContact && islandHasSupportAnchor );

        // Modern sleep is still velocity based, but Skullbonez also requires
        // credible island support so unsupported gravity bodies cannot become
        // numerically quiet for a few frames while visibly floating.
        if ( !quiet || !supported || terrainInhibitBlocksSleep )
        {
            m_sleepIslandEligible[root] = 0;
        }

        Physics::PhysicsPipelineRecord record;
        record.stage = Physics::PhysicsPipelineStage::SleepIslandDecision;
        record.bodyA = x;
        record.bodyB = root;
        record.point = m_gameModels[x].GetPosition();
        record.scalarA = quiet ? 1.0f : 0.0f;
        record.scalarB = supported ? 1.0f : 0.0f;
        record.scalarC = terrainInhibitBlocksSleep ? 1.0f : 0.0f;
        RecordPhysicsPipelineStage( record );
    }

    if ( !m_sleepEnabled )
    {
        std::fill( m_sleepCounter.begin(), m_sleepCounter.end(), static_cast<uint8_t>( 0 ) );
        m_sleepIslandCanSleep.assign( modelCount, 0 );
        m_sleepIslandAssignedVisualId.assign( modelCount, 0 );
        PROFILE_END( "Frame/Physics/Integrate" );
        return;
    }

    for ( int x = 0; x < modelCount; ++x )
    {
        if ( m_soaIsFixed[x] )
        {
            continue;
        }
        if ( m_sleepState[x] )
        {
            continue;
        }

        const int root = findIsland( x );
        if ( m_sleepIslandHasAwake[root] && m_sleepIslandEligible[root] )
        {
            if ( m_sleepCounter[x] < SLEEP_FRAMES )
            {
                ++m_sleepCounter[x];
            }
        }
        else
        {
            m_sleepCounter[x] = 0;
        }
    }

    for ( int x = 0; x < modelCount; ++x )
    {
        if ( m_soaIsFixed[x] )
        {
            continue;
        }
        if ( m_sleepState[x] )
        {
            continue;
        }

        const int root = findIsland( x );
        if ( m_sleepCounter[x] < SLEEP_FRAMES )
        {
            // Every awake body in an eligible island must accumulate the full
            // quiet-frame count before any body in that island is deactivated.
            m_sleepIslandCanSleep[root] = 0;
        }
    }

    m_sleepIslandAssignedVisualId.assign( modelCount, 0 );
    for ( int x = 0; x < modelCount; ++x )
    {
        if ( m_soaIsFixed[x] )
        {
            continue;
        }
        if ( !m_sleepState[x] || m_sleepIslandVisualId[x] == 0 )
        {
            continue;
        }

        const int root = findIsland( x );
        if ( m_sleepIslandAssignedVisualId[root] == 0 )
        {
            m_sleepIslandAssignedVisualId[root] = m_sleepIslandVisualId[x];
        }
    }

    for ( int x = 0; x < modelCount; ++x )
    {
        if ( m_soaIsFixed[x] )
        {
            continue;
        }
        if ( m_sleepState[x] )
        {
            continue;
        }

        const int root = findIsland( x );
        if ( m_sleepIslandHasAwake[root] && m_sleepIslandEligible[root] && m_sleepIslandCanSleep[root] )
        {
            if ( m_sleepIslandAssignedVisualId[root] == 0 )
            {
                m_sleepIslandAssignedVisualId[root] = m_nextSleepIslandVisualId++;
                if ( m_nextSleepIslandVisualId <= 0 )
                {
                    m_nextSleepIslandVisualId = 1;
                }
            }
            m_sleepState[x] = 1;
            m_sleepIslandVisualId[x] = m_sleepIslandAssignedVisualId[root];
            Physics::PhysicsPipelineRecord record;
            record.stage = Physics::PhysicsPipelineStage::SleepIslandDecision;
            record.bodyA = x;
            record.bodyB = root;
            record.point = m_gameModels[x].GetPosition();
            record.scalarA = 1.0f;
            record.scalarB = static_cast<float>( m_sleepIslandAssignedVisualId[root] );
            record.scalarC = static_cast<float>( m_sleepCounter[x] );
            RecordPhysicsPipelineStage( record );
            // Zeroing velocities at the island sleep transition prevents tiny
            // residual solver drift from reappearing when the body later wakes.
            m_gameModels[x].SetLinearVelocity( Math::Vector::ZERO_VECTOR );
            m_gameModels[x].SetAngularVelocity( Math::Vector::ZERO_VECTOR );
        }
    }
    PROFILE_END( "Frame/Physics/Integrate" );
}


void GameModelCollection::BuildShadowMesh()
{
    // Shadow disc rendered as a single quad (-1..+1 in XZ, Y=0).
    // The fragment shader discards pixels outside the unit circle (length(uv) > 1.0)
    // and applies a smooth per-pixel radial fade — no triangle fan needed.
    //
    // 2 triangles = 6 vertices (vs the old 16-segment fan = 48 vertices).
    // At 300 shadowed models this saves 4,200 triangles per frame.
    //
    //  (-1,0,-1)-------(1,0,-1)
    //      |          /    |
    //      |        /      |
    //      |      /        |
    //  (-1,0, 1)-------(1,0, 1)
    //
    static const float verts[] =
        {
            // Triangle 1
            -1.0f,
            0.0f,
            -1.0f,
            1.0f,
            0.0f,
            -1.0f,
            -1.0f,
            0.0f,
            1.0f,
            // Triangle 2
            1.0f,
            0.0f,
            -1.0f,
            1.0f,
            0.0f,
            1.0f,
            -1.0f,
            0.0f,
            1.0f,
        };
    m_shadowDiscVertexCount = 6;

    // Instance layout: 5 attributes (4×vec4 for mat4 + 1×float for alpha), starting at location 3
    int instanceAttribSizes[] = { 4, 4, 4, 4, 1 };
    m_shadowInstMesh = Gfx().CreateInstancedMesh( verts, m_shadowDiscVertexCount, 3, MAX_GAME_MODELS, SHADOW_INSTANCE_FLOATS, 3, instanceAttribSizes, 5 );

    // Create shader
    m_shadowShader = Gfx().CreateShader( "shaders/shadow" );
}


void GameModelCollection::ResetRenderResources()
{
    m_shadowShader.reset();
    if ( m_shadowInstMesh )
    {
        Gfx().DestroyInstancedMesh( m_shadowInstMesh );
        m_shadowInstMesh = 0;
    }
    m_shadowDiscVertexCount = 0;
}


bool GameModelCollection::SaveSceneSnapshot( const char* path, bool physicsOn, bool textOn, Environment::WorldEnvironment& worldEnv, const Vector3& camEye, const Vector3& camView, const Vector3& camUp )
{
    FILE* f = nullptr;
    if ( fopen_s( &f, path, "w" ) != 0 || !f )
    {
        return false;
    }

    fprintf( f, "# Snapshot — %d balls\n", static_cast<int>( m_gameModels.size() ) );
    fprintf( f, "physics %s\n", physicsOn ? "on" : "off" );
    fprintf( f, "text %s\n", textOn ? "on" : "off" );
    fprintf( f, "frames unlimited\n" );
    fprintf( f, "world %f %f %f\n", worldEnv.GetGravity(), worldEnv.GetFluidSurfaceHeight(), worldEnv.GetFluidDensity() );
    fprintf( f, "camera main  %.4f %.4f %.4f  %.4f %.4f %.4f  %.4f %.4f %.4f\n", camEye.x, camEye.y, camEye.z, camView.x, camView.y, camView.z, camUp.x, camUp.y, camUp.z );
    fprintf( f, "\n" );

    for ( int i = 0; i < static_cast<int>( m_gameModels.size() ); ++i )
    {
        const char* name = m_gameModels[i].GetName();
        char safeName[64];
        if ( !name[0] )
        {
            sprintf_s( safeName, sizeof( safeName ), "_ball_%d", i );
            name = safeName;
        }

        const Vector3& pos = m_gameModels[i].GetPosition();
        const Vector3& vel = m_gameModels[i].GetVelocity();
        const Vector3& avel = m_gameModels[i].GetAngularVelocity();
        const Vector3& ri = m_gameModels[i].GetRotationalInertia();
        float qx, qy, qz, qw;
        m_gameModels[i].GetOrientation().GetComponents( qx, qy, qz, qw );
        float r = m_gameModels[i].GetBoundingRadius();
        float mass = m_gameModels[i].GetMass();
        float rest = m_gameModels[i].GetCoefficientRestitution();

        fprintf( f,
                 "ball_state %s  %.6f %.6f %.6f  %.6f %.6f %.6f  %.6f %.6f %.6f"
                 "  %.8f %.8f %.8f %.8f  %.4f %.4f %.4f  %.4f %.4f %.4f\n",
                 name,
                 pos.x,
                 pos.y,
                 pos.z,
                 vel.x,
                 vel.y,
                 vel.z,
                 avel.x,
                 avel.y,
                 avel.z,
                 qx,
                 qy,
                 qz,
                 qw,
                 r,
                 mass,
                 rest,
                 ri.x,
                 ri.y,
                 ri.z );
    }

    fclose( f );
    return true;
}
