#
# File: tools/validate_shaders.py
# Purpose:
#   Documents and runs the validate_shaders.py developer/validation helper script.
#
# Mental model:
#   Tools are command-line guardrails around builds, validation, screenshots,
#   diagnostics, and artifact handling. They make the safe path repeatable and
#   keep output bounded for humans and agents.
#
# Glossary:
#   JSON (JavaScript Object Notation): Structured text format used by
#   diagnostics, baselines, and tool reports.
#   Validation gate: Repository script that proves a class of changes before
#   commit or PR.
#
# Invariants:
#   - Tool output should be bounded and readable because agents and humans use
#   it for decisions.
#
# Related:
#   - AGENTS.md
#   - Agentic/Reference/comment-style-guide.md
#
#
#!/usr/bin/env python3
#
# File: tools/validate_shaders.py
# Purpose:
#   Documents and runs the validate_shaders.py developer/validation helper script.
#
# Mental model:
#   Tools are command-line guardrails around builds, validation, screenshots,
#   diagnostics, and artifact handling. They make the safe path repeatable and
#   keep output bounded for humans and agents.
#
# Glossary:
#   JSON (JavaScript Object Notation): Structured text format used by
#   diagnostics, baselines, and tool reports.
#   Validation gate: Repository script that proves a class of changes before
#   commit or PR.
#
# Invariants:
#   - Tool output should be bounded and readable because agents and humans use
#   it for decisions.
#
# Related:
#   - AGENTS.md
#   - Agentic/Reference/comment-style-guide.md
#
#
"""Validate shader stage contracts and report incomplete manifest coverage."""

from __future__ import annotations

import argparse
import json
import re
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


VALID_STAGES = {"vert", "frag", "hlsl", "dxil"}
VALID_STATUS = {"active", "legacy"}


def repo_relative(repo: Path, path: Path) -> str:
    try:
        return str(path.resolve().relative_to(repo.resolve())).replace("/", "\\")
    except ValueError:
        return str(path)


def load_json(path: Path) -> dict[str, Any]:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError:
        raise SystemExit(f"ERROR: Manifest not found: {path}")
    except json.JSONDecodeError as exc:
        raise SystemExit(f"ERROR: Manifest JSON parse failed: {path}:{exc.lineno}: {exc.msg}") from exc


def shader_root_name(path: Path) -> str:
    for suffix in (".vert", ".frag", ".hlsl", ".dxil"):
        if path.name.endswith(suffix):
            return path.name[: -len(suffix)]
    return path.stem


def discover_shader_files(shader_root: Path) -> dict[str, dict[str, Path]]:
    files: dict[str, dict[str, Path]] = {}
    for path in shader_root.iterdir():
        if not path.is_file():
            continue
        suffix = path.suffix.lower().lstrip(".")
        if suffix not in VALID_STAGES:
            continue
        root = shader_root_name(path)
        files.setdefault(root, {})[suffix] = path
    return files


def discover_source_shader_roots(source_root: Path) -> set[str]:
    roots: set[str] = set()
    create_shader = re.compile(r'CreateShader\(\s*"shaders/([^"]+)"')
    literal_paths = re.compile(r'"shaders/([^"]+\.(?:hlsl|dxil))"')
    for path in source_root.rglob("*"):
        if path.suffix.lower() not in {".cpp", ".h", ".hpp"}:
            continue
        try:
            text = path.read_text(encoding="utf-8", errors="ignore")
        except OSError:
            continue
        roots.update(match.group(1) for match in create_shader.finditer(text))
        for match in literal_paths.finditer(text):
            roots.add(shader_root_name(Path(match.group(1))))
    return roots


def load_stage_texts(stage_paths: dict[str, Path]) -> str:
    chunks: list[str] = []
    for stage, path in stage_paths.items():
        if stage == "dxil":
            continue
        try:
            chunks.append(path.read_text(encoding="utf-8", errors="ignore"))
        except OSError:
            pass
    return "\n".join(chunks)


