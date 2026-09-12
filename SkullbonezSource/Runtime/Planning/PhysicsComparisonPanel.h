#pragma once
#include "PhysicsComparison.h"
#include "../../Physics/ColliderStore.h"
#include "../../Rendering/PairedViewRenderer.h"
#include "../../UI/UIInput.h"
#include "../../UI/UIComboBox.h"
#include "../../UI/UITooltip.h"

namespace SkullbonezCore::Runtime
{
enum class ComparisonPanelAction
{
    None,
    Open,
    Focus,
    Save,
    Restore,
    Close,
    RagdollWall,
    WallOnly
};
struct ComparisonPanelLayout
{
    UI::UIRect viewport, controls, details, transport, window;
    bool shared = false;
};

class PhysicsComparisonPanel
{
  public:
    std::array<UI::UITooltipTarget, 66> Tooltips() const;
    void ReleaseComparison();
    bool HasOpenPopup() const
    {
        return m_comparisonCombo.IsOpen();
    }
    UI::UIRect LibraryPopupBounds() const
    {
        return m_comparisonCombo.DropdownBounds( 2 );
    }
    void CancelInput()
    {
        m_comparisonCombo.Close();
        m_comboConsumedPointer = false;
    }
    void SetPresentationLayout( const ComparisonPanelLayout& layout )
    {
        m_layout = layout;
    }
    void Prepare( const Physics::ColliderStore& colliders, const Rendering::RenderInstanceStore& instances );
    Rendering::PairedViewFrame BuildFrame( const PhysicsComparison& comparison, int width, int height );
    const UI::UIDrawList& Compose( const PhysicsComparison& comparison, int width, int height );
    ComparisonPanelAction Input( PhysicsComparison& comparison, const UI::InputControl::UIInputSnapshot& input, bool timelineDrag );
    UI::UIRect TimelineBounds() const
    {
        return m_timeline;
    }
    bool TimelineContains( int x, int y ) const
    {
        return m_timeline.Contains( x, y );
    }
    bool Contains( int x, int y ) const;
    const UI::UIDrawList& ComposeLoading( int width, int height, int percent, const char* error, const char* phase );
    double Advance( PhysicsComparison& comparison, double now );
    float Radius( uint64_t id ) const noexcept;
    bool ContactPivot( const PhysicsComparison& comparison, Math::Vector::Vector3& pivot ) const;
    uint64_t Pick( const PhysicsComparison& comparison, const Math::Vector::Vector3& origin, const Math::Vector::Vector3& direction, int side ) const;

  private:
    struct Shape
    {
        uint64_t id;
        Physics::ColliderRecord collider;
        Rendering::RenderMaterial material;
        std::size_t geometryIndex = 0;
    };
    struct Button
    {
        UI::UIRect bounds;
        int action;
    };
    const UI::UIDrawList& ComposeShell( const PhysicsComparison& comparison, int width, int height );
    void ComposeShellControls( const PhysicsComparison& comparison );
    void ComposeShellDetails( const PhysicsComparison& comparison );
    void ComposeShellTransport( const PhysicsComparison& comparison );
    void ComposeViewLabels( const PhysicsComparison& comparison );
    std::size_t RetainGeometry( const Physics::ColliderRecord& collider );
    void RebindGeometry();
    void BuildModels( const PhysicsComparison& comparison );
    std::array<Rendering::ContactManifoldPresentation, 2> BuildContacts( const PhysicsComparison& comparison ) const;
    void ButtonAt( UI::UIRect bounds, const char* text, int action, bool active = false );
    void Plot( const PhysicsComparison& comparison, UI::UIRect bounds, bool velocity );
    std::vector<Shape> m_shapes;
    std::vector<Math::CollisionDetection::BoundingSphere> m_spheres;
    std::vector<Math::CollisionDetection::BoundingBox> m_boxes;
    std::vector<Math::CollisionDetection::ConvexHullShape> m_hulls;
    std::array<std::vector<Rendering::ModelViewItem>, 2> m_models;
    UI::UIDrawList m_draw;
    UI::UIDrawList m_foreground;
    std::array<Button, 64> m_buttons {};
    std::size_t m_buttonCount = 0;
    UI::UIRect m_sidebar, m_timeline, m_eventList;
    ComparisonPanelLayout m_layout;
    UI::UIRect m_buttonClip;
    float m_controlsScroll = 0.0f;
    float m_detailsScroll = 0.0f;
    UI::UIComboBox m_comparisonCombo;
    UI::UIPointerPosition m_pointer { -1, -1 };
    bool m_comboConsumedPointer = false;
    bool m_loadingSurface = false;
    double m_lastTime = 0;
    double m_contactPulseStarted = -1;
    uint64_t m_contactSelectionRevision = 0;
    int m_contactTick = -1;
    int m_eventOffset = 0;
};
} // namespace SkullbonezCore::Runtime
