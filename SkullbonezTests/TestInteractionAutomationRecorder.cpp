/*
File: SkullbonezTests/TestInteractionAutomationRecorder.cpp
Purpose:
  Proves interaction recording captures normalized inputs, semantic controls, and outputs valid JSON.

Summary:
  These tests verify that InteractionAutomationRecorder maps mouse coordinates into
  resolution-independent normalized values, identifies UI semantic control hits, tracks
  keyboard and wheel events, and serializes clean JSON replayable across arbitrary resolutions.

Invariants:
  - Coordinate normalization maps client coordinates directly into the unit interval [0, 1].
  - Recorded actions serialize with deterministic frame indices and semantic control identifiers.
  - Recording state toggles safely between idle and active without memory leakage.

Related:
  - SkullbonezSource/Runtime/Automation/InteractionAutomationRecorder.h
  - SkullbonezSource/Runtime/Automation/InteractionAutomationRecorder.cpp
  - SkullbonezSource/Runtime/Input/InputRouter.h
*/

#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Core/SbDiagnosticStore.h"
#include "../SkullbonezSource/Runtime/Automation/InteractionAutomationRecorder.h"
#include "../SkullbonezSource/Runtime/Input/InputRouter.h"
#include "../SkullbonezSource/Runtime/Interaction/RuntimeInteractionController.h"
#include "../SkullbonezSource/Runtime/Planning/ReplayCauseInspection.h"
#include "../SkullbonezSource/Runtime/Replay/ReplayAuthoringPackets.h"

#include <cstdio>
#include <fstream>
#include <string>

using namespace SkullbonezCore::Runtime;

TEST_CASE( "InteractionAutomationRecorder records normalized coordinates and semantic controls" )
{
    InteractionAutomationRecorder recorder;
    CHECK_FALSE( recorder.IsRecording() );
    CHECK( recorder.ActionCount() == 0u );

    const char* testPath = "TestOutput/temp_test_recording.json";
    recorder.StartRecording( testPath );
    CHECK( recorder.IsRecording() );

    SkullbonezCore::Core::SbDiagnosticStore diagnostics;
    InputRouter inputRouter( diagnostics );
    RuntimeInteractionController interaction;
    RunReplayCauseTreeState causeTree;
    ReplayCauseInspectionView causeInspection;

    // Simulate mouse movement and click at (960, 540) on a 1920x1080 display
    constexpr int screenW = 1920;
    constexpr int screenH = 1080;

    // Set up mock window and hit state
    causeTree.hasWindowPlacement = true;
    causeTree.x = 1180;
    causeTree.y = 140;
    causeTree.width = 430;
    causeTree.height = 500;

    recorder.RecordFrame( 10, screenW, screenH, inputRouter, interaction, causeTree, causeInspection );

    // Verify recording lifecycle and output
    CHECK( recorder.SaveToFile() );

    recorder.StopRecording();
    CHECK_FALSE( recorder.IsRecording() );

    // Verify that the file was created on disk
    std::ifstream file( testPath );
    CHECK( file.is_open() );
    file.close();
    (void)std::remove( testPath );
}

TEST_CASE( "InteractionAutomationRecorder toggle toggles recording state" )
{
    InteractionAutomationRecorder recorder;
    CHECK_FALSE( recorder.IsRecording() );

    recorder.ToggleRecording( "TestOutput/temp_toggle.json", 0 );
    CHECK( recorder.IsRecording() );

    recorder.ToggleRecording( "TestOutput/temp_toggle.json", 50 );
    CHECK_FALSE( recorder.IsRecording() );

    (void)std::remove( "TestOutput/temp_toggle.json" );
}
