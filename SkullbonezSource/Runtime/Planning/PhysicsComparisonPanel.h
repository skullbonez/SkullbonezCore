#pragma once
#include "PhysicsComparison.h"
#include "../../Physics/ColliderStore.h"
#include "../../Rendering/PairedViewRenderer.h"
#include "../../UI/UIInput.h"
#include "../../UI/UIComboBox.h"

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
class PhysicsComparisonPanel
{
  public:
    void Prepare( const Physics::ColliderStore& colliders, const Rendering::RenderInstanceStore& instances );
    Rendering::PairedViewFrame BuildFrame( const PhysicsComparison& comparison, int width, int height );
    const UI::UIDrawList& Compose( const PhysicsComparison& comparison, int width, int height );
    ComparisonPanelAction Input( PhysicsComparison& comparison, const UI::InputControl::UIInputSnapshot& input,
                                 bool timelineDrag );
    bool TimelineContains( int x, int y ) const
    {
        return m_timeline.Contains( x, y );
    }
    bool Contains( int x, int y ) const;
    const UI::UIDrawList& ComposeLoading( int width, int height, int percent, const char* error, const char* phase );
    double Advance( PhysicsComparison& comparison, double now );
    float Radius( uint64_t id ) const noexcept;
    bool ContactPivot( const PhysicsComparison& comparison, Math::Vector::Vector3& pivot ) const;
    uint64_t Pick( const PhysicsComparison& comparison, const Math::Vector::Vector3& origin,
                   const Math::Vector::Vector3& direction, int side ) const;

  private:
    struct Shape
    {
        uint64_t id;
        Physics::ColliderRecord collider;
        Rendering::RenderMaterial material;
    };
    struct Button
    {
        UI::UIRect bounds;
        int action;
    };
    void BuildModels( const PhysicsComparison& comparison );
    std::array<Rendering::ContactManifoldPresentation, 2> BuildContacts( const PhysicsComparison& comparison ) const;
    void ButtonAt( UI::UIRect bounds, const char* text, int action, bool active = false );
    void Plot( const PhysicsComparison& comparison, UI::UIRect bounds, bool velocity );
    std::vector<Shape> m_shapes;
    std::array<std::vector<Rendering::ModelViewItem>, 2> m_models;
    UI::UIDrawList m_draw;
    std::array<Button, 64> m_buttons {};
    std::size_t m_buttonCount = 0;
    UI::UIRect m_sidebar, m_timeline;
    UI::UIComboBox m_comparisonCombo;
    UI::UIPointerPosition m_pointer { -1, -1 };
    bool m_comboConsumedPointer = false;
    double m_lastTime = 0;
    double m_contactPulseStarted = -1;
    uint64_t m_contactSelectionRevision = 0;
    int m_contactTick = -1;
    int m_eventOffset = 0;
};
} // namespace SkullbonezCore::Runtime
