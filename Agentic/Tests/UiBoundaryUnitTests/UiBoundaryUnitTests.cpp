// File: Agentic/Tests/UiBoundaryUnitTests/UiBoundaryUnitTests.cpp
// Purpose:
//   Proves the production UI library can build a complete frame without linking
//   Runtime, Rendering, or a graphics backend.
//
// Mental model:
//   This executable is a link-boundary probe first and a fingerprint test
//   second. It consumes the same detached frame values as Runtime and calls the
//   real InGameUI::Draw implementation, but its project references only the UI
//   static library.
//
// Glossary:
//   Detached frame: Immutable presentation values assembled by an upper owner.
//   Fingerprint: Stable hash of ordered backend-neutral draw commands and text.
//   Boundary probe: A small executable whose successful link proves forbidden
//   implementation dependencies are absent.
//
// Invariants:
//   - The project must not compile or link Runtime, Rendering, or DX12 sources.
//   - Every public tab must produce its committed production draw fingerprint.
//   - No bounded draw buffer may overflow while producing the fixture.
//
// Related:
//   - SkullbonezSource/UI/UI.h
//   - tools/validate_ui_boundary_tests.bat

#include "UI/UI.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>

namespace
{
using SkullbonezCore::UI::InGameUI;
using SkullbonezCore::UI::InGameUIFrameData;
using SkullbonezCore::UI::InGameUITab;

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
    1227990051524176107ull,
    5621374501062094743ull,
    643319089294822447ull,
    9774020997193876338ull,
    3787874871094680490ull,
    13838569643518502325ull,
    1186693958027131891ull,
    5057719176066529734ull,
    3243788985155815295ull,
    15645422141942934428ull,
    5868520363750485546ull,
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
} // namespace

int main()
{
    const auto data = MakeFrameData();
    auto ui = std::make_unique<InGameUI>();
    ui->SetVisible( true );
    ui->SetWindowBounds( 54, 72, 760, 520 );
    ui->SetMouseOverride( true, 12, 12 );

    bool failed = false;
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
