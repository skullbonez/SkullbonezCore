// File: Agentic/Tests/UiBoundaryUnitTests/UiBoundaryUnitTests.cpp
// Purpose:
//   Proves the production UI library can build a complete frame without linking
//   Runtime, Rendering, or a graphics backend.
//
// Summary:
//   This executable proves both halves of the UI library boundary. It records
//   deterministic component states directly, then consumes the same detached
//   frame values as Runtime and calls the real InGameUI::Draw implementation;
//   its project references only the UI static library.
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
//   - Every public tab must produce its committed production draw fingerprint.
//   - No bounded draw buffer may overflow while producing the fixture.
//
// Related:
//   - Agentic/Reference/engine-glossary.md
//   - SkullbonezSource/UI/UI.h
//   - tools/validate_ui_boundary_tests.bat

#include "UI/UI.h"
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
using SkullbonezCore::UI::InGameUI;
using SkullbonezCore::UI::InGameUIFrameData;
using SkullbonezCore::UI::InGameUITab;
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
constexpr uint64_t kExpectedStatelessFixtureFingerprint = 9020569520314488178ull;

constexpr std::array kTabs = {
    InGameUITab::Profiler, InGameUITab::Scene,     InGameUITab::Editor,  InGameUITab::Physics,
    InGameUITab::Options,  InGameUITab::Render,    InGameUITab::Targets, InGameUITab::Keys,
    InGameUITab::Sky,      InGameUITab::Cinematic, InGameUITab::Memory,
};

constexpr std::array<uint64_t, kTabs.size()> kExpectedFingerprints = {

    // Profiler: refreshed when the per-marker Work column was added so
    // worker-thread time stopped being summed into the frame-thread rows. Only
    // this surface moved; the other ten prove the column disturbed no other tab.
    17282268762934632125ull,

    // Scene: the rolling-prediction checkbox, forecast stability rows,
    // interaction-replay selector, and Capture lockstep request label are part
    // of the committed operator stream.
    2399826200700883422ull,
    643319089294822447ull,
    9774020997193876338ull,

    // Options: the scene/session request is labelled Capture lockstep.
    16562541090565446015ull,
    13838569643518502325ull,
    1186693958027131891ull,
    5057719176066529734ull,
    3243788985155815295ull,
    15645422141942934428ull,

    // Memory: prediction evidence bank and release-checkpoint rows are part of
    // the detached UI contract; this must match the production unit fixture.
    14809053394253860312ull,
};

std::unique_ptr<InGameUIFrameData> MakeFrameData()
{
    static const char* sceneOptions[] = { "ui_scene", "ui_scene_alt" };
    auto data = std::make_unique<InGameUIFrameData>();
    data->screenW = 1920;
    data->screenH = 1080;
    data->rendererName = "DirectX 12";
    data->sceneName = "UI production fingerprint";
    data->sceneOptions = sceneOptions;
    data->sceneOptionCount = static_cast<int>( std::size( sceneOptions ) );
    data->selectedSceneOption = 0;
    data->currentSceneIndex = 0;
    data->sceneCount = data->sceneOptionCount;
    data->sceneMode = true;
    data->currentFrame = 20;
    data->targetFrameCount = 120;
    data->runtimeInputModeLabel = "Inspect";
    data->fps = 60.0f;
    data->renderMs = 4.0f;
    data->physicsMs = 2.0f;
    data->cpuFrameMs = 6.0f;
    data->gpuFrameMs = 3.0f;
    data->modelCount = 32;
    data->workerThreadCount = 4;
    data->maxWorkerThreadCount = 8;
    data->renderTargetPreviewCount = 1;
    data->renderTargetPreviews[0] = { "Scene HDR", 1920, 1080, false, false, true };
    return data;
}

bool NearlyEqual( float left, float right )
{
    return std::fabs( left - right ) < 0.0001f;
}


