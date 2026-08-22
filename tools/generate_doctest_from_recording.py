#!/usr/bin/env python3
"""
File: tools/generate_doctest_from_recording.py
Purpose:
  Translates recorded resolution-independent interaction JSON scenarios into C++ doctest test cases.

Usage:
  python tools/generate_doctest_from_recording.py --input TestScenarios/my_recording.json --test-name "My Test Name"
"""

import argparse
import json
import os
import sys

def generate_doctest(json_path: str, test_name: str) -> str:
    with open(json_path, "r", encoding="utf-8") as f:
        data = json.load(f)

    actions = data.get("actions", [])
    if not actions:
        print(f"Error: No actions found in {json_path}", file=sys.stderr)
        sys.exit(1)

    scene_path = data.get("scene", "")
    cpp_lines = [
        f'TEST_CASE( "Recorded Interaction: {test_name}" )',
        '{',
        '    using namespace SkullbonezCore::Runtime;',
        '',
    ]

    if scene_path:
        cpp_lines.extend([
            f'    // Recorded from initial scene snapshot: {scene_path}',
            f'    constexpr const char* scenePath = "{scene_path}";',
            '',
        ])

    cpp_lines.extend([
        '    // Setup simulated display bounds (test runs resolution-independent)',
        '    constexpr int screenWidth = 1920;',
        '    constexpr int screenHeight = 1080;',
        '',
        '    RunReplayCauseTreeState treeState;',
        '    treeState.hasWindowPlacement = true;',
        '    treeState.x = 1180;',
        '    treeState.y = 140;',
        '    treeState.width = 430;',
        '    treeState.height = 500;',
        '',
        '    ReplayCauseInspection inspection;',
        '    // Initialize inspection mode and layout',
        '',
    ])

    for i, action in enumerate(actions):
        frame = action.get("frame", i)
        cpp_lines.append(f'    // Turn {i+1} (Frame {frame})')

        if "clickReplayControl" in action:
            ctrl = action["clickReplayControl"]
            cpp_lines.append(f'    // Action: Click replay control "{ctrl}"')
            if ctrl.startswith("causeTab"):
                tab_name = ctrl.replace("causeTab", "")
                cpp_lines.append(f'    // Simulate clicking tab: {tab_name}')
                cpp_lines.append(f'    // inspection.TickSolverDetailPanelInput( ... );')
            elif ctrl.startswith("causeFilter"):
                chip_name = ctrl.replace("causeFilter", "")
                cpp_lines.append(f'    // Simulate filter chip: {chip_name}')
            elif ctrl == "causeCloseDrawer":
                cpp_lines.append(f'    // Simulate closing detail drawer')

        elif "clickPoint" in action:
            pt = action["clickPoint"]
            norm = action.get("normalizedPoint", [0, 0])
            button = action.get("button", "left")
            cpp_lines.append(f'    // Action: Click point ({pt[0]}, {pt[1]}) [norm: {norm[0]:.3f}, {norm[1]:.3f}] button: {button}')

        elif "moveMouse" in action:
            pt = action["moveMouse"]
            norm = action.get("normalizedPoint", [0, 0])
            cpp_lines.append(f'    // Action: Move mouse ({pt[0]}, {pt[1]}) [norm: {norm[0]:.3f}, {norm[1]:.3f}]')

        elif "pressKey" in action:
            key = action["pressKey"]
            cpp_lines.append(f'    // Action: Press key "{key}"')

        elif "scrollPoint" in action:
            scroll = action["scrollPoint"]
            cpp_lines.append(f'    // Action: Scroll at ({scroll[0]}, {scroll[1]}) delta: {scroll[2]}')

        cpp_lines.append('')

    cpp_lines.extend([
        '    // -------------------------------------------------------------',
        '    // Outcome Assertions (Customize based on your test expectations)',
        '    // -------------------------------------------------------------',
        '    // CHECK( inspection.View().mode != ReplayCauseInspectionMode::Returning );',
        '    // CHECK( inspection.View().activeTab == ReplayCauseInspectorTab::Iterations );',
        '    // CHECK( treeState.filter == RunReplayCauseTreeFilter::Contacts );',
        '}',
        ''
    ])

    return '\n'.join(cpp_lines)

def main():
    parser = argparse.ArgumentParser(description="Generate C++ doctest from recorded interaction scenario.")
    parser.add_argument("--input", required=True, help="Path to recorded scenario JSON file")
    parser.add_argument("--test-name", default="User Recorded Scenario", help="Name of the generated TEST_CASE")
    parser.add_argument("--output", help="Optional output C++ file path")

    args = parser.parse_args()

    test_cpp = generate_doctest(args.input, args.test_name)

    if args.output:
        with open(args.output, "w", encoding="utf-8") as f:
            f.write(test_cpp)
        print(f"Generated test case written to {args.output}")
    else:
        print(test_cpp)

if __name__ == "__main__":
    main()
