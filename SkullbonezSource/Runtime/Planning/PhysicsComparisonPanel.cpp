#include "PhysicsComparisonPanel.h"
#include "ReplayCauseInspection.h"
#include "../../UI/UIStyle.h"
#include "../../UI/UIFontMetrics.h"
#include "../../Core/Allocation/RuntimeAllocationTracker.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string_view>

using namespace SkullbonezCore;
using namespace SkullbonezCore::Runtime;
using Math::Transformation::Matrix4;
using Math::Vector::Vector3;
namespace
{
constexpr UI::Style::UIColor ink { 0.88f, 0.93f, 1, 1 }, muted { 0.56f, 0.65f, 0.75f, 1 };
constexpr UI::Style::UIColor cyan { 0.16f, 0.86f, 1, 1 }, coral { 1, 0.49f, 0.36f, 1 };
constexpr const char* comparisonOptions[] = { "Ragdoll & Wall: FP6 vs FP7",
                                              "Wall Only - Post-Ragdoll Velocity: FP6 vs FP7" };
int SelectedSolverLab( const PhysicsComparison& comparison )
{
    const std::string_view path = comparison.BundlePath();
    if ( path.ends_with( "solver-lab/ragdoll-wall/comparison.json" ) ||
         path.ends_with( "solver-lab\\ragdoll-wall\\comparison.json" ) )
    {
        return 0;
    }
    if ( path.ends_with( "solver-lab/wall-only/comparison.json" ) ||
         path.ends_with( "solver-lab\\wall-only\\comparison.json" ) )
    {
        return 1;
    }
    return -1;
}
void Line( UI::UIDrawList& draw, UI::UIPoint a, UI::UIPoint b, const UI::Style::UIColor& color )
{
    const float dx = b.x - a.x, dy = b.y - a.y;
    const float length = std::sqrt( dx * dx + dy * dy );
    if ( length < 0.001f )
    {
        return;
    }
    const UI::UIPoint offset { -dy / length, dx / length };
    draw.AddTriangle( { { a.x + offset.x, a.y + offset.y },
                        { b.x + offset.x, b.y + offset.y },
                        { b.x - offset.x, b.y - offset.y } },
                      color );
    draw.AddTriangle( { { a.x + offset.x, a.y + offset.y },
                        { b.x - offset.x, b.y - offset.y },
                        { a.x - offset.x, a.y - offset.y } },
                      color );
}
bool MatchesEvent( const ReplaySolverFrameSample& evidence, const Physics::PhysicsSolverPersistentContactSample& contact,
                   const ComparisonEvent& event )
{
    uint64_t a = 0, b = 0;
    for ( const auto& body : evidence.bodies )
    {
        if ( body.modelRow.value == contact.bodyA )
        {
            a = body.id.value;
        }
        if ( body.modelRow.value == contact.bodyB )
        {
            b = body.id.value;
        }
    }
    return a == event.bodyA && ( contact.isTerrain || b == event.bodyB ) && contact.featureId == event.feature;
}
int UniqueContactIndex( const ReplaySolverFrameSample& evidence, const ComparisonEvent& event )
{
    int index = -1;
    const auto& contacts = evidence.worldSnapshot.physics.persistentContacts;
    for ( std::size_t i = 0; i < contacts.size(); ++i )
    {
        if ( !MatchesEvent( evidence, contacts[i], event ) )
        {
            continue;
        }
        if ( index >= 0 )
        {
            return -1;
        }
        index = static_cast<int>( i );
    }
    return index;
}
const char* ChangeName( ComparisonChange change )
{
    switch ( change )
    {
    case ComparisonChange::Equal:
        return "unchanged";
    case ComparisonChange::Changed:
        return "changed";
    case ComparisonChange::OnlyA:
        return "A only";
    case ComparisonChange::OnlyB:
        return "B only";
    case ComparisonChange::Ambiguous:
        return "ambiguous";
    default:
        return "not recorded";
    }
}
} // namespace
void PhysicsComparisonPanel::Prepare( const Physics::ColliderStore& colliders,
                                      const Rendering::RenderInstanceStore& instances )
{
    Core::Allocation::RuntimeAllocationScope loading( Core::Allocation::RuntimeAllocationPhase::Capture );
    m_comparisonCombo.Close();
    m_eventOffset = 0;
    m_contactPulseStarted = -1;
    m_contactSelectionRevision = 0;
    m_contactTick = -1;
    m_shapes.clear();
    m_spheres.clear();
    m_boxes.clear();
    m_hulls.clear();
    m_shapes.reserve( colliders.Records().size() );
    for ( const auto& collider : colliders.Records() )
    {
        Rendering::RenderMaterial material;
        for ( const auto& instance : instances.Records() )
        {
            if ( instance.sceneObjectId == collider.sceneObjectId )
            {
                material = instance.material;
                break;
            }
        }
        // Controlled inspection shading has no contact flash or animated texture inputs.
        material.kind = Rendering::RenderMaterialKind::Matte;
        material.contactFlashAlpha = 0;
        material.baseColor[3] = 1;
        m_shapes.push_back( { collider.sceneObjectId.value, collider, material, RetainGeometry( collider ) } );
    }
    RebindGeometry();
    std::sort( m_shapes.begin(), m_shapes.end(), []( const auto& a, const auto& b ) { return a.id < b.id; } );
    for ( auto& models : m_models )
    {
        models.reserve( m_shapes.size() );
    }
}
std::size_t PhysicsComparisonPanel::RetainGeometry( const Physics::ColliderRecord& collider )
{
    using namespace Math::CollisionDetection;
    if ( const auto* sphere = GetShapeIf<BoundingSphere>( &collider.shape ) )
    {
        m_spheres.push_back( *sphere );
        return m_spheres.size() - 1;
    }
    if ( const auto* box = GetShapeIf<BoundingBox>( &collider.shape ) )
    {
        m_boxes.push_back( *box );
        return m_boxes.size() - 1;
    }
    const auto* hull = GetShapeIf<ConvexHullShape>( &collider.shape );
    for ( const Shape& retained : m_shapes )
    {
        if ( GetShapeIf<ConvexHullShape>( &retained.collider.shape ) == hull )
        {
            return retained.geometryIndex;
        }
    }
    m_hulls.push_back( *hull );
    return m_hulls.size() - 1;
}