def discover_hlsl_uniforms(text: str) -> set[str]:
    uniforms: set[str] = set()
    cbuffer_pattern = re.compile(r"cbuffer\s+\w+\s*(?::\s*register\([^)]+\))?\s*\{(?P<body>.*?)\};", re.DOTALL)
    variable_pattern = re.compile(r"\b(?:float4x4|float4|float3|float2|float|int|uint|bool)\s+(\w+)(?:\s*\[[^\]]+\])?\s*;")
    for cbuffer in cbuffer_pattern.finditer(text):
        body = re.sub(r"//.*", "", cbuffer.group("body"))
        uniforms.update(match.group(1) for match in variable_pattern.finditer(body))
    return uniforms


def discover_hlsl_resources(text: str) -> dict[str, dict[str, int | str]]:
    resources: dict[str, dict[str, int | str]] = {}
    resource_pattern = re.compile(
        r"\b(?P<type>(?:RW)?Texture\w*(?:<[^>]+>)?|Sampler\w+|StructuredBuffer(?:<[^>]+>)?)\s+"
        r"(?P<name>\w+)\s*:\s*register\((?P<class>[tus])(?P<slot>\d+)\)"
    )
    for match in resource_pattern.finditer(text):
        resources[match.group("name")] = {
            "type": match.group("type"),
            "registerClass": match.group("class"),
            "slot": int(match.group("slot")),
        }
    return resources


def validate_manifest(
    repo: Path,
    manifest: dict[str, Any],
    shader_files: dict[str, dict[str, Path]],
    source_roots: set[str],
) -> tuple[list[str], list[str], list[dict[str, Any]]]:
    errors: list[str] = []
    warnings: list[str] = []
    contracts_summary: list[dict[str, Any]] = []

    contracts = manifest.get("contracts")
    if not isinstance(contracts, list):
        return ["Manifest field 'contracts' must be a list."], warnings, contracts_summary

    seen: set[str] = set()
    manifest_roots: set[str] = set()

    for index, contract in enumerate(contracts):
        if not isinstance(contract, dict):
            errors.append(f"contracts[{index}] must be an object.")
            continue

        name = contract.get("name")
        status = contract.get("status", "active")
        stages = contract.get("stages", [])
        required_symbols = contract.get("requiredSymbols", [])
        required_uniforms = contract.get("uniforms", [])
        required_resources = contract.get("resources", [])

        if not isinstance(name, str) or not name:
            errors.append(f"contracts[{index}] has invalid name.")
            continue
        if name in seen:
            errors.append(f"{name}: duplicate manifest entry.")
            continue
        seen.add(name)
        manifest_roots.add(name)

        if status not in VALID_STATUS:
            errors.append(f"{name}: invalid status '{status}'.")
        if not isinstance(stages, list) or not stages:
            errors.append(f"{name}: stages must be a non-empty list.")
            stages = []

        stage_paths = shader_files.get(name, {})
        missing_stages: list[str] = []
        for stage in stages:
            if stage not in VALID_STAGES:
                errors.append(f"{name}: invalid stage '{stage}'.")
                continue
            if stage not in stage_paths:
                missing_stages.append(stage)
        if missing_stages:
            errors.append(f"{name}: missing expected stage file(s): {', '.join(missing_stages)}.")

        if name not in shader_files:
            errors.append(f"{name}: no shader files found.")

        combined_text = load_stage_texts(stage_paths)

        has_contract_checks = bool(required_symbols) or bool(required_uniforms) or bool(required_resources)
        if not has_contract_checks:
            warnings.append(f"{name}: manifest only checks file presence; symbol/resource contract is incomplete.")
        elif required_symbols and (not isinstance(required_symbols, list) or not all(isinstance(symbol, str) for symbol in required_symbols)):
            errors.append(f"{name}: requiredSymbols must be a string list.")
        elif required_symbols:
            missing_symbols = [symbol for symbol in required_symbols if symbol not in combined_text]
            if missing_symbols:
                errors.append(f"{name}: missing required symbol(s): {', '.join(missing_symbols)}.")

        if required_uniforms:
            if not isinstance(required_uniforms, list) or not all(isinstance(uniform, str) for uniform in required_uniforms):
                errors.append(f"{name}: uniforms must be a string list.")
            else:
                declared_uniforms = discover_hlsl_uniforms(combined_text)
                missing_uniforms = [uniform for uniform in required_uniforms if uniform not in declared_uniforms]
                if missing_uniforms:
                    errors.append(f"{name}: missing cbuffer uniform declaration(s): {', '.join(missing_uniforms)}.")

        if required_resources:
            if not isinstance(required_resources, list):
                errors.append(f"{name}: resources must be a list.")
            else:
                declared_resources = discover_hlsl_resources(combined_text)
                for resource_index, resource in enumerate(required_resources):
                    if not isinstance(resource, dict):
                        errors.append(f"{name}: resources[{resource_index}] must be an object.")
                        continue
                    resource_name = resource.get("name")
                    expected_slot = resource.get("slot")
                    expected_register_class = resource.get("registerClass", "t")
                    if not isinstance(resource_name, str) or not resource_name:
                        errors.append(f"{name}: resources[{resource_index}] has invalid name.")
                        continue
                    if not isinstance(expected_slot, int):
                        errors.append(f"{name}: resources[{resource_index}] has invalid slot.")
                        continue
                    if expected_register_class not in {"t", "u", "s"}:
                        errors.append(f"{name}: resources[{resource_index}] has invalid registerClass.")
                        continue
                    declared = declared_resources.get(resource_name)
                    if not declared:
                        errors.append(f"{name}: missing resource declaration: {resource_name}.")
                        continue
                    if declared["registerClass"] != expected_register_class or declared["slot"] != expected_slot:
                        errors.append(
                            f"{name}: resource {resource_name} expected {expected_register_class}{expected_slot}, "
                            f"found {declared['registerClass']}{declared['slot']}."
                        )

        if name in source_roots and status == "legacy":
            errors.append(f"{name}: marked legacy but referenced by source CreateShader/literal path.")

        contracts_summary.append(
            {
                "name": name,
                "status": status,
                "stages": stages,
                "files": {stage: repo_relative(repo, path) for stage, path in sorted(stage_paths.items())},
                "requiredSymbols": required_symbols,
                "uniforms": required_uniforms,
                "resources": required_resources,
                "referencedBySource": name in source_roots,
            }
        )

    for root in sorted(set(shader_files) - manifest_roots):
        warnings.append(f"{root}: shader files exist but root is not listed in shader_contracts.json.")

    for root in sorted(source_roots - manifest_roots):
        warnings.append(f"{root}: source references shader root but manifest has no entry.")

    return errors, warnings, contracts_summary


