// File: Agentic/Tests/UiBoundaryUnitTests/UiBoundaryUnitTests.cpp
// Purpose:
//   Proves the UI foundation can render its components without linking Runtime,
//   Rendering, product composition, or a graphics backend.
//
// Summary:
//   This executable records deterministic component states through the public
//   backend-neutral foundation API. Its project references only the UI static
//   library, so a successful link proves product composition remains above it.
//
// Glossary:
//   Detached frame: Immutable presentation values assembled by an upper owner.
//   Fingerprint: Stable hash of ordered backend-neutral draw commands and text.
//   Boundary probe: A small executable whose successful link proves forbidden
//   implementation dependencies are absent.
//
// Invariants:
//   - The project must not compile or link Runtime, Rendering, or DX12 sources.
//   - Component hit geometry, retained combo state, and recorded presentation
//     states must remain coherent without an upper-layer interaction owner.
//   - Stateless components consume explicit visible/enabled/hovered/focused/
//     active/selected/checked state and never rediscover it from pointer input.
//   - Resting and engaged component fixtures must retain their exact command
//     fingerprints without overflowing a bounded draw buffer.
//   - Established wrappers and direct stateless calls must produce identical
//     command streams and interaction values.
//   - No bounded draw buffer may overflow while producing the fixture.
//
// Related:
//   - Agentic/Reference/engine-glossary.md
//   - SkullbonezSource/UI/UIDraw.h
//   - tools/validate_ui_boundary_tests.bat

#include "UI/UIButton.h"
#include "UI/UICheckBox.h"
#include "UI/UIComboBox.h"
#include "UI/UIDraw.h"
#include "UI/UIDrawList.h"
#include "UI/UIDrawWidgets.h"
#include "UI/UIIconButton.h"
#include "UI/UIScrollBar.h"
#include "UI/UISlider.h"
#include "UI/UIStyle.h"
#include "UI/UITabBar.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>

