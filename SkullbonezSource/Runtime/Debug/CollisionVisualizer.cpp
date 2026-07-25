/*
File: SkullbonezSource/Runtime/Debug/CollisionVisualizer.cpp
Purpose:
  Builds debug drawing for collision shapes and contact diagnostics.

Summary:
  CollisionVisualizer.cpp builds debug drawing for collision shapes and
  contact diagnostics. As an implementation unit, keep edits anchored on
  deterministic physics, diagnostics, or world-state flow and on the
  glossary/invariants below.

Glossary:
  AABB (Axis-Aligned Bounding Box): Box aligned to world axes, used as a cheap
  broadphase/debug volume.
  Broadphase: Cheap collision pass that finds object pairs worth testing more
  precisely.
  Narrowphase: Precise collision pass that computes contact points, normals,
  and penetration.
  Manifold: Set of contact points and normals describing one colliding pair.
  Resource builder: Cold renderer owner borrowed only while compiling the
    collision shader.
  Geometry owner: Renderer owner borrowed while creating or destroying debug
    vertex and instance buffers.
  Render command context: Renderer capability borrowed only while drawing a
    collision-visualizer frame.
  Render diagnostics: Renderer capability borrowed to name child draw-trace
    scopes without reopening global renderer access.

Invariants:
  - Physics-visible behavior must remain deterministic; byte-exact baselines
  are the validation contract.

Related:
  - SkullbonezSource/Runtime/Debug/CollisionVisualizer.h
  - Agentic/Reference/physics-overview.md
  - Agentic/Reference/comment-style-guide.md
*/
// =============================================================================
// COLLISION VISUALIZER (CollisionVisualizer.cpp)
// =============================================================================
//
// Debug renderer for the V-key collision/sleep-state view.
//
// This path deliberately stays separate from normal textured model rendering:
//   - normal rendering draws material-like balls and boxes;
//   - collision visualization draws the same collision volumes with state colors.
//
// The primitive coordinates are shared through PrimitiveMeshBuilder.h so
// sphere tessellation, box winding, and local orientation cannot drift between the
// two renderers. GPU resources, shaders, and instance payloads remain separate
// because this renderer needs a per-instance color in addition to each model matrix.
// =============================================================================


#include "CollisionVisualizer.h"
#include "../../Assets/AssetKeys.h"
#include "../../Assets/AssetSystem.h"
#include "../../Rendering/RenderCommandTypes.h"
#include "../../Rendering/DX12/Dx12Diagnostics.h"
#include "../../Rendering/DX12/Dx12ResourceBuilder.h"
#include "../../Rendering/DX12/RenderBackendDX12.h"
#include "../../Rendering/RenderInstanceStore.h"
#include "../../Physics/ColliderStore.h"
#include "../../Physics/ConvexHullShape.h"
#include "../../Physics/PhysicsBodyStore.h"
#include "../../Rendering/PrimitiveMeshBuilder.h"
#include "../../Core/Common.h"

#include <algorithm>
#include <variant>


using namespace SkullbonezCore::Physics;
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Math::Vector;
using namespace SkullbonezCore::Rendering;

namespace
{
constexpr PassRasterStateBucket COLLISION_OPAQUE_RASTER = MakePassRasterStateBucket( 0, { true, true, false } );
constexpr PassRasterStateBucket COLLISION_TRANSLUCENT_RASTER = MakePassRasterStateBucket(
    1,
    { true, false, true, BlendFactor::SrcAlpha, BlendFactor::OneMinusSrcAlpha } );

// Why: the legacy DRAW_CALL_TRACE_SCOPE macro still reaches through the global
// renderer accessor. This local scope records the same child labels through the
// frame-owned diagnostics facet that CollisionVisualizer already borrows for
// rendering.
class CollisionVisualizerTraceScope
{
  public:
    CollisionVisualizerTraceScope( Dx12Diagnostics& renderDiagnostics, const char* name )
        : m_renderDiagnostics( renderDiagnostics ), m_hash( HashStr( name ) )
    {
        m_renderDiagnostics.PushDrawCallTraceScope( name, m_hash );
    }

