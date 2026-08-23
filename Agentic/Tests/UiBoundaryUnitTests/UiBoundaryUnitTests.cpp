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
//   - Resting and engaged component fixtures must retain their exact command
//     fingerprints without overflowing a bounded draw buffer.
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
#include "UI/UIIconButton.h"
#include "UI/UIScrollBar.h"
#include "UI/UISlider.h"
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
using SkullbonezCore::UI::UIScrollBar;
using SkullbonezCore::UI::UISlider;
using SkullbonezCore::UI::UITabBar;

constexpr uint64_t kExpectedRestingComponentFingerprint = 9585956286470253977ull;
constexpr uint64_t kExpectedEngagedComponentFingerprint = 15522795272601894673ull;

constexpr std::array kTabs = {
    InGameUITab::Profiler,
    InGameUITab::Scene,
    InGameUITab::Editor,
    InGameUITab::Physics,
    InGameUITab::Options,
    InGameUITab::Render,
    InGameUITab::Targets,
    InGameUITab::Keys,
    InGameUITab::Sky,
    InGameUITab::Cinematic,
    InGameUITab::Memory,
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

    comboBox.SetOpen( false );
    const bool geometryValid = button.HitTest( 20, 20 ) && !button.HitTest( 200, 20 ) &&
                               checkBox.HitTest( 20, 60 ) && !checkBox.HitTest( 200, 60 ) &&
                               comboBox.HitBox( 20, 100 ) && comboBox.HitOption( 80, 157, 3 ) == -1 &&
                               iconButton.HitTest( 220, 20 ) && !iconButton.HitTest( 250, 20 ) &&
                               slider.HitTest( 20, 200 ) && !slider.HitTest( 400, 200 ) &&
                               std::fabs( slider.ValueFromMouse( 0, 0.0f, 1.0f, 0.25f ) - 0.0f ) < 0.0001f &&
                               std::fabs( slider.ValueFromMouse( 213, 0.0f, 1.0f, 0.25f ) - 0.5f ) < 0.0001f &&
                               std::fabs( slider.ValueFromMouse( 640, 0.0f, 1.0f, 0.25f ) - 1.0f ) < 0.0001f &&
                               tabBar.HitTest( 190, 260, 3 ) == 1 && tabBar.HitTest( 400, 260, 3 ) == -1;

    static constexpr const char* kComboOptions[] = { "Alpha", "Beta", "Gamma" };
    static constexpr const char* kTabLabels[] = { "One", "Two", "Three" };
    auto drawList = std::make_unique<UIDrawList>();
    UIDrawContext draw( 640, 360, *drawList );

    auto recordFixture = [&]( bool engaged ) {
        drawList->Clear();
        comboBox.SetOpen( engaged );
        button.Draw( draw, "Apply", engaged ? 20 : 600, engaged ? 20 : 340 );
        checkBox.DrawToggle( draw, "Enabled", engaged, 0.20f, 0.60f, 0.90f );
        comboBox.Draw( draw, "Mode", kComboOptions, static_cast<int>( std::size( kComboOptions ) ), 1,
                       engaged ? 80 : 600, engaged ? 157 : 340, 1u );
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
    const bool overflow = restingStats.commandOverflow || restingStats.textOverflow || restingStats.clipOverflow ||
                          engagedStats.commandOverflow || engagedStats.textOverflow || engagedStats.clipOverflow;
    const bool fingerprintValid = restingFingerprint == kExpectedRestingComponentFingerprint &&
                                  engagedFingerprint == kExpectedEngagedComponentFingerprint;

    if ( geometryValid && fingerprintValid && !overflow )
    {
        return true;
    }

    std::fprintf(
        stderr,
        "FAIL: UI component baseline geometry=%d resting=%llu/%llu engaged=%llu/%llu overflow=%d\n",
        geometryValid ? 1 : 0,
        static_cast<unsigned long long>( restingFingerprint ),
        static_cast<unsigned long long>( kExpectedRestingComponentFingerprint ),
        static_cast<unsigned long long>( engagedFingerprint ),
        static_cast<unsigned long long>( kExpectedEngagedComponentFingerprint ),
        overflow ? 1 : 0
    );
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
    for ( size_t surface = 0; surface < kTabs.size(); ++surface )
    {
        ui->SetActiveTab( kTabs[surface] );
        ui->ResetPresentationState();
        const SkullbonezCore::UI::UIDrawList& frame = ui->Draw( *data );
        const auto stats = frame.GetStats();
        if ( frame.Fingerprint() != kExpectedFingerprints[surface] || stats.commandOverflow || stats.textOverflow ||
             stats.clipOverflow )
        {
            std::fprintf(
                stderr,
                "FAIL: UI surface %zu fingerprint=%llu expected=%llu overflow=%d/%d/%d\n",
                surface,
                static_cast<unsigned long long>( frame.Fingerprint() ),
                static_cast<unsigned long long>( kExpectedFingerprints[surface] ),
                stats.commandOverflow ? 1 : 0,
                stats.textOverflow ? 1 : 0,
                stats.clipOverflow ? 1 : 0
            );
            failed = true;
        }
    }

    if ( failed )
    {
        return 1;
    }

    std::printf( "PASS: production UI library rendered %zu detached surfaces without Runtime or Rendering.\n", kTabs.size() );
    return 0;
}