bool SameRect( const UIRect& left, const UIRect& right )
{
    return NearlyEqual( left.x, right.x ) && NearlyEqual( left.y, right.y ) && NearlyEqual( left.w, right.w ) &&
           NearlyEqual( left.h, right.h );
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
    const UIRect comboPopup = Widgets::ComboPopupBounds( comboBounds, true, false, 3 );
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

    // False-pass control: wrappers and stateless operations must emit the same
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
                              SameRect( comboBox.DropdownBounds( 3 ), comboPopup );
    comboBox.SetOpen( true );
    commandValuesValid = commandValuesValid && comboBox.HitOption( 80, 157, 3 ) ==
                                                   Widgets::ComboOptionAtPointer( comboPopup, kEnabled, 80, 157, 3 );
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
        Widgets::DrawComboField( draw, comboBounds, "Mode", "Beta", true, engaged, comboState, true, kEstablished );

        if ( engaged )
        {
            const int hoveredOption = Widgets::ComboOptionAtPointer( comboPopup, comboState, 80, 157, 3 );
            Widgets::DrawComboPopup( draw, comboPopup, kComboOptions, static_cast<int>( std::size( kComboOptions ) ), 1,
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
    const UIRect secondTab = Widgets::TabBounds( tabStrip, 1, 3 );
    const UIRect scrollThumb = Widgets::ScrollThumbBounds( scrollTrack, 420.0f, 210.0f, 105.0f );
    const UIRect comboField = Widgets::ComboFieldBounds( comboBounds, true );
    const UIRect comboPopup = Widgets::ComboPopupBounds( comboBounds, true, false, 3 );

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
                               NearlyEqual( comboField.x, 76.0f ) && NearlyEqual( comboField.y, 94.0f ) &&
                               NearlyEqual( comboField.w, 114.0f ) && NearlyEqual( comboPopup.y, 116.0f ) &&
                               NearlyEqual( comboPopup.h, 60.0f ) &&
                               Widgets::ComboOptionAtPointer( comboPopup, kEnabled, 80, 157, 3 ) == 2 &&
                               Widgets::ComboOptionAtPointer( comboPopup, kDisabled, 80, 157, 3 ) == 2 &&
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

    // Clipping is an authored value, not backend state. The row owns one
    // balanced clip pair and copies both text strings into the bounded list.
    drawList->Clear();
    Widgets::DrawLabelValueRow( draw, { 12.0f, 46.0f, 210.0f, 24.0f }, "Mode", "Inspect", Style::Palette().accentStrong,
                                kEnabled | UIVisualState::Focused );
    const auto rowCommands = drawList->Commands();
    const auto rowStats = drawList->GetStats();
    const bool clippingValid = rowCommands.size() == 5 && rowCommands.front().type == UIDrawList::CommandType::PushClip &&
                               rowCommands.back().type == UIDrawList::CommandType::PopClip && rowStats.maxClipDepth == 1 &&
                               !rowStats.clipOverflow && rowStats.textBytes > 0;

    static constexpr const char* kOptions[] = { "Alpha", "Beta", "Gamma" };
    drawList->Clear();
    Widgets::DrawPanel( draw, { 4.0f, 4.0f, 500.0f, 330.0f }, kEnabled | UIVisualState::Selected );
    Widgets::DrawLabelValueRow( draw, { 12.0f, 46.0f, 210.0f, 24.0f }, "Mode", "Inspect", Style::Palette().accentStrong,
                                kEnabled | UIVisualState::Hovered );
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

        Widgets::DrawTab( draw, Widgets::TabBounds( tabStrip, tabIndex, 3 ), kOptions[tabIndex], tabState );
    }

    Widgets::DrawScrollBar( draw, scrollTrack, 420.0f, 210.0f, 105.0f, 0.74f, kEnabled | UIVisualState::Active );
    Widgets::DrawComboField( draw, comboBounds, "Mode", "Beta", true, true, kEnabled | UIVisualState::Focused );
    Widgets::DrawComboPopup( draw, comboPopup, kOptions, static_cast<int>( std::size( kOptions ) ), 1, 2, 1u, kEnabled );
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

    if ( geometryValid && stateBoundsValid && fingerprintsValid && !stateOverflow && clippingValid && fixtureValid &&
         overflowReportingValid )
    {
        return true;
    }

    std::fprintf( stderr,
                  "FAIL: stateless UI geometry=%d bounds=%d fingerprints=%d overflow=%d clipping=%d fixture=%llu/%llu "
                  "fixtureStats=%d/%d/%d/%d overflowProbe=%d\n",
                  geometryValid ? 1 : 0, stateBoundsValid ? 1 : 0, fingerprintsValid ? 1 : 0, stateOverflow ? 1 : 0,
                  clippingValid ? 1 : 0, static_cast<unsigned long long>( fixtureFingerprint ),
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
    const auto data = MakeFrameData();
    auto ui = std::make_unique<InGameUI>();
    ui->SetVisible( true );
    ui->SetWindowBounds( 54, 72, 760, 520 );
    ui->SetMouseOverride( true, 12, 12 );

    bool failed = !CheckComponentBaselines();
    failed = !CheckStatelessComponentContracts() || failed;

    for ( size_t surface = 0; surface < kTabs.size(); ++surface )
    {
        ui->SetActiveTab( kTabs[surface] );
        ui->ResetPresentationState();
        const SkullbonezCore::UI::UIDrawList& frame = ui->Draw( *data );
        const auto stats = frame.GetStats();

        if ( frame.Fingerprint() != kExpectedFingerprints[surface] || stats.commandOverflow || stats.textOverflow ||
             stats.clipOverflow )
        {
            std::fprintf( stderr, "FAIL: UI surface %zu fingerprint=%llu expected=%llu overflow=%d/%d/%d\n", surface,
                          static_cast<unsigned long long>( frame.Fingerprint() ),
                          static_cast<unsigned long long>( kExpectedFingerprints[surface] ), stats.commandOverflow ? 1 : 0,
                          stats.textOverflow ? 1 : 0, stats.clipOverflow ? 1 : 0 );
            failed = true;
        }
    }

    if ( failed )
    {
        return 1;
    }

    std::printf( "PASS: production UI library rendered %zu detached surfaces without Runtime or Rendering.\n",
                 kTabs.size() );
    return 0;
}