    ~CollisionVisualizerTraceScope()
    {
        m_renderDiagnostics.PopDrawCallTraceScope( m_hash );
    }

    CollisionVisualizerTraceScope( const CollisionVisualizerTraceScope& ) = delete;
    CollisionVisualizerTraceScope& operator=( const CollisionVisualizerTraceScope& ) = delete;

  private:
    Dx12Diagnostics& m_renderDiagnostics;
    uint32_t m_hash = 0;
};
} // namespace

CollisionVisualizer::CollisionVisualizer()
{
    // Runtime allocation policy: the debug visualizer mirrors model state every
    // frame when enabled. Reserve its per-model and instance staging buffers up
    // front so diagnostics cannot grow heap storage during steady gameplay.
    m_models.reserve( SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS );
    m_sleepGroupSizes.reserve( SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS );
    m_sphereInstanceData.reserve( static_cast<std::size_t>( SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS ) *
                                  INSTANCE_FLOATS );

    m_boxInstanceData.reserve( static_cast<std::size_t>( SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS ) *
                               INSTANCE_FLOATS );
}

CollisionVisualizer::~CollisionVisualizer()
{
    ResetResources( nullptr );
}


void CollisionVisualizer::SetClipPlane( float x, float y, float z, float w )
{
    // The render loop uses this during water reflection passes so the debug solids
    // obey the same reflection clipping as the normal model renderer.
    m_clipPlane[0] = x;
    m_clipPlane[1] = y;
    m_clipPlane[2] = z;
    m_clipPlane[3] = w;
}


void CollisionVisualizer::SetAlphaOverride( float alpha )
{
    m_alphaOverride = alpha;
}


void CollisionVisualizer::ResetResources( Dx12GeometryOwner* renderGeometry )
{
    // Lifetime: explicit runtime teardown passes the live geometry owner so the
    // backend can delete handles. The destructor may run after backend shutdown,
    // in which case the ordered teardown path has already released them.
    m_shader.reset();
    if ( renderGeometry )
    {
        if ( m_sphereInstMesh )
        {
            renderGeometry->DestroyInstancedMesh( m_sphereInstMesh );
        }

        if ( m_boxInstMesh )
        {
            renderGeometry->DestroyInstancedMesh( m_boxInstMesh );
        }

        if ( m_hullDynamicVB )
        {
            renderGeometry->DestroyDynamicVB( m_hullDynamicVB );
        }
    }

    m_sphereInstMesh = 0;
    m_boxInstMesh = 0;
    m_hullDynamicVB = 0;
    m_sphereVertexCount = 0;
    m_boxVertexCount = 0;
}


void CollisionVisualizer::BuildSphereMesh( Dx12GeometryOwner& renderGeometry )
{
    // The collision shader only reads position and normal from the static vertex
    // buffer. UVs are generated by the shared primitive builder for the normal
    // renderer, then intentionally dropped here.
    constexpr int slices = 25;
    constexpr int stacks = 25;
    std::vector<float> verts;
    verts.reserve( PrimitiveMeshes::SphereTriangleVertexCount( slices, stacks ) * 6 );

    PrimitiveMeshes::EmitUnitSphere(
        slices,
        stacks,
        [&]( const PrimitiveMeshes::VertexPNUV& vertex )
        { verts.insert( verts.end(), { vertex.x, vertex.y, vertex.z, vertex.nx, vertex.ny, vertex.nz } ); } );

    m_sphereVertexCount = PrimitiveMeshes::SphereTriangleVertexCount( slices, stacks );

    // Static attributes occupy shader locations 0-1: position and normal.
    // Instance attributes start at location 3, leaving location 2 unused so the
    // layout stays compatible with the normal renderer's position/normal/uv slots.
    int staticAttribSizes[] = { 3, 3 };
    int instanceAttribSizes[] = { 4, 4, 4, 4, 4 };

    m_sphereInstMesh = renderGeometry.CreateInstancedMesh( verts.data(),
                                                           m_sphereVertexCount,
                                                           6,
                                                           INSTANCE_FLOATS,
                                                           3,
                                                           instanceAttribSizes,
                                                           staticAttribSizes );
}