void PhysicsComparisonPanel::RebindGeometry()
{
    using namespace Math::CollisionDetection;
    // Lifetime: copy each shape while Scene still owns the source, then bind
    // after vector growth. Shared hull variants have one owned copy. Scene
    // reset/loading can now release its collider storage independently.
    for ( Shape& shape : m_shapes )
    {
        switch ( shape.collider.shapeKind )
        {
        case Physics::ColliderShapeKind::Sphere:
            shape.collider.shape = CollisionShapeReference( m_spheres[shape.geometryIndex],
                                                            CollisionShapeReference::INVALID_STORAGE_INDEX );
            break;
        case Physics::ColliderShapeKind::Box:
            shape.collider.shape = CollisionShapeReference( m_boxes[shape.geometryIndex],
                                                            CollisionShapeReference::INVALID_STORAGE_INDEX );
            break;
        case Physics::ColliderShapeKind::ConvexHull:
            shape.collider.shape = CollisionShapeReference( m_hulls[shape.geometryIndex],
                                                            CollisionShapeReference::INVALID_STORAGE_INDEX );
            break;
        }
    }
}

void PhysicsComparisonPanel::ReleaseComparison()
{
    m_comparisonCombo.Close();
    m_buttonCount = 0;
    m_timeline = {};
    m_eventOffset = 0;
    m_controlsScroll = m_detailsScroll = 0.0f;
    for ( auto& models : m_models )
    {
        std::vector<Rendering::ModelViewItem> {}.swap( models );
    }
    std::vector<Shape> {}.swap( m_shapes );
    std::vector<Math::CollisionDetection::BoundingSphere> {}.swap( m_spheres );
    std::vector<Math::CollisionDetection::BoundingBox> {}.swap( m_boxes );
    std::vector<Math::CollisionDetection::ConvexHullShape> {}.swap( m_hulls );
}

