"""Exercise the shipping shader target with native MSBuild tracking and a stub baker."""
from __future__ import annotations

import argparse
import copy
from pathlib import Path
import subprocess
import time
import xml.etree.ElementTree as ET

REPO = Path(__file__).resolve().parents[1]
NS = "http://schemas.microsoft.com/developer/msbuild/2003"
ET.register_namespace("", NS)


def run(msbuild: Path, output: Path) -> None:
    output.mkdir(parents=True, exist_ok=False)
    fixture = output / "workspace with spaces"
    shaders = fixture / "SkullbonezData/shaders"
    generated = fixture / "SkullbonezData/generated"
    tools = fixture / "tools"
    sdk = fixture / "sdk"
    for folder in (shaders, generated, tools, sdk / "bin/10.0.26100.0/x64"):
        folder.mkdir(parents=True)
    for name in ("sample.hlsl", "shared.hlsli", "generate_mips.hlsl", "reflect.rt.hlsl"):
        (shaders / name).write_text("input\n")
    (tools / "bake_shaders.py").write_text("# stub input\n")
    (sdk / "bin/10.0.26100.0/x64/dxc.exe").write_bytes(b"tracked compiler input")
    outputs = [shaders / name for name in (
        "sample.vs.dxil", "sample.ps.dxil", "generate_mips.cs.dxil",
        "reflect.rt.dxil", "shader_manifest.json")]
    outputs.append(generated / "GeneratedShaderReflection.h")
    baker = ["@echo off", 'if exist "%~dp0..\\fail-bake" exit /b 7',
             'echo baked>>"%~dp0..\\bake-count.txt"']
    baker.extend(f'type nul > "{path}"' for path in outputs)
    baker.append("exit /b 0")
    (tools / "bake_shaders.bat").write_text("\n".join(baker) + "\n")

    vc = next(parent / "Microsoft/VC" for parent in msbuild.parents
              if (parent / "Microsoft/VC").is_dir())
    tasks = sorted(vc.glob("v*/Microsoft.Build.CppTasks.Common.dll"))[-1]
    project = ET.Element(f"{{{NS}}}Project", DefaultTargets="BakeShippingShaders")
    ET.SubElement(project, f"{{{NS}}}UsingTask", TaskName="GetOutOfDateItems", AssemblyFile=str(tasks))
    properties = ET.SubElement(project, f"{{{NS}}}PropertyGroup")
    for name, value in {"ProjectDir": str(fixture) + "\\", "WindowsSdkDir": str(sdk) + "\\",
                        "TLogLocation": str(fixture / "tracking") + "\\"}.items():
        ET.SubElement(properties, f"{{{NS}}}{name}").text = value
    # Exercise the production target and its actual dependency declarations.
    source = ET.parse(REPO / "SKULLBONEZ_RENDERING.vcxproj").getroot()
    for child in source:
        if child.find(f"{{{NS}}}ShaderBakeInput") is not None or child.get("Name") == "BakeShippingShaders":
            project.append(copy.deepcopy(child))
    ET.SubElement(project, f"{{{NS}}}Target", Name="ClCompile")
    project_path = fixture / "tracking.proj"
    ET.ElementTree(project).write(project_path, encoding="utf-8", xml_declaration=True)

    def build(label: str, expected: int, design_time: bool = False) -> None:
        command = [str(msbuild), str(project_path), "/nologo", "/v:normal"]
        if design_time:
            command.append("/p:DesignTimeBuild=true")
        result = subprocess.run(command, capture_output=True, text=True)
        (output / f"{label}.log").write_text(result.stdout + result.stderr, encoding="utf-8")
        assert (result.returncode == 0) == (expected == 0), (label, result.returncode, result.stdout[-2000:])

    def count() -> int:
        return len((fixture / "bake-count.txt").read_text().splitlines())

    build("initial", 0)
    assert count() == 1 and all(path.exists() for path in outputs)
    timestamps = [path.stat().st_mtime_ns for path in outputs]
    build("unchanged", 0)
    assert count() == 1 and timestamps == [path.stat().st_mtime_ns for path in outputs]
    time.sleep(0.05)
    (shaders / "shared.hlsli").write_text("changed include\n")
    build("include-edit", 0)
    assert count() == 2
    outputs[0].unlink()
    build("missing-output", 0)
    assert count() == 3 and outputs[0].exists()
    outputs[0].unlink()
    (fixture / "fail-bake").write_text("fail\n")
    build("failed-bake", 7)
    assert count() == 3 and not outputs[0].exists()
    build("design-time", 0, design_time=True)
    assert count() == 3 and not outputs[0].exists()
    (fixture / "fail-bake").unlink()
    build("retry", 0)
    assert count() == 4 and outputs[0].exists()
    read_log = "".join(path.read_text(encoding="utf-8-sig") for path in (fixture / "tracking").glob("*.read.*.tlog")).upper()
    write_log = "".join(path.read_text(encoding="utf-8-sig") for path in (fixture / "tracking").glob("*.write.*.tlog")).upper()
    assert str(shaders / "shared.hlsli").upper() in read_log
    assert str(sdk / "bin/10.0.26100.0/x64/dxc.exe").upper() in read_log
    assert all(str(path).upper() in write_log for path in outputs)
    print("PASS: native shader tlogs, unchanged skip, include edit, missing output, failed bake, retry and design-time skip")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--msbuild", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    run(args.msbuild.resolve(), args.output.resolve())