void CollisionVisualizer::BuildBoxMesh( Dx12GeometryOwner& renderGeometry )
{
    // Same shared unit cube as PrimitiveMeshes::EmitUnitBox(), packed for the
    // collision shader's smaller static layout: position plus face normal.
    std::vector<float> verts;
    verts.reserve( PrimitiveMeshes::BoxTriangleVertexCount() * 6 );

    PrimitiveMeshes::EmitUnitBox(
        [&]( const PrimitiveMeshes::VertexPNUV& vertex )
        { verts.insert( verts.end(), { vertex.x, vertex.y, vertex.z, vertex.nx, vertex.ny, vertex.nz } ); } );

    m_boxVertexCount = PrimitiveMeshes::BoxTriangleVertexCount();

    // Instance layout is mat4 + rgba:
    //   locations 3-6: model matrix columns
    //   location 7:    debug color
    int staticAttribSizes[] = { 3, 3 };
    int instanceAttribSizes[] = { 4, 4, 4, 4, 4 };

    m_boxInstMesh = renderGeometry.CreateInstancedMesh( verts.data(),
                                                        m_boxVertexCount,
                                                        6,
                                                        INSTANCE_FLOATS,
                                                        3,
                                                        instanceAttribSizes,
                                                        staticAttribSizes );
}


void CollisionVisualizer::EnsureResources( Assets::AssetSystem& assets,
                                           Rendering::Dx12ResourceBuilder& renderResources,
                                           Rendering::Dx12GeometryOwner& renderGeometry )
{
    // Resource creation is lazy so toggling the visualizer off has no startup cost.
    // The shader and both primitive meshes are created together on the first visible
    // frame, then reused until ResetResources() is called.
    if ( !m_shader )
    {
        m_shader = assets.CreateShader( renderResources, "shader.collision_visualizer" );
    }

    if ( m_sphereInstMesh == 0 )
    {
        BuildSphereMesh( renderGeometry );
    }

    if ( m_boxInstMesh == 0 )
    {
        BuildBoxMesh( renderGeometry );
    }

    if ( m_hullDynamicVB == 0 )
    {
        int attribs[] = { 3, 3, 4, 4, 4, 4, 4 };
        m_hullDynamicVB = renderGeometry.CreateDynamicVB( attribs, 7, HULL_MAX_TRIANGLE_VERTICES );
    }
}


void CollisionVisualizer::AppendInstance( std::vector<float>& out, const Matrix4& model, const Color& color )
{
    // The instance buffer is a tightly packed stream of:
    //   16 floats: model matrix
    //    4 floats: rgba debug color
    //
    // Render() builds separate sphere and box streams because each shape has its
    // own static mesh handle but the same instance payload format.
    const float* md = model.Data();
    out.insert( out.end(), md, md + 16 );
    out.push_back( color.r );
    out.push_back( color.g );
    out.push_back( color.b );
    out.push_back( color.a );
}


void CollisionVisualizer::Update( float dt, const CollisionVisualizerFrameView& view )
{
    // This mirrors per-frame contact/sleep state into a visual fade cache. The
    // cache is for rendering only; the solver never reads these values back.
    // PhysicsWorld records whether each object contacted another object during
    // the current physics step. This visualizer keeps a small amount of
    // temporal state so contact flashes do not disappear in a single frame.
    //
    // Color state:
    //   sleeping object: collision flash forced to zero; sleep palette wins
    //   contact object:  collision amount snaps to 1.0
    //   otherwise:       collision amount fades back toward 0.0 over FADE_DURATION
    const int modelCount = view.modelCount;
    if ( static_cast<int>( m_models.size() ) != modelCount )
    {
        m_models.assign( modelCount, TrackedModel() );
    }

    const std::vector<uint8_t>& contacts = view.collisionContacts;
    const auto sleepStates = view.sleepStates;
    const float fadeStep = ( FADE_DURATION > 0.0f ) ? ( dt / FADE_DURATION ) : 1.0f;

    for ( int i = 0; i < modelCount; ++i )
    {
        const bool sleeping = i < static_cast<int>( sleepStates.size() ) && sleepStates[i] != 0;
        const bool contact = i < static_cast<int>( contacts.size() ) && contacts[i] != 0;

        if ( sleeping )
        {
            m_models[i].collisionAmount = 0.0f;
        }
        else if ( contact )
        {
            m_models[i].collisionAmount = 1.0f;
        }
        else
        {
            m_models[i].collisionAmount = (std::max)( 0.0f, m_models[i].collisionAmount - fadeStep );
        }
    }
}