void PhysicsComparisonPanel::BuildModels( const PhysicsComparison& comparison )
{
    const auto& settings = comparison.Settings();
    for ( int side = 0; side < 2; ++side )
    {
        auto& models = m_models[side];
        models.clear();
        for ( const auto& shape : m_shapes )
        {
            const auto* body = comparison.Body( side, shape.id, comparison.Tick() );
            if ( !body )
            {
                continue;
            }
            Rendering::ModelViewItem item;
            item.material = shape.material;
            const auto rotation = Matrix4::FromQuaternion(
                Math::Orientation::Quaternion( body->orientation[0], body->orientation[1], body->orientation[2],
                                               body->orientation[3] ) );
            item.shape = shape.collider.shapeKind == Physics::ColliderShapeKind::Box
                             ? Rendering::RenderInstanceShapeKind::Box
                         : shape.collider.shapeKind == Physics::ColliderShapeKind::Sphere
                             ? Rendering::RenderInstanceShapeKind::Sphere
                             : Rendering::RenderInstanceShapeKind::ConvexHull;
            item.hull = Math::CollisionDetection::GetShapeIf<Math::CollisionDetection::ConvexHullShape>(
                &shape.collider.shape );
            item.transform = item.hull ? Matrix4::Translate( body->position ) * rotation
                                       : Math::CollisionDetection::GetShapeModelMatrix( shape.collider.shape, body->position,
                                                                                        rotation );
            if ( settings.display == ComparisonDisplay::Heatmap )
            {
                const auto difference = comparison.Difference( shape.id, comparison.Tick() );
                const float amount = std::clamp( ( settings.angularHeatmap ? difference.angleDegrees
                                                                           : difference.distance ) /
                                                     settings.heatScale,
                                                 0.0f, 1.0f );
                item.material.baseColor[0] = amount;
                item.material.baseColor[1] = 0.2f + 0.5f * ( 1 - amount );
                item.material.baseColor[2] = 1 - amount;
                if ( difference.change == ComparisonChange::OnlyA || difference.change == ComparisonChange::OnlyB )
                {
                    item.material.baseColor[0] = 1;
                    item.material.baseColor[2] = 1;
                }
            }
            models.push_back( item );
        }
    }
}
std::array<Rendering::ContactManifoldPresentation, 2>
PhysicsComparisonPanel::BuildContacts( const PhysicsComparison& comparison ) const
{
    std::array<Rendering::ContactManifoldPresentation, 2> result;
    if ( comparison.SelectedEvent() < 0 ||
         static_cast<std::size_t>( comparison.SelectedEvent() ) >= comparison.Events().size() )
    {
        return result;
    }
    const auto& event = comparison.Events()[static_cast<std::size_t>( comparison.SelectedEvent() )];
    for ( int side = 0; side < 2; ++side )
    {
        const auto* evidence = comparison.Recording( side ).Evidence( event.tick );
        int index = side ? event.contactB : event.contactA;
        if ( !evidence || index < 0 )
        {
            continue;
        }
        if ( event.family != ComparisonFamily::Contact && event.family != ComparisonFamily::Terrain )
        {
            continue;
        }
        const auto& contacts = evidence->worldSnapshot.physics.persistentContacts;
        if ( event.summary )
        {
            index = UniqueContactIndex( *evidence, event );
        }
        if ( index < 0 || static_cast<std::size_t>( index ) >= contacts.size() )
        {
            continue;
        }
        const auto& contact = contacts[static_cast<std::size_t>( index )];
        ReplayCauseSolverDetailResult detail;
        detail.frame = evidence->frameIndex;
        detail.availability = ReplayCauseSolverDetailAvailability::Available;
        detail.sourceContacts = contacts;
        detail.sourcePipelineRecords = evidence->worldSnapshot.physics.pipelineTrace;
        detail.bodyA = contact.bodyA;
        detail.bodyB = contact.bodyB;
        detail.terrain = contact.isTerrain;
        for ( const auto& row : contacts )
        {
            if ( row.bodyA == contact.bodyA && row.bodyB == contact.bodyB && row.isTerrain == contact.isTerrain )
            {
                ++detail.contactRowCount;
            }
        }
        // The existing presentation projection reads only these recorded
        // rows and original narrowphase points; no solver is invoked.
        detail.pipelineRecordCount = detail.sourcePipelineRecords.size();
        result[side] = BuildReplayCauseContactPresentation( detail, *evidence );
    }
    return result;
}
bool PhysicsComparisonPanel::ContactPivot( const PhysicsComparison& comparison, Vector3& pivot ) const
{
    const auto contacts = BuildContacts( comparison );
    if ( !contacts[0].HasGeometry() && !contacts[1].HasGeometry() )
    {
        return false;
    }
    pivot = contacts[0].HasGeometry() && contacts[1].HasGeometry() ? ( contacts[0].Center() + contacts[1].Center() ) * 0.5f
            : contacts[0].HasGeometry()                            ? contacts[0].Center()
                                                                   : contacts[1].Center();
    return true;
}
Rendering::PairedViewFrame PhysicsComparisonPanel::BuildFrame( const PhysicsComparison& comparison, int width, int height )
{
    BuildModels( comparison );
    const auto& settings = comparison.Settings();
    Rendering::PairedViewFrame frame;
    frame.models = { m_models[0], m_models[1] };
    frame.x = 0;
    frame.y = 82;
    frame.width = (std::max)( 2, ( width - 390 ) / 2 * 2 );
    frame.height = (std::max)( 2, ( height - 139 ) / 2 * 2 );
    if ( m_layout.shared )
    {
        frame.x = static_cast<int>( m_layout.viewport.x );
        frame.y = static_cast<int>( m_layout.viewport.y );
        frame.width = (std::max)( 2, static_cast<int>( m_layout.viewport.w ) / 2 * 2 );
        frame.height = (std::max)( 2, static_cast<int>( m_layout.viewport.h ) / 2 * 2 );
    }
    frame.stacked = settings.stackedViews;
    frame.mode = settings.display == ComparisonDisplay::Split                      ? 0
                 : settings.display == ComparisonDisplay::Overlay                  ? 1
                 : settings.display == ComparisonDisplay::Pixels                   ? 4
                 : settings.display == ComparisonDisplay::Toggle && settings.showA ? 2
                                                                                   : 3;
    // Keep the field of view bounded on the longer viewport axis. A very wide
    // stacked pane must not acquire an ultrawide lens while retaining a tall
    // pane's vertical angle. The projection still uses its exact pixel aspect.
    const float aspect = static_cast<float>( frame.ImageWidth() ) / frame.ImageHeight();
    const float verticalTangent = 0.41421356237f / (std::max)( 1.0f, aspect );
    const float verticalDegrees = 2.0f * std::atan( verticalTangent ) * 180.0f / 3.141592653589793f;
    frame.projection = Matrix4::PerspectiveZeroToOne( verticalDegrees, aspect, 0.05f, 100000 );
    frame.gain = settings.pixelGain;
    frame.outlineAlpha = settings.outlineAlpha;
    frame.occludedOutline = settings.occludedOutline;
    frame.contacts = BuildContacts( comparison );
    if ( comparison.SelectedEvent() >= 0 && ( frame.contacts[0].HasGeometry() || frame.contacts[1].HasGeometry() ) )
    {
        const int eventTick = comparison.Events()[static_cast<std::size_t>( comparison.SelectedEvent() )].tick;
        const bool newSelection = m_contactSelectionRevision != comparison.EventSelectionRevision();
        const bool crossed = ( m_contactTick < eventTick && comparison.Tick() >= eventTick ) ||
                             ( m_contactTick > eventTick && comparison.Tick() <= eventTick );
        if ( ( newSelection && comparison.Tick() == eventTick ) || ( !newSelection && crossed ) )
        {
            m_contactPulseStarted = m_lastTime;
        }
        const float alpha = m_contactPulseStarted >= 0
                                ? static_cast<float>(
                                      std::clamp( 1.0 - ( m_lastTime - m_contactPulseStarted ) / 0.2, 0.0, 1.0 ) )
                                : 0.0f;
        for ( auto& patch : frame.contacts )
        {
            patch.normalLengthScale = 1.0f + 0.2f * alpha;
        }
    }
    else
    {
        m_contactPulseStarted = -1;
    }
    m_contactSelectionRevision = comparison.EventSelectionRevision();
    m_contactTick = comparison.Tick();
    return frame;
}
float PhysicsComparisonPanel::Radius( uint64_t id ) const noexcept
{
    for ( const auto& shape : m_shapes )
    {
        if ( shape.id == id )
        {
            return shape.collider.boundingRadius;
        }
    }
    return 1;
}
void PhysicsComparisonPanel::ButtonAt( UI::UIRect bounds, const char* text, int action, bool active )
{
    if ( m_buttonCount == m_buttons.size() )
    {
        return;
    }
    UI::UIRect hit = bounds;
    if ( m_layout.shared )
    {
        hit.x = (std::max)( bounds.x, m_buttonClip.x );
        hit.y = (std::max)( bounds.y, m_buttonClip.y );
        hit.w = (std::min)( bounds.x + bounds.w, m_buttonClip.x + m_buttonClip.w ) - hit.x;
        hit.h = (std::min)( bounds.y + bounds.h, m_buttonClip.y + m_buttonClip.h ) - hit.y;
        if ( hit.w <= 0 || hit.h <= 0 )
        {
            return;
        }
    }
    m_buttons[m_buttonCount++] = { hit, action };
    m_draw.AddRoundedRect( bounds, 5,
                           active ? UI::Style::UIColor { 0.23f, 0.25f, 0.28f, 1 }
                                  : UI::Style::UIColor { 0.13f, 0.14f, 0.16f, 1 } );
    const char* label = text;
    if ( m_layout.shared && bounds.w < 100.0f )
    {
        switch ( action )
        {
        case 1:
            label = "Open";
            break;
        case 3:
            label = "Save";
            break;
        case 4:
            label = "Load";
            break;
        case 5:
            label = "Close";
            break;
        case 6:
            label = "Sel.";
            break;
        case 7:
            label = "All";
            break;
        case 8:
            label = "Diff.";
            break;
        case 9:
            label = "First";
            break;
        case 10:
            label = "Follow";
            break;
        case 14:
            label = std::string_view( text ) == "Stacked" ? "Stack" : "Side";
            break;
        default:
            break;
        }
    }
    float fontSize = m_layout.shared ? 11.0f : 13.0f;
    while ( fontSize > 8.5f && UI::UIFontMetrics::MeasureText( fontSize, label ) > bounds.w - 6.0f )
    {
        fontSize -= 0.5f;
    }
    m_draw.PushClip( bounds );
    m_draw.AddText( { bounds.x + 3, bounds.y + ( bounds.h - fontSize ) * 0.5f - 1 }, fontSize, ink, label );
    m_draw.PopClip();
}
void PhysicsComparisonPanel::Plot( const PhysicsComparison& comparison, UI::UIRect bounds, bool velocity )
{
    m_draw.AddRect( bounds, { 0.025f, 0.04f, 0.065f, 1 } );
    constexpr int count = 96;
    float low = ( std::numeric_limits<float>::max )(), high = std::numeric_limits<float>::lowest();
    for ( int side = 0; side < 2; ++side )
    {
        for ( int i = 0; i < count; ++i )
        {
            const auto* body = comparison.Body( side, comparison.Selected(), i * comparison.LastTick() / ( count - 1 ) );
            if ( !body )
            {
                continue;
            }
            const float value = velocity ? body->linearVelocity.y : body->position.y;
            low = (std::min)( low, value );
            high = (std::max)( high, value );
        }
    }
    if ( low > high )
    {
        return;
    }
    const float range = (std::max)( 0.001f, high - low );
    for ( int side = 0; side < 2; ++side )
    {
        UI::UIPoint previous {};
        bool valid = false;
        for ( int i = 0; i < count; ++i )
        {
            const auto* body = comparison.Body( side, comparison.Selected(), i * comparison.LastTick() / ( count - 1 ) );
            if ( !body )
            {
                valid = false;
                continue;
            }
            const float value = velocity ? body->linearVelocity.y : body->position.y;
            UI::UIPoint point { bounds.x + bounds.w * i / ( count - 1 ),
                                bounds.y + bounds.h - 8 - ( bounds.h - 22 ) * ( value - low ) / range };
            if ( valid )
            {
                Line( m_draw, previous, point, side ? coral : cyan );
            }
            previous = point;
            valid = true;
        }
    }
    char label[100];
    std::snprintf( label, sizeof( label ), "%s  %.4g .. %.4g", velocity ? "Vertical velocity (m/s)" : "Height (m)", low,
                   high );
    m_draw.AddText( { bounds.x + 5, bounds.y + 4 }, 11, muted, label );
    const float x = bounds.x + bounds.w * comparison.Tick() / (std::max)( 1, comparison.LastTick() );
    Line( m_draw, { x, bounds.y }, { x, bounds.y + bounds.h }, ink );
}
const UI::UIDrawList& PhysicsComparisonPanel::Compose( const PhysicsComparison& comparison, int width, int height )
{
    if ( m_layout.shared )
    {
        return ComposeShell( comparison, width, height );
    }
    m_draw.Clear();
    m_buttonCount = 0;
    m_draw.AddRect( { 0, 0, static_cast<float>( width ), 82 }, { 0.035f, 0.055f, 0.08f, 1 } );
    m_draw.AddText( { 16, 12 }, 20, ink, "Solver Lab" );
    ButtonAt( { 172, 7, 72, 30 }, "Open", 1 );
    ButtonAt( { 252, 7, 80, 30 }, "Finding", 4 );
    ButtonAt( { 340, 7, 64, 30 }, "Save", 3 );
    ButtonAt( { 412, 7, 70, 30 }, "Focus", 2 );
    ButtonAt( { 490, 7, 75, 30 }, "Follow A", 10, comparison.Settings().followA );
    ButtonAt( { static_cast<float>( width - 80 ), 7, 64, 30 }, "exit", 5 );
    ButtonAt( { 573, 7, 118, 30 }, comparison.Settings().stackedViews ? "Stacked" : "Side by side", 14 );
    const bool compact = width < 1080;
    m_comparisonCombo.SetBounds( compact ? 694.0f : 705.0f, compact ? 44.0f : 7.0f,
                                 std::clamp( static_cast<float>( width - ( compact ? 710 : 801 ) ), 100.0f, 460.0f ), 30 );
    m_comparisonCombo.SetLabelVisible( false );
    const char* modes[] = { "Split", "Overlay", "Toggle", "Heatmap", "Pixels" };
    for ( int i = 0; i < 5; ++i )
    {
        ButtonAt( { 16.0f + 92 * i, 44, 84, 29 }, modes[i], 20 + i, static_cast<int>( comparison.Settings().display ) == i );
    }
    ButtonAt( { 490, 44, 76, 29 }, comparison.Settings().showA ? "Show B" : "Show A", 11 );
    ButtonAt( { 574, 44, 104, 29 }, comparison.Settings().occludedOutline ? "X-ray on" : "X-ray off", 12 );
    m_sidebar = { static_cast<float>( (std::max)( 0, width - 390 ) ), 82, 390,
                  static_cast<float>( (std::max)( 1, height - 139 ) ) };
    const float x = m_sidebar.x + 12;
    m_draw.AddRect( m_sidebar, { 0.045f, 0.065f, 0.09f, 1 } );
    ButtonAt( { x, 92, 170, 29 }, "Selected objects", 6, comparison.Settings().selectedOnly );
    ButtonAt( { x + 178, 92, 174, 29 }, "All differences", 7, !comparison.Settings().selectedOnly );
    ButtonAt( { x, 129, 120, 29 }, "Differences only", 8, comparison.Settings().differencesOnly );
    ButtonAt( { x + 128, 129, 116, 29 }, "First difference", 9 );
    ButtonAt( { x + 252, 129, 100, 29 }, "Threshold +", 13 );
    char text[240];
    std::snprintf( text, sizeof( text ), "Threshold %.5g m / %.3g deg", comparison.Settings().positionThreshold,
                   comparison.Settings().angleThreshold );
    m_draw.AddText( { x, 166 }, 12, muted, text );
    m_draw.AddText( { x, 185 }, 12, muted, "Recorded divergence; not a proven root cause" );
    float y = 210;
    int skipped = 0;
    for ( std::size_t i = 0; i < comparison.Events().size() && y < m_sidebar.y + m_sidebar.h - 245; ++i )
    {
        const auto& event = comparison.Events()[i];
        if ( !comparison.VisibleEvent( event ) )
        {
            continue;
        }
        if ( skipped++ < m_eventOffset )
        {
            continue;
        }
        std::snprintf( text, sizeof( text ), "%d | #%llu %s %s", event.tick, event.bodyA,
                       event.family == ComparisonFamily::Contact || event.family == ComparisonFamily::Terrain ? "contact"
                       : event.family == ComparisonFamily::Sleep                                              ? "sleep"
                                                                                                              : "motion",
                       ChangeName( event.change ) );
        if ( event.family == ComparisonFamily::SolverIteration )
        {
            std::snprintf( text, sizeof( text ), "%d | solver iteration %s", event.tick, ChangeName( event.change ) );
        }
        ButtonAt( { x, y, 352, 26 }, text, 1000 + static_cast<int>( i ),
                  comparison.SelectedEvent() == static_cast<int>( i ) );
        y += 31;
    }
    const float plotsY = (std::max)( y + 8, m_sidebar.y + m_sidebar.h - 215 );
    if ( comparison.Selected() )
    {
        const auto delta = comparison.Difference( comparison.Selected(), comparison.Tick() );
        std::snprintf( text, sizeof( text ), "#%llu  d=%.5g m  rot=%.4g deg", comparison.Selected(), delta.distance,
                       delta.angleDegrees );
        m_draw.AddText( { x, plotsY }, 12, ink, text );
        Plot( comparison, { x, plotsY + 22, 352, 76 }, false );
        Plot( comparison, { x, plotsY + 106, 352, 76 }, true );
    }
    const bool a = comparison.Recording( 0 ).Frame( comparison.Tick() ) != nullptr,
               b = comparison.Recording( 1 ).Frame( comparison.Tick() ) != nullptr;
    std::snprintf( text, sizeof( text ), "A: %s   B: %s", a ? "motion recorded" : "NO TICK COVERAGE",
                   b ? "motion recorded" : "NO TICK COVERAGE" );
    m_draw.AddText( { 16, 90 }, 13, cyan, text );
    std::snprintf( text, sizeof( text ), "Contacts  A: %s  B: %s",
                   comparison.Recording( 0 ).Evidence( comparison.Tick() )      ? "manifold recorded"
                   : comparison.Recording( 0 ).Observation( comparison.Tick() ) ? "summary recorded"
                                                                                : "not recorded",
                   comparison.Recording( 1 ).Evidence( comparison.Tick() )      ? "manifold recorded"
                   : comparison.Recording( 1 ).Observation( comparison.Tick() ) ? "summary recorded"
                                                                                : "not recorded" );
    m_draw.AddText( { 16, 111 }, 12, muted, text );
    const float bottom = static_cast<float>( height - 57 );
    m_draw.AddRect( { 0, bottom, static_cast<float>( width ), 57 }, { 0.035f, 0.055f, 0.08f, 1 } );
    ButtonAt( { 12, bottom + 12, 45, 29 }, "<", 30 );
    ButtonAt( { 63, bottom + 12, 68, 29 }, "Reverse", 31 );
    ButtonAt( { 137, bottom + 12, 62, 29 }, comparison.Direction() ? "Pause" : "Play", 32 );
    ButtonAt( { 205, bottom + 12, 45, 29 }, ">", 33 );
    std::snprintf( text, sizeof( text ), "%.2gx", comparison.Settings().speed );
    ButtonAt( { 256, bottom + 12, 55, 29 }, text, 34 );
    ButtonAt( { 317, bottom + 12, 56, 29 }, "Loop", 35, comparison.LoopEnabled() );
    m_timeline = { 383, bottom + 15, static_cast<float>( (std::max)( 1, width - 605 ) ), 22 };
    m_draw.AddRoundedRect( m_timeline, 5, { 0.13f, 0.2f, 0.27f, 1 } );
    m_draw.AddRect( { m_timeline.x, m_timeline.y, m_timeline.w * comparison.Tick() / (std::max)( 1, comparison.LastTick() ),
                      m_timeline.h },
                    cyan );
    std::snprintf( text, sizeof( text ), "Tick %d / %d  %.4fs", comparison.Tick(), comparison.LastTick(),
                   comparison.Tick() / 120.0 );
    m_draw.AddText( { static_cast<float>( width - 212 ), bottom + 20 }, 12, ink, text );
    // Draw the popup last so its opaque rows cover the toolbar and scene labels.
    const UI::UIDrawContext draw( width, height, m_draw );
    const int selected = SelectedSolverLab( comparison );
    m_comparisonCombo.Draw( draw, "Comparison",
                            { std::span<const char* const>( comparisonOptions ), selected, 0,
                              selected >= 0 ? comparisonOptions[selected] : "Load comparison" },
                            m_pointer );
    return m_draw;
}
bool PhysicsComparisonPanel::Contains( int x, int y ) const
{
    if ( m_layout.shared )
    {
        return m_comparisonCombo.IsOpen() || m_comboConsumedPointer || m_layout.controls.Contains( x, y ) ||
               m_layout.details.Contains( x, y ) || m_layout.transport.Contains( x, y );
    }
    return m_comparisonCombo.IsOpen() || m_comboConsumedPointer || y < 82 || m_sidebar.Contains( x, y ) ||
           y >= m_timeline.y - 15;
}
ComparisonPanelAction PhysicsComparisonPanel::Input( PhysicsComparison& comparison,
                                                     const UI::InputControl::UIInputSnapshot& input, bool timelineDrag )
{
    m_pointer = { input.mouseX, input.mouseY };
    m_comboConsumedPointer = false;
    if ( m_layout.shared && !timelineDrag && !m_comparisonCombo.IsOpen() &&
         !( m_loadingSurface ? m_layout.viewport.Contains( input.mouseX, input.mouseY )
                             : m_layout.controls.Contains( input.mouseX, input.mouseY ) ||
                                   m_layout.details.Contains( input.mouseX, input.mouseY ) ||
                                   m_layout.transport.Contains( input.mouseX, input.mouseY ) ) )
    {
        return ComparisonPanelAction::None;
    }
    if ( input.leftPressed && m_comparisonCombo.IsOpen() )
    {
        const int option = m_comparisonCombo.HitOption( input.mouseX, input.mouseY, 2 );
        m_comparisonCombo.Close();
        m_comboConsumedPointer = true;
        if ( option >= 0 && option != SelectedSolverLab( comparison ) )
        {
            return option == 0 ? ComparisonPanelAction::RagdollWall : ComparisonPanelAction::WallOnly;
        }
        return ComparisonPanelAction::None;
    }
    if ( input.leftPressed && m_comparisonCombo.HitBox( input.mouseX, input.mouseY ) )
    {
        m_comparisonCombo.SetOpen( true );
        m_comboConsumedPointer = true;
        return ComparisonPanelAction::None;
    }
    if ( m_layout.shared && input.wheelDelta && m_layout.controls.Contains( input.mouseX, input.mouseY ) )
    {
        m_controlsScroll = std::clamp( m_controlsScroll - input.wheelDelta / 120.0f * 36.0f, 0.0f,
                                       (std::max)( 0.0f, 460.0f - m_layout.controls.h ) );
    }
    if ( m_layout.shared && input.wheelDelta && m_layout.details.Contains( input.mouseX, input.mouseY ) &&
         m_eventList.Contains( input.mouseX, input.mouseY ) )
    {
        m_eventOffset = (std::max)( 0, m_eventOffset + ( input.wheelDelta > 0 ? -1 : 1 ) );
    }
    else if ( m_layout.shared && input.wheelDelta && m_layout.details.h < 450.0f &&
              m_layout.details.Contains( input.mouseX, input.mouseY ) )
    {
        m_detailsScroll = std::clamp( m_detailsScroll - input.wheelDelta / 120.0f * 36.0f, 0.0f,
                                      (std::max)( 0.0f, 450.0f - m_layout.details.h ) );
    }
    else if ( input.wheelDelta && m_sidebar.Contains( input.mouseX, input.mouseY ) )
    {
        m_eventOffset = (std::max)( 0, m_eventOffset + ( input.wheelDelta > 0 ? -5 : 5 ) );
    }
    if ( timelineDrag )
    {
        comparison.Play( 0 );
        const float fraction = std::clamp( ( input.mouseX - m_timeline.x ) / m_timeline.w, 0.0f, 1.0f );
        comparison.Seek( static_cast<int>( fraction * comparison.LastTick() ) );
        return ComparisonPanelAction::None;
    }
    if ( !input.leftPressed )
    {
        return ComparisonPanelAction::None;
    }
    for ( std::size_t i = 0; i < m_buttonCount; ++i )
    {
        const auto& button = m_buttons[i];
        if ( !button.bounds.Contains( input.mouseX, input.mouseY ) )
        {
            continue;
        }
        auto& settings = comparison.Settings();
        const int action = button.action;
        if ( action >= 1000 )
        {
            comparison.SelectEvent( static_cast<std::size_t>( action - 1000 ) );
            break;
        }
        if ( action >= 20 && action <= 24 )
        {
            settings.display = static_cast<ComparisonDisplay>( action - 20 );
            break;
        }
        switch ( action )
        {
        case 1:
            return ComparisonPanelAction::Open;
        case 2:
            return ComparisonPanelAction::Focus;
        case 3:
            return ComparisonPanelAction::Save;
        case 4:
            return ComparisonPanelAction::Restore;
        case 5:
            return ComparisonPanelAction::Close;
        case 6:
            settings.selectedOnly = true;
            m_eventOffset = 0;
            break;
        case 7:
            settings.selectedOnly = false;
            m_eventOffset = 0;
            break;
        case 8:
            settings.differencesOnly = !settings.differencesOnly;
            break;
        case 9:
            comparison.Seek( 0 );
            comparison.NextDifference();
            break;
        case 10:
            settings.followA = !settings.followA;
            break;
        case 11:
            settings.showA = !settings.showA;
            break;
        case 12:
            settings.occludedOutline = !settings.occludedOutline;
            break;
        case 14:
            settings.stackedViews = !settings.stackedViews;
            break;
        case 13:
            settings.positionThreshold = settings.positionThreshold >= 0.1f ? 0.0001f : settings.positionThreshold * 10;
            break;
        case 30:
            comparison.Step( -1 );
            break;
        case 31:
            comparison.Play( -1 );
            break;
        case 32:
            comparison.Play( comparison.Direction() ? 0 : 1 );
            break;
        case 33:
            comparison.Step( 1 );
            break;
        case 34:
            settings.speed = settings.speed >= 4 ? 0.25f : settings.speed * 2;
            break;
        case 35:
            comparison.SetLoop( comparison.LoopStart(), comparison.LoopEnd(), !comparison.LoopEnabled() );
            break;
        default:
            break;
        }
        break;
    }
    return ComparisonPanelAction::None;
}
double PhysicsComparisonPanel::Advance( PhysicsComparison& comparison, double now )
{
    const double elapsed = m_lastTime > 0 ? std::clamp( now - m_lastTime, 0.0, 0.1 ) : 0;
    if ( m_lastTime > 0 )
    {
        comparison.Advance( now - m_lastTime );
    }
    m_lastTime = now;
    return elapsed;
}
uint64_t PhysicsComparisonPanel::Pick( const PhysicsComparison& comparison, const Vector3& origin, const Vector3& direction,
                                       int requestedSide ) const
{
    float nearest = ( std::numeric_limits<float>::max )();
    uint64_t selected = 0;
    for ( const auto& shape : m_shapes )
    {
        for ( int side = 0; side < 2; ++side )
        {
            if ( requestedSide >= 0 && side != requestedSide )
            {
                continue;
            }
            const auto* body = comparison.Body( side, shape.id, comparison.Tick() );
            if ( !body )
            {
                continue;
            }
            const auto offset = body->position - origin;
            const float along = offset.x * direction.x + offset.y * direction.y + offset.z * direction.z;
            const float distance = offset.x * offset.x + offset.y * offset.y + offset.z * offset.z - along * along;
            if ( along > 0 && along < nearest && distance <= shape.collider.boundingRadius * shape.collider.boundingRadius )
            {
                nearest = along;
                selected = shape.id;
            }
        }
    }
    return selected;
}