namespace
{
using SkullbonezCore::UI::UIButton;
using SkullbonezCore::UI::UICheckBox;
using SkullbonezCore::UI::UIComboBox;
using SkullbonezCore::UI::UIDrawContext;
using SkullbonezCore::UI::UIDrawList;
using SkullbonezCore::UI::UIIconButton;
using SkullbonezCore::UI::UIRect;
using SkullbonezCore::UI::UIScrollBar;
using SkullbonezCore::UI::UISlider;
using SkullbonezCore::UI::UITabBar;
using SkullbonezCore::UI::UIVisualState;
namespace Style = SkullbonezCore::UI::Style;
namespace Widgets = SkullbonezCore::UI::Widgets;

constexpr uint64_t kExpectedRestingComponentFingerprint = 9585956286470253977ull;
constexpr uint64_t kExpectedEngagedComponentFingerprint = 15522795272601894673ull;
constexpr std::array<uint64_t, 8> kExpectedStatelessStateFingerprints = {
    9138081368605736749ull, 49288575029089482ull,   211756514920079498ull,   6302452228787434232ull,
    2904659679807374515ull, 4645013559851399903ull, 13061036390687143437ull, 5558979605539197941ull,
};

// The fixture contains only production-reachable component operations,
// including the compact panel and button adopted by Runtime owners.
constexpr uint64_t kExpectedStatelessFixtureFingerprint = 7518969973781773681ull;

bool NearlyEqual( float left, float right )
{
    return std::fabs( left - right ) < 0.0001f;
}


bool SameRect( const UIRect& left, const UIRect& right )
{
    return NearlyEqual( left.x, right.x ) && NearlyEqual( left.y, right.y ) && NearlyEqual( left.w, right.w ) &&
           NearlyEqual( left.h, right.h );
}


bool SameCommandGeometry( const UIDrawList::Command& left, const UIDrawList::Command& right )
{
    return left.type == right.type && NearlyEqual( left.x0, right.x0 ) && NearlyEqual( left.y0, right.y0 ) &&
           NearlyEqual( left.x1, right.x1 ) && NearlyEqual( left.y1, right.y1 ) && NearlyEqual( left.x2, right.x2 ) &&
           NearlyEqual( left.y2, right.y2 ) && NearlyEqual( left.w, right.w ) && NearlyEqual( left.h, right.h ) &&
           NearlyEqual( left.radius, right.radius ) && NearlyEqual( left.pxSize, right.pxSize );
}


bool CommandHasColor( const UIDrawList::Command& command, const Style::UIColor& color, float alpha )
{
    return NearlyEqual( command.r, color.r ) && NearlyEqual( command.g, color.g ) && NearlyEqual( command.b, color.b ) &&
           NearlyEqual( command.a, alpha );
}


bool CheckComponentBaselines()
{
    // Invariant: geometry stays in each component, while selection, checked,
    // slider value, and pointer-derived presentation are disposable caller
    // values. Combo openness is the one deliberately retained popup invariant
    // exercised here; future value-state work must preserve that distinction.
    UIButton button;
    button.SetBounds( 10.0f, 10.0f, 120.0f, 28.0f );
    UICheckBox checkBox;
    checkBox.SetBounds( 10.0f, 50.0f, 160.0f, 28.0f );
    UIComboBox comboBox;
    comboBox.SetBounds( 10.0f, 90.0f, 180.0f, 28.0f );
    UIIconButton iconButton;
    iconButton.SetBounds( 210.0f, 10.0f, 24.0f, 24.0f );
    UISlider slider;
    slider.SetBounds( 10.0f, 190.0f, 360.0f, 34.0f );
    UIScrollBar scrollBar;
    scrollBar.SetBounds( 382.0f, 10.0f, 8.0f, 210.0f );
    UITabBar tabBar;
    tabBar.SetBounds( 10.0f, 240.0f, 360.0f, 50.0f );

    constexpr UIVisualState kEnabled = UIVisualState::Visible | UIVisualState::Enabled;
    constexpr Widgets::ComponentAppearance kEstablished = Widgets::ComponentAppearance::Established;
    const UIRect buttonBounds = button.Bounds();
    const UIRect checkBoxBounds = checkBox.Bounds();
    const UIRect comboBounds = comboBox.Bounds();
    const UIRect iconBounds = { 210.0f, 10.0f, 24.0f, 24.0f };
    const UIRect sliderBounds = slider.Bounds();
    const UIRect scrollBounds = scrollBar.Bounds();
    const UIRect tabBounds = tabBar.Bounds();
    const Widgets::ComboLayout comboLayout = Widgets::ResolveComboLayout( comboBounds, true, false, 3 );
    const Widgets::TabLayout adaptiveSecondTab = Widgets::ResolveTabLayout( tabBounds, 1, 3 );
    const Widgets::TabLayout establishedSecondTab = Widgets::ResolveTabLayout( tabBounds, 1, 3, kEstablished );
    const UIRect expectedSecondTabInteraction = { 130.0f, 240.0f, 120.0f, 50.0f };
    const UIRect expectedSecondTabVisual = { 132.0f, 251.0f, 112.0f, 30.0f };

    comboBox.SetOpen( false );
    const bool geometryValid = button.HitTest( 20, 20 ) && !button.HitTest( 200, 20 ) && checkBox.HitTest( 20, 60 ) &&
                               !checkBox.HitTest( 200, 60 ) && comboBox.HitBox( 20, 100 ) &&
                               comboBox.HitOption( 80, 157, 3 ) == -1 && iconButton.HitTest( 220, 20 ) &&
                               !iconButton.HitTest( 250, 20 ) && slider.HitTest( 20, 200 ) && !slider.HitTest( 400, 200 ) &&
                               std::fabs( slider.ValueFromMouse( 0, 0.0f, 1.0f, 0.25f ) - 0.0f ) < 0.0001f &&
                               std::fabs( slider.ValueFromMouse( 213, 0.0f, 1.0f, 0.25f ) - 0.5f ) < 0.0001f &&
                               std::fabs( slider.ValueFromMouse( 640, 0.0f, 1.0f, 0.25f ) - 1.0f ) < 0.0001f &&
                               tabBar.HitTest( 190, 260, 3 ) == 1 && tabBar.HitTest( 400, 260, 3 ) == -1;

    // Negative control: wrappers and stateless operations must emit the same
    // interaction values, including the established strip's exact boundary
    // ownership. The two named rectangles are resolved by one layout operation:
    // adaptive geometry cannot diverge, while the established inset is pinned
    // as an explicit compatibility contract instead of a hidden hit/draw split.
    // A wrapper-local hit, inset, or quantization path fails these comparisons
    // even when ordinary center-point tests still pass.
    bool commandValuesValid = button.HitTest( 20, 20 ) == Widgets::CanActivateComponent( buttonBounds, kEnabled, 20, 20 ) &&
                              checkBox.HitTest( 20, 60 ) ==
                                  Widgets::CanActivateComponent( checkBoxBounds, kEnabled, 20, 60 ) &&
                              NearlyEqual( slider.ValueFromMouse( 213, 0.0f, 1.0f, 0.25f ),
                                           Widgets::SliderValueFromPointer( sliderBounds, 213, 0.0f, 1.0f, 0.25f ) ) &&
                              tabBar.HitTest( 130, 260, 3 ) ==
                                  Widgets::HitTestTab( tabBounds, kEnabled, 130, 260, 3, kEstablished ) &&
                              tabBar.HitTest( 130, 260, 3 ) == 1 &&
                              SameRect( adaptiveSecondTab.interactionBounds, adaptiveSecondTab.visualBounds ) &&
                              SameRect( establishedSecondTab.interactionBounds, expectedSecondTabInteraction ) &&
                              SameRect( establishedSecondTab.visualBounds, expectedSecondTabVisual ) &&
                              SameRect( adaptiveSecondTab.visualBounds, establishedSecondTab.visualBounds ) &&
                              !SameRect( establishedSecondTab.interactionBounds, establishedSecondTab.visualBounds ) &&
                              Widgets::ContainsComponent( establishedSecondTab.interactionBounds, kEnabled, 130, 260 ) &&
                              !Widgets::ContainsComponent( establishedSecondTab.visualBounds, kEnabled, 130, 260 ) &&
                              SameRect( Widgets::ScrollThumbBounds( scrollBounds, 420.0f, 210.0f, 105.0f ),
                                        Widgets::ScrollThumbBounds( scrollBounds, 420.0f, 210.0f, 105.0f, kEstablished ) ) &&
                              SameRect( comboLayout.interactionBounds, comboBounds ) &&
                              SameRect( comboBox.DropdownBounds( 3 ), comboLayout.popupBounds ) &&
                              comboBox.HitBox( 20, 100 ) ==
                                  Widgets::CanActivateComponent( comboLayout.interactionBounds, kEnabled, 20, 100 ) &&
                              Widgets::ContainsComponent( comboLayout.interactionBounds, kEnabled, 20, 100 ) &&
                              !Widgets::ContainsComponent( comboLayout.fieldBounds, kEnabled, 20, 100 );
    comboBox.SetOpen( true );
    commandValuesValid = commandValuesValid &&
                         comboBox.HitOption( 80, 157, 3 ) ==
                             Widgets::ComboOptionAtPointer( comboLayout.popupBounds, kEnabled, 80, 157, 3 );
    comboBox.SetOpen( false );

    static constexpr const char* kComboOptions[] = { "Alpha", "Beta", "Gamma" };
    static constexpr const char* kTabLabels[] = { "One", "Two", "Three" };
    auto drawList = std::make_unique<UIDrawList>();
    UIDrawContext draw( 640, 360, *drawList );

    auto recordFixture = [&]( bool engaged )
    {
        drawList->Clear();
        comboBox.SetOpen( engaged );
        button.Draw( draw, "Apply", engaged ? 20 : 600, engaged ? 20 : 340 );
        checkBox.DrawToggle( draw, "Enabled", engaged, 0.20f, 0.60f, 0.90f );
        comboBox.Draw( draw, "Mode", kComboOptions, static_cast<int>( std::size( kComboOptions ) ), 1, engaged ? 80 : 600,
                       engaged ? 157 : 340, 1u );
        iconButton.DrawExpander( draw, engaged );
        slider.Draw( draw, "Strength", engaged ? "0.75" : "0.25", engaged ? 0.75f : 0.25f, 0.0f, 1.0f );
        scrollBar.Draw( draw, 420.0f, 210.0f, 105.0f, engaged ? 2.0 : 0.0, 1.5 );
        tabBar.Draw( draw, kTabLabels, static_cast<int>( std::size( kTabLabels ) ), engaged ? 1 : 0 );
        return drawList->Fingerprint();
    };

    const uint64_t restingFingerprint = recordFixture( false );
    const auto restingStats = drawList->GetStats();
    const uint64_t engagedFingerprint = recordFixture( true );
    const auto engagedStats = drawList->GetStats();

    auto recordStatelessFixture = [&]( bool engaged )
    {
        drawList->Clear();
        UIVisualState buttonState = kEnabled;

        if ( engaged )
        {
            buttonState |= UIVisualState::Hovered;
        }

        Widgets::DrawButton( draw, buttonBounds, "Apply", buttonState, kEstablished );
        Widgets::DrawToggle( draw, checkBoxBounds, "Enabled", { 0.20f, 0.60f, 0.90f, 1.0f },
                             engaged ? kEnabled | UIVisualState::Checked : kEnabled, kEstablished );

        UIVisualState comboState = kEnabled;
        Widgets::DrawComboField( draw, comboLayout, "Mode", "Beta", true, engaged, comboState, true, kEstablished );

        if ( engaged )
        {
            const int hoveredOption = Widgets::ComboOptionAtPointer( comboLayout.popupBounds, comboState, 80, 157, 3 );
            Widgets::DrawComboPopup( draw, comboLayout, kComboOptions, static_cast<int>( std::size( kComboOptions ) ), 1,
                                     hoveredOption, 1u, comboState, kEstablished );
        }

        Widgets::DrawIconButton( draw, iconBounds, engaged ? Widgets::ComponentIcon::Minus : Widgets::ComponentIcon::Plus,
                                 kEnabled, kEstablished );
        Widgets::DrawSlider( draw, sliderBounds, "Strength", engaged ? "0.75" : "0.25", engaged ? 0.75f : 0.25f, 0.0f, 1.0f,
                             kEnabled, kEstablished );
        Widgets::DrawScrollBar( draw, scrollBounds, 420.0f, 210.0f, 105.0f, engaged ? 0.5f : 0.0f, kEnabled, kEstablished );

        for ( int tabIndex = 0; tabIndex < static_cast<int>( std::size( kTabLabels ) ); ++tabIndex )
        {
            UIVisualState tabState = kEnabled;

            if ( tabIndex == ( engaged ? 1 : 0 ) )
            {
                tabState |= UIVisualState::Selected;
            }

            const Widgets::TabLayout layout = Widgets::ResolveTabLayout( tabBounds, tabIndex, 3, kEstablished );
            Widgets::DrawTab( draw, layout.visualBounds, kTabLabels[tabIndex], tabState, kEstablished );
        }

        return drawList->Fingerprint();
    };

    const uint64_t directRestingFingerprint = recordStatelessFixture( false );
    const auto directRestingStats = drawList->GetStats();
    const uint64_t directEngagedFingerprint = recordStatelessFixture( true );
    const auto directEngagedStats = drawList->GetStats();
    const bool overflow = restingStats.commandOverflow || restingStats.textOverflow || restingStats.clipOverflow ||
                          engagedStats.commandOverflow || engagedStats.textOverflow || engagedStats.clipOverflow ||
                          directRestingStats.commandOverflow || directRestingStats.textOverflow ||
                          directRestingStats.clipOverflow || directEngagedStats.commandOverflow ||
                          directEngagedStats.textOverflow || directEngagedStats.clipOverflow;
    const bool fingerprintValid = restingFingerprint == kExpectedRestingComponentFingerprint &&
                                  engagedFingerprint == kExpectedEngagedComponentFingerprint;
    const bool wrapperParityValid = directRestingFingerprint == restingFingerprint &&
                                    directEngagedFingerprint == engagedFingerprint;

    if ( geometryValid && commandValuesValid && fingerprintValid && wrapperParityValid && !overflow )
    {
        return true;
    }

    std::fprintf( stderr,
                  "FAIL: UI component baseline geometry=%d commands=%d parity=%d resting=%llu/%llu/%llu "
                  "engaged=%llu/%llu/%llu overflow=%d\n",
                  geometryValid ? 1 : 0, commandValuesValid ? 1 : 0, wrapperParityValid ? 1 : 0,
                  static_cast<unsigned long long>( restingFingerprint ),
                  static_cast<unsigned long long>( directRestingFingerprint ),
                  static_cast<unsigned long long>( kExpectedRestingComponentFingerprint ),
                  static_cast<unsigned long long>( engagedFingerprint ),
                  static_cast<unsigned long long>( directEngagedFingerprint ),
                  static_cast<unsigned long long>( kExpectedEngagedComponentFingerprint ), overflow ? 1 : 0 );
    return false;
}


bool CheckComponentEdgeContracts()
{
    constexpr UIVisualState kEnabled = UIVisualState::Visible | UIVisualState::Enabled;
    constexpr UIVisualState kDisabled = UIVisualState::Visible;
    constexpr Widgets::ComponentAppearance kEstablished = Widgets::ComponentAppearance::Established;
    constexpr Widgets::ComponentAppearance kFooter = Widgets::ComponentAppearance::Footer;
    const UIRect comboBounds = { 10.0f, 90.0f, 180.0f, 28.0f };
    const Widgets::ComboLayout labelled = Widgets::ResolveComboLayout( comboBounds, true, false, 3 );
    const Widgets::ComboLayout labelHidden = Widgets::ResolveComboLayout( comboBounds, false, false, 3 );
    const Widgets::ComboLayout dropUp = Widgets::ResolveComboLayout( comboBounds, true, true, 3 );
    const Widgets::ComboLayout zeroOptions = Widgets::ResolveComboLayout( comboBounds, true, false, 0 );
    const Widgets::ComboLayout negativeOptions = Widgets::ResolveComboLayout( comboBounds, true, false, -3 );

    UIComboBox comboBox;
    comboBox.SetBounds( comboBounds.x, comboBounds.y, comboBounds.w, comboBounds.h );

    // Negative control: the labelled wrapper deliberately activates its
    // label column although only fieldBounds is drawn as the control. Every
    // wrapper projection must equal the one resolved layout value.
    const bool labelColumnValid = SameRect( labelled.interactionBounds, comboBounds ) && comboBox.HitBox( 20, 100 ) &&
                                  Widgets::CanActivateComponent( labelled.interactionBounds, kEnabled, 20, 100 ) &&
                                  !Widgets::ContainsComponent( labelled.fieldBounds, kEnabled, 20, 100 ) &&
                                  SameRect( comboBox.DropdownBounds( 3 ), labelled.popupBounds );

    comboBox.SetLabelVisible( false );
    const bool labelHiddenValid = SameRect( labelHidden.interactionBounds, comboBounds ) &&
                                  NearlyEqual( labelHidden.fieldBounds.x, 10.0f ) &&
                                  NearlyEqual( labelHidden.fieldBounds.y, 94.0f ) &&
                                  NearlyEqual( labelHidden.fieldBounds.w, 180.0f ) &&
                                  NearlyEqual( labelHidden.popupBounds.x, 10.0f ) &&
                                  NearlyEqual( labelHidden.popupBounds.y, 116.0f ) &&
                                  NearlyEqual( labelHidden.popupBounds.w, 180.0f ) &&
                                  SameRect( comboBox.DropdownBounds( 3 ), labelHidden.popupBounds );

    comboBox.SetLabelVisible( true );
    comboBox.SetDropUp( true );
    const bool dropUpValid = NearlyEqual( dropUp.popupBounds.y, 30.0f ) &&
                             SameRect( comboBox.DropdownBounds( 3 ), dropUp.popupBounds );
    comboBox.SetDropUp( false );
    comboBox.SetOpen( true );
    const int disabledRow = comboBox.HitOption( 80, 120, 3 );
    const bool optionEdgesValid = SameRect( zeroOptions.popupBounds, negativeOptions.popupBounds ) &&
                                  NearlyEqual( zeroOptions.popupBounds.h, 20.0f ) &&
                                  Widgets::ComboOptionAtPointer( zeroOptions.popupBounds, kEnabled, 80, 120, 0 ) == -1 &&
                                  Widgets::ComboOptionAtPointer( negativeOptions.popupBounds, kEnabled, 80, 120, -3 ) ==
                                      -1 &&
                                  disabledRow == 0 && !Widgets::IsComboOptionEnabled( 1u, disabledRow );

    auto drawList = std::make_unique<UIDrawList>();
    UIDrawContext draw( 640, 360, *drawList );
    const Style::UIPalette& palette = Style::Palette();
    const Style::FooterToggleStyle& footer = Style::FooterToggle();
    const UIRect footerBounds = { 12.0f, 12.0f, 156.0f, 26.0f };
    std::array<UIDrawList::Command, 4> enabledFooterCommands = {};
    std::array<UIDrawList::Command, 4> disabledFooterCommands = {};

    // Negative control: disabling a footer toggle changes all four authored
    // colors without changing its command types or geometry.
    drawList->Clear();
    Widgets::DrawToggle( draw, footerBounds, "Enabled", Style::Accent(), kEnabled | UIVisualState::Checked, kFooter );
    const uint64_t enabledFooterFingerprint = drawList->Fingerprint();
    const auto enabledFooterView = drawList->Commands();
    const bool enabledFooterCount = enabledFooterView.size() == enabledFooterCommands.size();

    if ( enabledFooterCount )
    {
        for ( size_t index = 0; index < enabledFooterCommands.size(); ++index )
        {
            enabledFooterCommands[index] = enabledFooterView[index];
        }
    }

    drawList->Clear();
    Widgets::DrawToggle( draw, footerBounds, "Enabled", Style::Accent(), kDisabled | UIVisualState::Checked, kFooter );
    const uint64_t disabledFooterFingerprint = drawList->Fingerprint();
    const auto disabledFooterView = drawList->Commands();
    const bool disabledFooterCount = disabledFooterView.size() == disabledFooterCommands.size();

    if ( disabledFooterCount )
    {
        for ( size_t index = 0; index < disabledFooterCommands.size(); ++index )
        {
            disabledFooterCommands[index] = disabledFooterView[index];
        }
    }

    bool footerGeometryValid = enabledFooterCount && disabledFooterCount;

    for ( size_t index = 0; footerGeometryValid && index < enabledFooterCommands.size(); ++index )
    {
        footerGeometryValid = SameCommandGeometry( enabledFooterCommands[index], disabledFooterCommands[index] );
    }

    const bool footerStateValid = footerGeometryValid && enabledFooterFingerprint != disabledFooterFingerprint &&
                                  CommandHasColor( enabledFooterCommands[0], footer.label, 1.0f ) &&
                                  CommandHasColor( enabledFooterCommands[1], palette.border, palette.border.a ) &&
                                  CommandHasColor( enabledFooterCommands[2], palette.accent, palette.accent.a ) &&
                                  CommandHasColor( enabledFooterCommands[3], palette.accentStrong, 0.96f ) &&
                                  CommandHasColor( disabledFooterCommands[0], palette.textMuted, 1.0f ) &&
                                  CommandHasColor( disabledFooterCommands[1], palette.border, 0.05f ) &&
                                  CommandHasColor( disabledFooterCommands[2], palette.windowSubtle, 0.58f ) &&
                                  CommandHasColor( disabledFooterCommands[3], palette.textMuted, 0.62f );

    std::array<UIDrawList::Command, 6> enabledChevronCommands = {};
    std::array<UIDrawList::Command, 6> disabledChevronCommands = {};

    // Invariant: Established chevrons are exactly six pixel rects. Inspecting
    // each record prevents unrelated disabled commands from masking a chevron
    // that still uses the enabled color.
    drawList->Clear();
    Widgets::DrawComboField( draw, labelled, "Mode", "Beta", true, false, kEnabled, true, kEstablished );
    const uint64_t enabledChevronFingerprint = drawList->Fingerprint();
    const auto enabledComboCommands = drawList->Commands();
    const bool enabledChevronCount = enabledComboCommands.size() == 10;

    if ( enabledChevronCount )
    {
        const size_t chevronStart = enabledComboCommands.size() - enabledChevronCommands.size();

        for ( size_t index = 0; index < enabledChevronCommands.size(); ++index )
        {
            enabledChevronCommands[index] = enabledComboCommands[chevronStart + index];
        }
    }

    drawList->Clear();
    Widgets::DrawComboField( draw, labelled, "Mode", "Beta", true, false, kDisabled, false, kEstablished );
    const uint64_t disabledChevronFingerprint = drawList->Fingerprint();
    const auto disabledComboCommands = drawList->Commands();
    const bool disabledChevronCount = disabledComboCommands.size() == 10;

    if ( disabledChevronCount )
    {
        const size_t chevronStart = disabledComboCommands.size() - disabledChevronCommands.size();

        for ( size_t index = 0; index < disabledChevronCommands.size(); ++index )
        {
            disabledChevronCommands[index] = disabledComboCommands[chevronStart + index];
        }
    }

    bool chevronCommandsValid = enabledChevronCount && disabledChevronCount &&
                                enabledChevronFingerprint != disabledChevronFingerprint;

    for ( size_t index = 0; chevronCommandsValid && index < enabledChevronCommands.size(); ++index )
    {
        chevronCommandsValid = enabledChevronCommands[index].type == UIDrawList::CommandType::Rect &&
                               disabledChevronCommands[index].type == UIDrawList::CommandType::Rect &&
                               SameCommandGeometry( enabledChevronCommands[index], disabledChevronCommands[index] ) &&
                               CommandHasColor( enabledChevronCommands[index], palette.textSecondary, 0.96f ) &&
                               CommandHasColor( disabledChevronCommands[index], palette.textMuted, 0.56f );
    }

    static constexpr const char* kOptions[] = { "Alpha", "Beta", "Gamma" };

    // Hazard: null catalogs and invalid counts are legal detached values. They
    // may preserve popup framing but must never dereference text or select a
    // row, while an out-of-range selection is equivalent to no selection.
    drawList->Clear();
    Widgets::DrawComboPopup( draw, labelled, nullptr, 3, 1, 2, 0u, kEnabled );
    const auto nullOptionsStats = drawList->GetStats();
    const bool nullOptionsValid = nullOptionsStats.commandCount == 5 && nullOptionsStats.textBytes == 0 &&
                                  nullOptionsStats.maxClipDepth == 1 && !nullOptionsStats.clipOverflow;

    drawList->Clear();
    Widgets::DrawComboPopup( draw, labelled, kOptions, 3, -1, -1, 0u, kEnabled );
    const uint64_t noSelectionFingerprint = drawList->Fingerprint();
    drawList->Clear();
    Widgets::DrawComboPopup( draw, labelled, kOptions, 3, 99, -1, 0u, kEnabled );
    const bool outOfRangeSelectionValid = drawList->Fingerprint() == noSelectionFingerprint;

    drawList->Clear();
    Widgets::DrawComboPopup( draw, zeroOptions, kOptions, 0, -1, -1, 0u, kEnabled );
    const bool zeroOptionsValid = drawList->GetStats().commandCount == 0;
    drawList->Clear();
    Widgets::DrawComboPopup( draw, negativeOptions, kOptions, -3, -1, -1, 0u, kEnabled );
    const bool negativeOptionsValid = drawList->GetStats().commandCount == 0;

    const bool passed = labelColumnValid && labelHiddenValid && dropUpValid && optionEdgesValid && footerStateValid &&
                        chevronCommandsValid && nullOptionsValid && outOfRangeSelectionValid && zeroOptionsValid &&
                        negativeOptionsValid;

    if ( passed )
    {
        return true;
    }

    std::fprintf( stderr,
                  "FAIL: component edges label=%d hidden=%d dropUp=%d options=%d footer=%d chevron=%d null=%d "
                  "selection=%d zero=%d negative=%d footerFp=%llu/%llu chevronFp=%llu/%llu\n",
                  labelColumnValid ? 1 : 0, labelHiddenValid ? 1 : 0, dropUpValid ? 1 : 0, optionEdgesValid ? 1 : 0,
                  footerStateValid ? 1 : 0, chevronCommandsValid ? 1 : 0, nullOptionsValid ? 1 : 0,
                  outOfRangeSelectionValid ? 1 : 0, zeroOptionsValid ? 1 : 0, negativeOptionsValid ? 1 : 0,
                  static_cast<unsigned long long>( enabledFooterFingerprint ),
                  static_cast<unsigned long long>( disabledFooterFingerprint ),
                  static_cast<unsigned long long>( enabledChevronFingerprint ),
                  static_cast<unsigned long long>( disabledChevronFingerprint ) );
    return false;
}


bool CheckStatelessComponentContracts()
{
    constexpr UIVisualState kEnabled = UIVisualState::Visible | UIVisualState::Enabled;
    constexpr UIVisualState kDisabled = UIVisualState::Visible;
    constexpr std::array kStates = {
        kEnabled,
        kEnabled | UIVisualState::Hovered,
        kEnabled | UIVisualState::Focused,
        kEnabled | UIVisualState::Active,
        kEnabled | UIVisualState::Selected,
        kEnabled | UIVisualState::Checked,
        kDisabled,
        UIVisualState::None,
    };

    const UIRect buttonBounds = { 10.0f, 10.0f, 120.0f, 28.0f };
    const UIRect sliderBounds = { 10.0f, 190.0f, 360.0f, 34.0f };
    const UIRect tabStrip = { 10.0f, 240.0f, 360.0f, 50.0f };
    const UIRect scrollTrack = { 382.0f, 10.0f, 8.0f, 210.0f };
    const UIRect comboBounds = { 10.0f, 90.0f, 180.0f, 28.0f };
    const UIRect sliderTrack = Widgets::SliderTrackBounds( sliderBounds );
    const UIRect sliderThumb = Widgets::SliderThumbBounds( sliderBounds, 0.5f, 0.0f, 1.0f );
    const UIRect secondTab = Widgets::ResolveTabLayout( tabStrip, 1, 3 ).visualBounds;
    const UIRect scrollThumb = Widgets::ScrollThumbBounds( scrollTrack, 420.0f, 210.0f, 105.0f );
    const Widgets::ComboLayout comboLayout = Widgets::ResolveComboLayout( comboBounds, true, false, 3 );

    // Invariant: disabled rows still own their exact hit rectangle, but only
    // visible and enabled rows may become actions. Drawing receives these
    // already-resolved facts and never samples the pointer itself.
    const bool geometryValid = Widgets::ContainsComponent( buttonBounds, kEnabled, 20, 20 ) &&
                               Widgets::ContainsComponent( buttonBounds, kDisabled, 20, 20 ) &&
                               !Widgets::ContainsComponent( buttonBounds, UIVisualState::Enabled, 20, 20 ) &&
                               Widgets::CanActivateComponent( buttonBounds, kEnabled, 20, 20 ) &&
                               !Widgets::CanActivateComponent( buttonBounds, kDisabled, 20, 20 ) &&
                               !Widgets::CanActivateComponent( buttonBounds, kEnabled, 200, 20 ) &&
                               NearlyEqual( sliderTrack.x, 128.0f ) && NearlyEqual( sliderTrack.y, 207.0f ) &&
                               NearlyEqual( sliderTrack.w, 170.0f ) &&
                               NearlyEqual( Widgets::SliderValueFromPointer( sliderBounds, 0, 0.0f, 1.0f, 0.25f ), 0.0f ) &&
                               NearlyEqual( Widgets::SliderValueFromPointer( sliderBounds, 213, 0.0f, 1.0f, 0.25f ),
                                            0.5f ) &&
                               NearlyEqual( Widgets::SliderValueFromPointer( sliderBounds, 640, 0.0f, 1.0f, 0.25f ),
                                            1.0f ) &&
                               NearlyEqual( sliderThumb.x, 208.0f ) && NearlyEqual( sliderThumb.y, 202.0f ) &&
                               NearlyEqual( secondTab.x, 132.0f ) && NearlyEqual( secondTab.w, 112.0f ) &&
                               Widgets::HitTestTab( tabStrip, kEnabled, 190, 260, 3 ) == 1 &&
                               Widgets::HitTestTab( tabStrip, kDisabled, 190, 260, 3 ) == -1 &&
                               NearlyEqual( scrollThumb.x, 381.0f ) && NearlyEqual( scrollThumb.y, 62.5f ) &&
                               NearlyEqual( scrollThumb.w, 10.0f ) && NearlyEqual( scrollThumb.h, 105.0f ) &&
                               NearlyEqual( comboLayout.fieldBounds.x, 76.0f ) &&
                               NearlyEqual( comboLayout.fieldBounds.y, 94.0f ) &&
                               NearlyEqual( comboLayout.fieldBounds.w, 114.0f ) &&
                               NearlyEqual( comboLayout.popupBounds.y, 116.0f ) &&
                               NearlyEqual( comboLayout.popupBounds.h, 60.0f ) &&
                               Widgets::ComboOptionAtPointer( comboLayout.popupBounds, kEnabled, 80, 157, 3 ) == 2 &&
                               Widgets::ComboOptionAtPointer( comboLayout.popupBounds, kDisabled, 80, 157, 3 ) == 2 &&
                               !Widgets::IsComboOptionEnabled( 1u, 0 ) && Widgets::IsComboOptionEnabled( 1u, 1 );

    auto drawList = std::make_unique<UIDrawList>();
    UIDrawContext draw( 640, 360, *drawList );
    std::array<uint64_t, kStates.size()> stateFingerprints = {};
    bool stateBoundsValid = true;
    bool stateOverflow = false;

    for ( size_t index = 0; index < kStates.size(); ++index )
    {
        drawList->Clear();
        Widgets::DrawButton( draw, buttonBounds, "Apply", kStates[index] );
        stateFingerprints[index] = drawList->Fingerprint();
        const auto stats = drawList->GetStats();
        stateOverflow = stateOverflow || stats.commandOverflow || stats.textOverflow || stats.clipOverflow;

        if ( index < kStates.size() - 1 )
        {
            const auto commands = drawList->Commands();
            stateBoundsValid = stateBoundsValid && !commands.empty() &&
                               commands.front().type == UIDrawList::CommandType::RoundedRect &&
                               NearlyEqual( commands.front().x0, buttonBounds.x ) &&
                               NearlyEqual( commands.front().y0, buttonBounds.y ) &&
                               NearlyEqual( commands.front().w, buttonBounds.w ) &&
                               NearlyEqual( commands.front().h, buttonBounds.h );
        }
        else
        {
            stateBoundsValid = stateBoundsValid && stats.commandCount == 0;
        }
    }

    bool fingerprintsValid = stateFingerprints == kExpectedStatelessStateFingerprints;

    static constexpr const char* kOptions[] = { "Alpha", "Beta", "Gamma" };
    drawList->Clear();
    Widgets::DrawPanel( draw, { 8.0f, 68.0f, 218.0f, 44.0f }, kEnabled,
                        Widgets::ComponentAppearance::Compact, 0.88f );
    Widgets::DrawButton( draw, { 12.0f, 46.0f, 100.0f, 22.0f }, "Compact", kEnabled,
                         Widgets::ComponentAppearance::Compact );
    Widgets::DrawButton( draw, { 12.0f, 76.0f, 100.0f, 28.0f }, "Apply", kEnabled | UIVisualState::Active );
    Widgets::DrawButton( draw, { 118.0f, 76.0f, 100.0f, 28.0f }, "Blocked", kDisabled );
    Widgets::DrawToggle( draw, { 12.0f, 110.0f, 206.0f, 24.0f }, "Enabled", Style::Palette().accent,
                         kEnabled | UIVisualState::Hovered | UIVisualState::Checked );
    Widgets::DrawSlider( draw, sliderBounds, "Strength", "0.50", 0.5f, 0.0f, 1.0f,
                         kEnabled | UIVisualState::Focused | UIVisualState::Active );

    for ( int tabIndex = 0; tabIndex < 3; ++tabIndex )
    {
        UIVisualState tabState = kEnabled;

        if ( tabIndex == 1 )
        {
            tabState |= UIVisualState::Selected;
        }
        else if ( tabIndex == 2 )
        {
            tabState |= UIVisualState::Hovered;
        }

        const Widgets::TabLayout tabLayout = Widgets::ResolveTabLayout( tabStrip, tabIndex, 3 );
        Widgets::DrawTab( draw, tabLayout.visualBounds, kOptions[tabIndex], tabState );
    }

    Widgets::DrawScrollBar( draw, scrollTrack, 420.0f, 210.0f, 105.0f, 0.74f, kEnabled | UIVisualState::Active );
    Widgets::DrawComboField( draw, comboLayout, "Mode", "Beta", true, true, kEnabled | UIVisualState::Focused );
    Widgets::DrawComboPopup( draw, comboLayout, kOptions, static_cast<int>( std::size( kOptions ) ), 1, 2, 1u, kEnabled );
    Widgets::DrawIconButton( draw, { 234.0f, 46.0f, 24.0f, 24.0f }, Widgets::ComponentIcon::Plus,
                             kEnabled | UIVisualState::Hovered );
    Widgets::DrawIconButton( draw, { 264.0f, 46.0f, 24.0f, 24.0f }, Widgets::ComponentIcon::Close,
                             kEnabled | UIVisualState::Focused );
    const uint64_t fixtureFingerprint = drawList->Fingerprint();
    const auto fixtureStats = drawList->GetStats();
    const bool fixtureValid = fixtureFingerprint == kExpectedStatelessFixtureFingerprint && !fixtureStats.commandOverflow &&
                              !fixtureStats.textOverflow && !fixtureStats.clipOverflow && fixtureStats.maxClipDepth == 1;

    drawList->Clear();

    for ( int command = 0; command < UIDrawList::MAX_COMMANDS; ++command )
    {
        draw.Rect( 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f );
    }

    Widgets::DrawButton( draw, buttonBounds, "Overflow", kEnabled );
    const bool overflowReportingValid = drawList->GetStats().commandOverflow;

    if ( geometryValid && stateBoundsValid && fingerprintsValid && !stateOverflow && fixtureValid && overflowReportingValid )
    {
        return true;
    }

    std::fprintf( stderr,
                  "FAIL: stateless UI geometry=%d bounds=%d fingerprints=%d overflow=%d fixture=%llu/%llu "
                  "fixtureStats=%d/%d/%d/%d overflowProbe=%d\n",
                  geometryValid ? 1 : 0, stateBoundsValid ? 1 : 0, fingerprintsValid ? 1 : 0, stateOverflow ? 1 : 0,
                  static_cast<unsigned long long>( fixtureFingerprint ),
                  static_cast<unsigned long long>( kExpectedStatelessFixtureFingerprint ), fixtureStats.commandCount,
                  fixtureStats.textBytes, fixtureStats.maxClipDepth, fixtureStats.clipOverflow ? 1 : 0,
                  overflowReportingValid ? 1 : 0 );

    for ( size_t index = 0; index < stateFingerprints.size(); ++index )
    {
        std::fprintf( stderr, "  state[%zu]=%llu/%llu\n", index, static_cast<unsigned long long>( stateFingerprints[index] ),
                      static_cast<unsigned long long>( kExpectedStatelessStateFingerprints[index] ) );
    }

    return false;
}
} // namespace

int main()
{
    bool failed = !CheckComponentBaselines();
    failed = !CheckComponentEdgeContracts() || failed;
    failed = !CheckStatelessComponentContracts() || failed;

    if ( failed )
    {
        return 1;
    }

    std::printf( "PASS: UI foundation rendered deterministic components without product, Runtime, or Rendering.\n" );
    return 0;
}