void CollisionVisualizer::BuildSleepGroupSizes( const CollisionVisualizerFrameView& view )
{
    // Sleeping objects are grouped into islands by the solver. The color palette
    // uses that island id so stacks/resting piles are easy to distinguish at a
    // glance. Group size is also used for a special single-sphere color below.
    const int modelCount = view.modelCount;
    m_sleepGroupSizes.assign( modelCount, 1 );

    const auto sleepStates = view.sleepStates;
    const auto islandIds = view.sleepIslandVisualIds;

    for ( int i = 0; i < modelCount; ++i )
    {
        if ( i >= static_cast<int>( sleepStates.size() ) || sleepStates[i] == 0 )
        {
            continue;
        }

        const int islandId = i < static_cast<int>( islandIds.size() ) ? islandIds[i] : 0;
        int count = 0;
        for ( int j = 0; j < modelCount; ++j )
        {
            if ( j >= static_cast<int>( sleepStates.size() ) || sleepStates[j] == 0 )
            {
                continue;
            }

            const int otherIslandId = j < static_cast<int>( islandIds.size() ) ? islandIds[j] : 0;
            if ( islandId != 0 ? otherIslandId == islandId : j == i )
            {
                ++count;
            }
        }

        m_sleepGroupSizes[i] = (std::max)( 1, count );
    }
}


CollisionVisualizer::Color CollisionVisualizer::ComputeModelColor( int modelIndex,
                                                                   const CollisionVisualizerFrameView& view ) const
{
    // Awake objects fade between green and red:
    //   green = no recent contact
    //   red   = contact this frame or still fading from one
    //
    // Sleeping objects use a separate palette by sleep island so stable groups are
    // visible even when they are no longer generating active contact flashes.
    static constexpr Color green = { 0.05f, 0.78f, 0.18f, 1.0f };
    static constexpr Color red = { 1.0f, 0.05f, 0.02f, 1.0f };

    static constexpr Color yellow = { 1.0f, 0.86f, 0.05f, 1.0f };

    static constexpr Color sleepingPalette[3] = {
        { 0.52f, 0.22f, 0.95f, 1.0f },
        { 1.0f, 0.22f, 0.67f, 1.0f },
        { 0.05f, 0.42f, 1.0f, 1.0f },
    };

    const auto sleepStates = view.sleepStates;
    const bool sleeping = modelIndex < static_cast<int>( sleepStates.size() ) && sleepStates[modelIndex] != 0;
    if ( sleeping )
    {
        const auto& colliders = view.colliders.Records();
        const bool isBox = modelIndex < static_cast<int>( colliders.size() ) &&
                           colliders[static_cast<std::size_t>( modelIndex )].shapeKind == ColliderShapeKind::Box;

        if ( !isBox && modelIndex < static_cast<int>( m_sleepGroupSizes.size() ) && m_sleepGroupSizes[modelIndex] <= 1 )
        {
            return yellow;
        }

        const auto islandIds = view.sleepIslandVisualIds;
        const int islandId = modelIndex < static_cast<int>( islandIds.size() ) && islandIds[modelIndex] != 0
                                 ? islandIds[modelIndex]
                                 : modelIndex + 1;

        return sleepingPalette[( islandId - 1 ) % 3];
    }

    const float t = modelIndex < static_cast<int>( m_models.size() ) ? m_models[modelIndex].collisionAmount : 0.0f;
    return {
        green.r + ( red.r - green.r ) * t,
        green.g + ( red.g - green.g ) * t,
        green.b + ( red.b - green.b ) * t,
        1.0f,
    };
}