const UI::UIDrawList& PhysicsComparisonPanel::ComposeLoading( int width, int height, int percent, const char* error,
                                                              const char* phase )
{
    m_draw.Clear();
    m_buttonCount = 0;
    m_loadingSurface = true;
    const UI::UIRect viewport = m_layout.shared
                                    ? m_layout.viewport
                                    : UI::UIRect { 0, 0, static_cast<float>( width ), static_cast<float>( height ) };
    m_buttonClip = viewport;
    m_sidebar = {};
    m_timeline = {};
    m_comparisonCombo.Close();
    m_comparisonCombo.SetBounds( 0, 0, 0, 0 );
    m_draw.PushClip( viewport );
    m_draw.AddRect( viewport, { 0.075f, 0.08f, 0.09f, 1 } );
    const float panelWidth = (std::min)( 560.0f, (std::max)( 100.0f, viewport.w - 32.0f ) );
    const float x = viewport.x + ( viewport.w - panelWidth ) * 0.5f;
    const float y = viewport.y + ( viewport.h - 150.0f ) * 0.5f;
    const bool failed = error && *error;
    m_draw.AddText( { x, y }, 23, ink, failed ? "Unable to open comparison" : "Loading comparison" );
    m_draw.AddText( { x, y + 40 }, 14, muted, failed ? error : phase );
    if ( !failed )
    {
        m_draw.AddRoundedRect( { x, y + 75, panelWidth, 16 }, 5, { 0.13f, 0.2f, 0.27f, 1 } );
        m_draw.AddRoundedRect( { x, y + 75, panelWidth * std::clamp( percent, 0, 100 ) / 100.0f, 16 }, 5, cyan );
        char label[48];
        std::snprintf( label, sizeof( label ), "%d%%", percent );
        m_draw.AddText( { x, y + 108 }, 14, ink, label );
    }
    ButtonAt( { x + panelWidth - 90, y + 105, 90, 30 }, failed ? "Close" : "Cancel", 5 );
    m_draw.PopClip();
    return m_draw;
}