def write_summary(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--manifest", type=Path, default=None)
    parser.add_argument("--json-out", type=Path, default=None)
    parser.add_argument("--warnings-as-errors", action="store_true")
    args = parser.parse_args()

    repo = args.repo.resolve()
    manifest_path = args.manifest or repo / "tools" / "shader_contracts.json"
    manifest = load_json(manifest_path)

    shader_root = repo / manifest.get("shaderRoot", "SkullbonezData/shaders")
    if not shader_root.exists():
        print(f"ERROR: Shader root not found: {shader_root}")
        return 1

    shader_files = discover_shader_files(shader_root)
    source_roots = discover_source_shader_roots(repo / "SkullbonezSource")
    errors, warnings, contracts = validate_manifest(repo, manifest, shader_files, source_roots)

    if args.warnings_as_errors and warnings:
        errors.extend(f"warning treated as error: {warning}" for warning in warnings)

    summary_path = args.json_out or repo / "TestOutput" / "validation" / "shaders" / "summary.json"
    summary = {
        "generatedAtUtc": datetime.now(timezone.utc).isoformat(),
        "manifest": repo_relative(repo, manifest_path),
        "shaderRoot": repo_relative(repo, shader_root),
        "status": "pass" if not errors else "fail",
        "errorCount": len(errors),
        "warningCount": len(warnings),
        "errors": errors,
        "warnings": warnings,
        "contracts": contracts,
    }
    write_summary(summary_path, summary)

    for warning in warnings:
        print(f"WARNING: {warning}")
    for error in errors:
        print(f"ERROR: {error}")
    print(
        f"Shader contract summary: {repo_relative(repo, summary_path)} "
        f"({len(errors)} errors, {len(warnings)} warnings)"
    )

    if errors:
        print("FAIL: Shader contract validation failed.")
        return 1

    print("PASS: Shader contract validation passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