void CollisionVisualizer::DrawInstances( Dx12GeometryOwner& renderCommands,
                                         uint32_t mesh,
                                         int vertexCount,
                                         const std::vector<float>& instanceData,
                                         const PassRasterStateBucket& rasterState )
{
    // Skip empty shape batches. This keeps mixed scenes cheap when one primitive
    // type is absent, and it avoids uploading an empty instance buffer.
    const int instanceCount = static_cast<int>( instanceData.size() ) / INSTANCE_FLOATS;
    if ( mesh == 0 || vertexCount <= 0 || instanceCount <= 0 )
    {
        return;
    }

    renderCommands.UploadInstanceData( mesh, instanceData );
    renderCommands.DrawInstancedMesh( { mesh, vertexCount, instanceCount, rasterState } );
}


void CollisionVisualizer::DrawHullInstance( Dx12GeometryOwner& renderCommands,
                                            const ConvexHullShape& hull,
                                            const Matrix4& model,
                                            const Color& color,
                                            const PassRasterStateBucket& rasterState )
{
    static_assert(
        HULL_MAX_TRIANGLE_VERTICES == ConvexHullShape::MAX_FACES * ( ConvexHullShape::MAX_FACE_VERTICES - 2 ) * 3,
        "CollisionVisualizer hull scratch must match ConvexHullShape capacity." );

    if ( m_hullDynamicVB == 0 )
    {
        return;
    }

    float instanceData[INSTANCE_FLOATS] = {};
    const float* md = model.Data();
    std::copy( md, md + 16, instanceData );
    instanceData[16] = color.r;
    instanceData[17] = color.g;
    instanceData[18] = color.b;
    instanceData[19] = color.a;

    int vertexCount = 0;
    auto emitVertex = [&]( uint16_t index, const Vector3& normal )
    {
        if ( vertexCount >= HULL_MAX_TRIANGLE_VERTICES )
        {
            return;
        }

        const Vector3 p = hull.GetPosition() + hull.GetVertex( index );
        float* out = &m_hullDebugVertexData[static_cast<size_t>( vertexCount ) * HULL_DYNAMIC_FLOATS_PER_VERTEX];
        out[0] = p.x;
        out[1] = p.y;
        out[2] = p.z;
        out[3] = normal.x;
        out[4] = normal.y;
        out[5] = normal.z;
        std::copy( instanceData, instanceData + INSTANCE_FLOATS, out + 6 );
        ++vertexCount;
    };

    for ( uint16_t f = 0; f < hull.GetFaceCount(); ++f )
    {
        const ConvexHullFace& face = hull.GetFace( f );
        if ( face.indexCount < 3 )
        {
            continue;
        }

        const uint16_t root = hull.GetFaceIndex( face.firstIndex );
        for ( uint8_t i = 1; i + 1 < face.indexCount; ++i )
        {
            const uint16_t b = hull.GetFaceIndex( face.firstIndex + i );
            const uint16_t c = hull.GetFaceIndex( face.firstIndex + i + 1 );
            emitVertex( root, face.normalLocal );
            emitVertex( b, face.normalLocal );
            emitVertex( c, face.normalLocal );
        }
    }

    if ( vertexCount > 0 )
    {
        renderCommands.UploadAndDrawDynamicVB(
            m_hullDynamicVB,
            std::span<const float>( m_hullDebugVertexData.data(),
                                    static_cast<size_t>( vertexCount ) * HULL_DYNAMIC_FLOATS_PER_VERTEX ),
            rasterState );
    }
}