const UI::UIDrawList& PhysicsComparisonPanel::ComposeShell( const PhysicsComparison& comparison, int width, int height )
{
    m_loadingSurface = false;
    m_draw.Clear();
    m_buttonCount = 0;
    m_sidebar = m_layout.details;
    m_comparisonCombo.SetPopupViewport( m_layout.window );
    if ( m_layout.controls.w > 0 )
    {
        ComposeShellControls( comparison );
    }
    else
    {
        m_comparisonCombo.Close();
        m_comparisonCombo.SetBounds( 0, 0, 0, 0 );
    }
    if ( comparison.Active() && m_layout.details.w > 0 )
    {
        ComposeShellDetails( comparison );
    }
    if ( comparison.Active() )
    {
        ComposeShellTransport( comparison );
    }
    else
    {
        m_timeline = {};
        m_draw.PushClip( m_layout.viewport );
        m_draw.AddText( { m_layout.viewport.x + 24, m_layout.viewport.y + 48 }, 18, ink, "Solver Lab" );
        m_draw.AddText( { m_layout.viewport.x + 24, m_layout.viewport.y + 80 }, 12, muted,
                        "Open Details to load a comparison, or use Scenes > Solver Lab library." );
        m_draw.PopClip();
    }
    // The library popup is foreground content, using the same handler as the
    // previous comparison surface. Hidden panes never keep an active popup.
    if ( m_layout.controls.w > 0 )
    {
        const UI::UIDrawContext draw( width, height, m_draw );
        draw.PushClip( m_layout.controls );
        const int selected = SelectedSolverLab( comparison );
        static const char* compactOptions[] = { "Ragdoll", "Wall" };
        const auto options = m_layout.controls.w < 180.0f ? std::span<const char* const>( compactOptions )
                                                          : std::span<const char* const>( comparisonOptions );
        m_comparisonCombo.Draw( draw, "Comparison", { options, selected, 0, selected >= 0 ? options[selected] : "Load" },
                                m_pointer );
        draw.PopClip();
    }
    // Invariant: popups shed their parent pane clip before the foreground pass,
    // using the same extraction contract as GameUI.
    m_draw.ExtractForeground( m_foreground );
    m_draw.Append( m_foreground );
    return m_draw;
}

