"""Build historical point-joint probes against one unchanged portable support library."""

import argparse
import os
from pathlib import Path
import re
import subprocess


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=Path, required=True)
    parser.add_argument("--cmake", required=True)
    parser.add_argument("--support-library", type=Path, required=True)
    parser.add_argument("--run-only", action="store_true")
    args = parser.parse_args()
    root = args.repo.resolve()
    output = root / "TestOutput/ragdoll-physics-unification/FP5/measurements"
    env = {key.upper(): value for key, value in os.environ.items()}
    revisions = (("pre_stage_a", "87d501af5^"), ("stage_a", "87d501af5"), ("candidate", None))
    for name, revision in revisions:
        target = output / name
        target.mkdir(parents=True, exist_ok=True)
        if args.run_only:
            continue
        for filename in ("Ragdoll.cpp", "Ragdoll.h"):
            original = Path("SkullbonezSource/Physics") / filename
            source = (root / original).read_text() if revision is None else subprocess.check_output(
                ["git", "show", f"{revision}:{original.as_posix()}"], cwd=root, text=True
            )

            def replace_include(match):
                path = (root / original.parent / match.group(1)).resolve()
                if path.name == "Ragdoll.h":
                    path = target / "Ragdoll.h"
                return f'#include "{path.as_posix()}"'

            source = re.sub(r'#include "([^"]+)"', replace_include, source)
            (target / filename).write_text(source)
        (target / "main.cpp").write_text(Path(__file__).with_name("loaded_chain_probe.cpp").read_text())
        (target / "CMakeLists.txt").write_text(f'''cmake_minimum_required(VERSION 3.20)
project(FP5Probe LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
add_executable(fp5_legacy main.cpp Ragdoll.cpp)
target_include_directories(fp5_legacy PRIVATE "{root.as_posix()}")
target_compile_definitions(fp5_legacy PRIVATE JSON_NOEXCEPTION NDEBUG SKULLBONEZ_PORTABLE_CPU SKULLBONEZ_RENDER_FREE_TESTS _HAS_EXCEPTIONS=0 _HAS_STD_BYTE=0 DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS DOCTEST_CONFIG_NO_RTTI DOCTEST_CONFIG_USE_STD_HEADERS)
target_compile_options(fp5_legacy PRIVATE /fp:precise /FI{root.as_posix()}/SkullbonezSource/Core/FloatingPointContract.h)
target_link_libraries(fp5_legacy PRIVATE "{args.support_library.resolve().as_posix()}")
''')
        subprocess.run([args.cmake, "-S", str(target), "-B", str(target / "build"),
                        "-G", "Visual Studio 18 2026", "-A", "x64"], env=env, check=True)
        subprocess.run([args.cmake, "--build", str(target / "build"), "--config", "Release"], env=env, check=True)
    # Alternate complete runs to expose host drift; all three use the same fixture,
    # compiler options, support library, and four solver iterations per joint.
    for round_index in range(2):
        for name, _ in revisions:
            with (output / f"{name}-{round_index}.log").open("w") as log:
                subprocess.run([str(output / name / "build/Release/fp5_legacy.exe")],
                               cwd=root, env=env, stdout=log, stderr=subprocess.STDOUT, check=True)


if __name__ == "__main__":
    main()