void CollisionVisualizer::Render( Assets::AssetSystem& assets,
                                  Rendering::Dx12ResourceBuilder& renderResources,
                                  Rendering::Dx12GeometryOwner& renderGeometry,
                                  Rendering::Dx12Diagnostics& renderDiagnostics,
                                  const CollisionVisualizerFrameView& view,
                                  const Matrix4& cameraView,
                                  const Matrix4& proj,
                                  const float lightPos[4] )
{
    if ( !m_enabled || view.modelCount <= 0 )
    {
        return;
    }

    EnsureResources( assets, renderResources, renderGeometry );
    BuildSleepGroupSizes( view );

    m_sphereInstanceData.clear();
    m_boxInstanceData.clear();

    // Build primitive streams from the authoritative collision shape. Hulls are
    // drawn after shader constants are bound because each hull emits transient
    // triangle data instead of reusing a cached static mesh.
    const auto colliders = view.colliders.Records();
    const auto instances = view.renderInstances.Records();
    const int modelCount = (std::min)( view.modelCount,
                                       (std::min)( static_cast<int>( colliders.size() ),
                                                   static_cast<int>( instances.size() ) ) );

    for ( int i = 0; i < modelCount; ++i )
    {
        const ColliderRecord& collider = colliders[static_cast<std::size_t>( i )];
        const RenderInstanceRecord& instance = instances[static_cast<std::size_t>( i )];
        Color color = ComputeModelColor( i, view );
        if ( m_alphaOverride >= 0.0f )
        {
            color.a = m_alphaOverride;
        }

        if ( collider.shapeKind == ColliderShapeKind::Box )
        {
            AppendInstance( m_boxInstanceData, instance.modelMatrix, color );
        }
        else if ( collider.shapeKind != ColliderShapeKind::ConvexHull )
        {
            AppendInstance( m_sphereInstanceData, instance.modelMatrix, color );
        }
    }

    // The fragment shader expects light position in view space. Keep the same
    // transform used by the normal lit path so collision solids respond to camera
    // movement and directional/positional light modes consistently.
    float viewLightPos[4];
    for ( int i = 0; i < 3; ++i )
    {
        viewLightPos[i] = cameraView.m[i] * lightPos[0] + cameraView.m[i + 4] * lightPos[1] +
                          cameraView.m[i + 8] * lightPos[2] + cameraView.m[i + 12] * lightPos[3];
    }

    viewLightPos[3] = lightPos[3];

    m_shader->Use();
    m_shader->SetMat4( "uView", cameraView );
    m_shader->SetMat4( "uProjection", proj );
    m_shader->SetVec4( "uClipPlane", m_clipPlane[0], m_clipPlane[1], m_clipPlane[2], m_clipPlane[3] );
    m_shader->SetVec4( "uLightPosition", viewLightPos[0], viewLightPos[1], viewLightPos[2], viewLightPos[3] );

    const bool translucent = m_alphaOverride >= 0.0f && m_alphaOverride < 1.0f;
    const PassRasterStateBucket& rasterState = translucent ? COLLISION_TRANSLUCENT_RASTER : COLLISION_OPAQUE_RASTER;
    {
        CollisionVisualizerTraceScope traceScope( renderDiagnostics, "CollisionSpheres" );
        DrawInstances( renderGeometry, m_sphereInstMesh, m_sphereVertexCount, m_sphereInstanceData, rasterState );
    }
    {
        CollisionVisualizerTraceScope traceScope( renderDiagnostics, "CollisionBoxes" );
        DrawInstances( renderGeometry, m_boxInstMesh, m_boxVertexCount, m_boxInstanceData, rasterState );
    }
    {
        CollisionVisualizerTraceScope traceScope( renderDiagnostics, "CollisionHulls" );
        for ( int i = 0; i < modelCount; ++i )
        {
            const ColliderRecord& collider = colliders[static_cast<std::size_t>( i )];
            if ( collider.shapeKind != ColliderShapeKind::ConvexHull )
            {
                continue;
            }

            const ConvexHullShape* hull = std::get_if<ConvexHullShape>( &collider.shape );
            if ( !hull )
            {
                continue;
            }

            Color color = ComputeModelColor( i, view );
            if ( m_alphaOverride >= 0.0f )
            {
                color.a = m_alphaOverride;
            }

            DrawHullInstance( renderGeometry,
                              *hull,
                              instances[static_cast<std::size_t>( i )].modelMatrix,
                              color,
                              rasterState );
        }
    }
}