void PhysicsComparisonPanel::ComposeShellControls( const PhysicsComparison& comparison )
{
    m_buttonClip = m_layout.controls;
    m_draw.PushClip( m_buttonClip );
    m_controlsScroll = std::clamp( m_controlsScroll, 0.0f, (std::max)( 0.0f, 460.0f - m_buttonClip.h ) );
    const float x = m_buttonClip.x + 10, y = m_buttonClip.y + 10 - m_controlsScroll;
    const float w = (std::max)( 1.0f, m_buttonClip.w - 20 ), half = ( w - 6 ) * 0.5f;
    m_comparisonCombo.SetBounds( x, y + 22, w, 24 );
    m_comparisonCombo.SetLabelVisible( false );
    m_draw.AddText( { x, y }, 14, ink, "Comparison" );
    ButtonAt( { x, y + 56, half, 28 }, "Open file", 1 );
    ButtonAt( { x + half + 6, y + 56, half, 28 }, "Load finding", 4 );
    if ( comparison.Active() )
    {
        ButtonAt( { x, y + 90, half, 28 }, "Save finding", 3 );
        ButtonAt( { x + half + 6, y + 90, half, 28 }, "Close comparison", 5 );
        m_draw.AddText( { x, y + 132 }, 12, muted, "View" );
        const char* modes[] = { "Split", "Overlay", "Toggle", "Heatmap", "Pixels" };
        for ( int i = 0; i < 5; ++i )
        {
            ButtonAt( { x + static_cast<float>( i % 2 ) * ( half + 6 ), y + 152 + static_cast<float>( i / 2 ) * 34, half,
                        28 },
                      modes[i], 20 + i, static_cast<int>( comparison.Settings().display ) == i );
        }
        ButtonAt( { x + half + 6, y + 220, half, 28 }, comparison.Settings().showA ? "Show B" : "Show A", 11 );
        ButtonAt( { x, y + 254, half, 28 }, comparison.Settings().stackedViews ? "Stacked" : "Side by side", 14 );
        ButtonAt( { x + half + 6, y + 254, half, 28 }, comparison.Settings().occludedOutline ? "X-ray on" : "X-ray off",
                  12 );
        ButtonAt( { x, y + 288, half, 28 }, "Focus", 2 );
        ButtonAt( { x + half + 6, y + 288, half, 28 }, "Follow A", 10, comparison.Settings().followA );
        ButtonAt( { x, y + 322, half, 28 }, "Reverse", 31, comparison.Direction() < 0 );
        char speed[48];
        std::snprintf( speed, sizeof( speed ), "Speed %.2gx", comparison.Settings().speed );
        ButtonAt( { x + half + 6, y + 322, half, 28 }, speed, 34 );
        ButtonAt( { x, y + 356, w, 28 }, "Loop selected range", 35, comparison.LoopEnabled() );
        for ( int side = 0; side < 2; ++side )
        {
            char coverage[150];
            std::snprintf( coverage, sizeof( coverage ), "%s: %s; %s", side ? "B" : "A",
                           comparison.Recording( side ).Frame( comparison.Tick() ) ? "motion recorded" : "NO TICK COVERAGE",
                           comparison.Recording( side ).Evidence( comparison.Tick() )      ? "manifold"
                           : comparison.Recording( side ).Observation( comparison.Tick() ) ? "summary"
                                                                                           : "no contact evidence" );
            m_draw.AddText( { x, y + 402 + side * 20.0f }, 10, side ? coral : cyan, coverage );
        }
    }
    m_draw.PopClip();
}

void PhysicsComparisonPanel::ComposeShellDetails( const PhysicsComparison& comparison )
{
    m_buttonClip = m_layout.details;
    m_draw.PushClip( m_buttonClip );
    m_detailsScroll = std::clamp( m_detailsScroll, 0.0f, (std::max)( 0.0f, 450.0f - m_buttonClip.h ) );
    const float x = m_buttonClip.x + 10, top = m_buttonClip.y + 10 - m_detailsScroll;
    const float w = (std::max)( 1.0f, m_buttonClip.w - 20 ), half = ( w - 6 ) * 0.5f;
    ButtonAt( { x, top, half, 28 }, "Selected objects", 6, comparison.Settings().selectedOnly );
    ButtonAt( { x + half + 6, top, half, 28 }, "All differences", 7, !comparison.Settings().selectedOnly );
    ButtonAt( { x, top + 34, half, 28 }, "Differences only", 8, comparison.Settings().differencesOnly );
    ButtonAt( { x + half + 6, top + 34, half, 28 }, "First difference", 9 );
    ButtonAt( { x, top + 68, w, 28 }, "Cycle position threshold", 13 );
    char text[240];
    std::snprintf( text, sizeof( text ), "Threshold %.5g m / %.3g deg", comparison.Settings().positionThreshold,
                   comparison.Settings().angleThreshold );
    m_draw.AddText( { x, top + 106 }, 11, muted, text );
    m_draw.AddText( { x, top + 124 }, 11, muted, "Recorded divergence;" );
    m_draw.AddText( { x, top + 141 }, 11, muted, "not a proven root cause" );
    const float contentHeight = (std::max)( 450.0f, m_buttonClip.h );
    float y = top + 166;
    m_eventList = { x, y, w, contentHeight - 411.0f + 26.0f };
    int skipped = 0;
    for ( std::size_t i = 0; i < comparison.Events().size() && y < top + contentHeight - 245; ++i )
    {
        const auto& event = comparison.Events()[i];
        if ( !comparison.VisibleEvent( event ) || skipped++ < m_eventOffset )
        {
            continue;
        }
        std::snprintf( text, sizeof( text ), "%d | #%llu %s %s", event.tick, event.bodyA,
                       event.family == ComparisonFamily::SolverIteration ? "solver iteration"
                       : event.family == ComparisonFamily::Contact || event.family == ComparisonFamily::Terrain ? "contact"
                       : event.family == ComparisonFamily::Sleep                                                ? "sleep"
                                                                                                                : "motion",
                       ChangeName( event.change ) );
        ButtonAt( { x, y, w, 26 }, text, 1000 + static_cast<int>( i ), comparison.SelectedEvent() == static_cast<int>( i ) );
        y += 31;
    }
    if ( comparison.Selected() )
    {
        const float plotsY = (std::max)( y + 8, top + contentHeight - 215 );
        const auto delta = comparison.Difference( comparison.Selected(), comparison.Tick() );
        std::snprintf( text, sizeof( text ), "#%llu  d=%.5g m  rot=%.4g deg", comparison.Selected(), delta.distance,
                       delta.angleDegrees );
        m_draw.AddText( { x, plotsY }, 11, ink, text );
        Plot( comparison, { x, plotsY + 22, w, 76 }, false );
        Plot( comparison, { x, plotsY + 106, w, 76 }, true );
    }
    m_draw.PopClip();
}

void PhysicsComparisonPanel::ComposeShellTransport( const PhysicsComparison& comparison )
{
    m_buttonClip = m_layout.transport;
    m_draw.PushClip( m_buttonClip );
    const float x = m_buttonClip.x, y = m_buttonClip.y;
    const bool compact = m_buttonClip.w < 220.0f;
    const float scale = compact ? m_buttonClip.w / 220.0f : 1.0f;
    ButtonAt( { x, y + 2, 28 * scale, 24 }, "<", 30 );
    ButtonAt( { x + 30 * scale, y + 2, 48 * scale, 24 },
              compact ? ( comparison.Direction() ? "||" : ">" ) : ( comparison.Direction() ? "Pause" : "Play" ), 32 );
    ButtonAt( { x + 80 * scale, y + 2, 28 * scale, 24 }, ">", 33 );
    const float labelWidth = m_buttonClip.w >= 380 ? 152.0f : 0.0f;
    // The visible track is four pixels high; the remaining transport is its hit target.
    m_timeline = { x + 116 * scale, y, (std::max)( 1.0f, m_buttonClip.w - 124 * scale - labelWidth ), m_buttonClip.h };
    const float filled = m_timeline.w * comparison.Tick() / (std::max)( 1, comparison.LastTick() );
    m_draw.AddRoundedRect( { m_timeline.x, y + 12, m_timeline.w, 4 }, 2, { 0.22f, 0.24f, 0.27f, 1 } );
    m_draw.AddRoundedRect( { m_timeline.x, y + 12, filled, 4 }, 2, cyan );
    if ( labelWidth > 0 )
    {
        char label[80];
        std::snprintf( label, sizeof( label ), "Tick %d / %d  %.3fs", comparison.Tick(), comparison.LastTick(),
                       comparison.Tick() / 120.0 );
        m_draw.AddText( { m_timeline.x + m_timeline.w + 8, y + 8 }, 10, ink, label );
    }
    m_draw.PopClip();
}


namespace
{
UI::UITooltipText ComparisonActionTooltip( int action )
{
    switch ( action )
    {
    case 1:
        return { "Load a comparison bundle through the existing file dialog.", "", "F8" };
    case 2:
        return { "Frame the selected object or recorded contact in both views." };
    case 3:
        return { "Save this comparison's tick, selection, camera and view settings as a finding." };
    case 4:
        return { "Load a saved finding and restore its comparison inspection state." };
    case 5:
        return { "Close this comparison and release its recordings and geometry." };
    case 6:
        return { "Show recorded differences involving the selected object." };
    case 7:
        return { "Show recorded differences for all objects." };
    case 8:
        return { "Filter the event list to differences between A and B." };
    case 9:
        return { "Seek the first visible recorded difference." };
    case 10:
        return { "Move the comparison camera with the selected object's recorded A position." };
    case 11:
        return { "Choose which recording is shown by Toggle or Heatmap view." };
    case 12:
        return { "Show comparison outlines through occluding objects." };
    case 13:
        return { "Cycle the position-difference threshold.", "Metres; angle threshold is in degrees" };
    case 14:
        return { "Arrange the paired views side by side or vertically stacked." };
    case 20:
        return { "Render A and B in separate views with the same camera." };
    case 21:
        return { "Overlay both recorded states in one view." };
    case 22:
        return { "Display one recording at a time using Show A / Show B." };
    case 23:
        return { "Colour recorded position or angular differences using the current heat scale." };
    case 24:
        return { "Display the pixel difference between the two controlled renders." };
    case 30:
        return { "Step backward by one exact tick.", "120 ticks per second", "Left arrow" };
    case 31:
        return { "Play the recording in reverse at the selected speed." };
    case 32:
        return { "Pause playback, or play forward from the current tick.", "", "Space" };
    case 33:
        return { "Step forward by one exact tick.", "120 ticks per second", "Right arrow" };
    case 34:
        return { "Cycle comparison playback speed from 0.25x to 4x.", "Multiplier of recorded time" };
    case 35:
        return { "Enable or disable looping over the current tick range." };
    default:
        return { "Select this recorded divergence and seek its evidence tick. It does not prove a root cause." };
    }
}
} // namespace

std::array<UI::UITooltipTarget, 66> PhysicsComparisonPanel::Tooltips() const
{
    std::array<UI::UITooltipTarget, 66> result {};
    if ( m_comparisonCombo.IsOpen() )
    {
        return result;
    }
    for ( std::size_t i = 0; i < m_buttonCount; ++i )
    {
        const Button& button = m_buttons[i];
        result[i] = { 0x20000000u + static_cast<uint32_t>( button.action ), button.bounds,
                      m_loadingSurface ? UI::UITooltipText { "Cancel the pending load or dismiss its error." }
                                       : ComparisonActionTooltip( button.action ) };
    }
    result[64] =
        { 0x21000000u,
          m_comparisonCombo.Bounds(),
          { "Choose an existing Solver Lab library comparison. Replacing it releases the current recordings first." } };
    result[65] = { 0x21000001u,
                   m_timeline,
                   { "Drag to seek the paired recordings. Use arrow keys for exact single-tick changes.",
                     "120 ticks per second", "Left / Right arrow" } };
    return result;
}
