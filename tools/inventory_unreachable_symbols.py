#!/usr/bin/env python3
"""
File: inventory_unreachable_symbols.py
Purpose:
  Inventory ordinary out-of-line first-party C++ function definitions in .cpp
  files that have no production caller outside their own translation unit.

Summary:
  The repository's balanced C++ scanner supplies normalized definitions and
  call-shaped occurrences. This inventory joins .cpp definitions to header
  declarations, respects default-argument arity, and separates no-reference,
  own-TU-only, and test-only rows. Exact current-source owner rulings make every
  reported row reviewable without turning the population into a count budget.

Mental model:
  This is a review inventory, not a linker. A row means no decorated-object or
  unique source edge roots the definition through the current production relay
  graph. The row still names lexical uncertainty; owner evidence remains
  appropriate before deleting a virtual, configured, or callback seam.

Glossary:
  External production caller: A call-shaped occurrence in a different
    SkullbonezSource file from the definition.
  Own-TU-only: A definition called only from its defining .cpp file.
  Test-only: A definition reached by SkullbonezTests or Agentic/Tests but not
    by another production translation unit.
  Permitted arity: Every argument count from the required parameter count
    through the declared parameter count when trailing defaults exist, or every
    count at or above the fixed prefix for a variadic operation.
  Current ruling: Owner judgement keyed by definition file and normalized
    signature; moving or changing the symbol invalidates it.

Invariants:
  - Only ordinary .cpp function definitions with a matching first-party header
    declaration enter the inventory; constructors, destructors, operators,
    inline/header definitions, and internal-linkage helpers are outside this
    inventory and remain review-owned.
  - Comments, literals, and preprocessor directive text cannot manufacture
    definitions or calls. Calls in conditional bodies remain visible so
    Debug-only and platform-only seams are not reported as unreferenced.
  - Automation/Debug/Profile objects own production and SKULLBONEZ_TESTS edges.
    Unique
    cross-TU source calls supplement relocations erased by inlining, while
    constructors, destructors, callbacks, and other non-reportable functions
    relay roots without entering the reported population. Standalone
    Agentic/Tests projects contribute masked lexical test edges because their
    objects live outside those three configuration roots.
  - Compiler-generated adapter identities and emitted template specialization
    sets may root one source definition; they never become separate ruling rows.
  - Overloads with different arities are evaluated independently.
  - Every current row needs an exact ruling, and every stale ruling fails strict
    mode. The row count is never an allowance, ceiling, or ratchet.
  - The tool is read-only.

Related:
  - AGENTS.md
  - tools/cpp_source_scan.py
  - tools/inventory_wide_signatures.py
  - Agentic/Reports/2026-07-30/maths-surface-reachability-closure.md
  - Agentic/Reports/2026-07-30/unreachable-symbol-remediation-closure.md
"""

from __future__ import annotations

import argparse
import ctypes
import functools
import json
import os
import re
import subprocess
import sys
import tempfile
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Iterable

sys.path.insert(0, str(Path(__file__).resolve().parent))

from inventory_wide_signatures import (  # noqa: E402
    Candidate,
    matching_pairs,
    mask_cpp,
    normalize_space,
    parameter_type_identity,
    qualified_name_before,
    scan_file,
    split_top_level,
    strip_top_level_default,
)
from inventory_function_complexity import _definition_body  # noqa: E402


SOURCE_SUFFIXES = {".cpp", ".h", ".hpp", ".inl"}
DEFAULT_RULINGS_PATH = Path("tools/reachability_rulings.json")
RULING_DISPOSITIONS = {"retain-owner", "repair-plan"}


@dataclass(frozen=True)
class ReachabilityRow:
    file: str
    line: int
    name: str
    signature: str
    required_arity: int
    total_arity: int
    own_tu_calls: int
    test_calls: int
    ambiguous_production_calls: int
    symbol_mapping: str
    compiler_symbols: tuple[str, ...]
    classification: str

    @property
    def key(self) -> tuple[str, str]:
        return self.file, self.signature


def _tracked_files(repo: Path, roots: Iterable[str]) -> list[Path]:
    command = ["git", "ls-files", "-z", "--", *roots]
    result = subprocess.run(command, cwd=repo, check=True, capture_output=True)
    relative_paths = result.stdout.decode("utf-8", errors="strict").split("\0")
    return [
        repo / relative
        for relative in relative_paths
        if relative
        and Path(relative).suffix.lower() in SOURCE_SUFFIXES
        and (repo / relative).is_file()
    ]


def _identity(candidate: Candidate) -> tuple[str, tuple[str, ...]]:
    return (
        candidate.qualified_name,
        tuple(parameter_type_identity(parameter) for parameter in candidate.parameters),
    )


def _required_arity(candidate: Candidate) -> int:
    return sum(
        1
        for parameter in candidate.parameters
        if parameter != "..." and strip_top_level_default(parameter) == parameter
    )


def _declaration_required_arities(
    candidates_by_file: dict[Path, list[Candidate]],
) -> dict[tuple[str, tuple[str, ...]], int]:
    required_arities: dict[tuple[str, tuple[str, ...]], int] = {}
    for path, candidates in candidates_by_file.items():
        if path.suffix.lower() == ".cpp":
            continue
        for candidate in candidates:
            if candidate.is_definition:
                continue
            identity = _identity(candidate)
            required = _required_arity(candidate)
            prior = required_arities.get(identity)
            required_arities[identity] = required if prior is None else min(prior, required)
    return required_arities


def _anonymous_namespace_spans(masked: str) -> list[tuple[int, int]]:
    brace_pairs = matching_pairs(masked, "{", "}")
    spans: list[tuple[int, int]] = []
    for match in re.finditer(r"\bnamespace\s*\{", masked):
        opening = masked.find("{", match.start(), match.end())
        closing = brace_pairs.get(opening)
        if closing is not None:
            spans.append((opening, closing))
    return spans


def _eligible_definitions(
    candidates_by_file: dict[Path, list[Candidate]],
    masked_by_file: dict[Path, str],
) -> list[tuple[Path, Candidate]]:
    declarations = _declaration_required_arities(candidates_by_file)

    definitions: list[tuple[Path, Candidate]] = []
    for path, candidates in candidates_by_file.items():
        if path.suffix.lower() != ".cpp":
            continue
        anonymous_spans = _anonymous_namespace_spans(masked_by_file[path])
        for candidate in candidates:
            if (
                candidate.is_definition
                and candidate.kind != "constructor"
                and not candidate.simple_name.startswith("~")
                and not candidate.simple_name.startswith("operator")
                and not any(
                    opening < candidate.opening_paren < closing
                    for opening, closing in anonymous_spans
                )
                and _identity(candidate) in declarations
            ):
                definitions.append((path, candidate))
    return definitions


def _graph_definitions(
    candidates_by_file: dict[Path, list[Candidate]],
    masked_by_file: dict[Path, str],
    repo: Path,
) -> list[tuple[Path, Candidate]]:
    definitions = [
        (path, candidate)
        for path, candidates in candidates_by_file.items()
        if path.suffix.lower() == ".cpp"
        for candidate in candidates
        if candidate.is_definition
    ]
    for path, masked in masked_by_file.items():
        if path.suffix.lower() == ".cpp":
            definitions.extend(
                (path, candidate)
                for candidate in _lifecycle_candidates(path, repo, masked)
            )
    return definitions


def _lifecycle_candidates(
    path: Path,
    repo: Path,
    masked: str,
) -> list[Candidate]:
    """Supply out-of-class constructors/destructors as graph-only relays."""
    paren_pairs = matching_pairs(masked, "(", ")")
    brace_pairs = matching_pairs(masked, "{", "}")
    pattern = re.compile(
        r"\b(?P<scope>(?:[A-Za-z_]\w*::)*[A-Za-z_]\w*)::"
        r"(?P<name>~?[A-Za-z_]\w*)\s*(?P<opening>\()"
    )
    candidates: list[Candidate] = []
    for match in pattern.finditer(masked):
        class_name = match.group("scope").split("::")[-1]
        name = match.group("name")
        if name.lstrip("~") != class_name:
            continue
        opening = match.start("opening")
        closing = paren_pairs.get(opening)
        if closing is None:
            continue
        parameters = tuple(
            normalize_space(parameter)
            for parameter in split_top_level(masked[opening + 1 : closing])
        )
        qualified_name = f"{match.group('scope')}::{name}"
        candidate = Candidate(
            file=str(path.relative_to(repo)).replace("\\", "/"),
            line=masked.count("\n", 0, match.start("scope")) + 1,
            qualified_name=qualified_name,
            simple_name=name,
            scope_hint=class_name,
            kind="destructor" if name.startswith("~") else "constructor",
            arity=len(parameters),
            parameters=parameters,
            signature=normalize_space(
                f"{qualified_name}({masked[opening + 1 : closing]})"
            ),
            is_definition=True,
            opening_paren=opening,
            closing_paren=closing,
        )
        if _definition_body(masked, candidate, brace_pairs) is not None:
            candidates.append(candidate)
    return candidates


def _definition_signature(masked: str, candidate: Candidate) -> str:
    body = _definition_body(
        masked,
        candidate,
        matching_pairs(masked, "{", "}"),
    )
    if body is None:
        return candidate.signature
    suffix = normalize_space(masked[candidate.closing_paren + 1 : body[0]])
    return normalize_space(f"{candidate.signature} {suffix}")


def _arity_is_permitted(
    candidate: Candidate,
    required_arity: int,
    call_arity: int,
) -> bool:
    if candidate.parameters and candidate.parameters[-1] == "...":
        return call_arity >= required_arity
    return call_arity in range(required_arity, candidate.arity + 1)


def _call_occurrences(masked: str) -> list[tuple[int, str, str, int]]:
    """Return call offset, qualified spelling, final name, and argument count."""
    occurrences: list[tuple[int, str, str, int]] = []
    for opening, closing in sorted(matching_pairs(masked, "(", ")").items()):
        name_result = qualified_name_before(masked, opening)
        if not name_result:
            cursor = opening - 1
            while cursor >= 0 and masked[cursor].isspace():
                cursor -= 1
            if cursor >= 0 and masked[cursor] == ">":
                depth = 1
                cursor -= 1
                while cursor >= 0 and depth:
                    if masked[cursor] == ">":
                        depth += 1
                    elif masked[cursor] == "<":
                        depth -= 1
                    cursor -= 1
                if depth == 0:
                    match = re.search(
                        r"((?:~?[A-Za-z_]\w*::)*~?[A-Za-z_]\w*)\s*$",
                        masked[: cursor + 1],
                    )
                    if match:
                        name_result = (match.group(1), match.start(1))
        if not name_result:
            continue
        qualified_name, _ = name_result
        simple_name = qualified_name.split("::")[-1].lstrip("~")
        arity = len(split_top_level(masked[opening + 1 : closing]))
        occurrences.append((opening, qualified_name, simple_name, arity))
    return occurrences


def _call_matches(
    qualified_call: str,
    simple_name: str,
    arity: int,
    candidate: Candidate,
    required_arity: int,
) -> bool:
    if candidate.simple_name != simple_name or not _arity_is_permitted(
        candidate, required_arity, arity
    ):
        return False
    if "::" in qualified_call:
        return candidate.qualified_name.endswith(qualified_call)
    return True


def _classification(own_calls: int, test_calls: int) -> str:
    if own_calls and test_calls:
        return "own-tu-and-test-only"
    if own_calls:
        return "own-tu-only"
    if test_calls:
        return "test-only"
    return "no-reference"


def _find_msvc_tool(pattern: str, display_name: str) -> Path:
    vswhere = Path(r"C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe")
    result = subprocess.run(
        [
            str(vswhere),
            "-latest",
            "-products",
            "*",
            "-requires",
            "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
            "-find",
            pattern,
        ],
        check=True,
        capture_output=True,
        text=True,
    )
    paths = [Path(line.strip()) for line in result.stdout.splitlines() if line.strip()]
    if not paths:
        raise OSError(f"{display_name} was not found")
    return paths[-1]


def _find_dumpbin() -> Path:
    return _find_msvc_tool(
        r"VC\Tools\MSVC\**\bin\Hostx64\x64\dumpbin.exe",
        "dumpbin.exe",
    )


def _find_cl() -> Path:
    return _find_msvc_tool(
        r"VC\Tools\MSVC\**\bin\Hostx64\x64\cl.exe",
        "cl.exe",
    )


@functools.lru_cache(maxsize=1)
def _undecorate_function():
    undecorate = ctypes.WinDLL("dbghelp").UnDecorateSymbolNameW
    undecorate.argtypes = [ctypes.c_wchar_p, ctypes.c_wchar_p, ctypes.c_uint32, ctypes.c_uint32]
    undecorate.restype = ctypes.c_uint32
    return undecorate


def _demangle(symbol: str) -> str:
    buffer = ctypes.create_unicode_buffer(8192)
    if _undecorate_function()(symbol, buffer, len(buffer), 0):
        return buffer.value
    return symbol


def _coff_index(
    object_roots: list[Path],
    source_paths: list[Path],
) -> tuple[dict[str, set[Path]], dict[str, set[Path]], dict[str, str]]:
    resolved_roots = {root.resolve() for root in object_roots}
    configuration_names = {root.name.lower() for root in resolved_roots}
    required_configurations = {"automation", "debug", "profile"}
    if len(resolved_roots) != 3 or configuration_names != required_configurations:
        raise OSError("reachability scan requires distinct Automation, Debug, and Profile object roots")
    production_sources = {
        path.stem.lower(): path
        for path in source_paths
        if path.suffix.lower() == ".cpp"
        and "SkullbonezTests" not in path.parts
        and "Agentic" not in path.parts
    }
    test_sources = {
        path.stem.lower(): path
        for path in source_paths
        if path.suffix.lower() == ".cpp"
        and ("SkullbonezTests" in path.parts or "Agentic" in path.parts)
    }
    object_paths: list[Path] = []
    for root in object_roots:
        root_objects = sorted(path.resolve() for path in root.rglob("*.obj"))
        if not root_objects:
            raise OSError(f"object root has no objects: {root}")
        current_objects: list[Path] = []
        stale_objects: list[Path] = []
        for path in root_objects:
            normalized = str(path).replace("\\", "/").lower()
            source_map = test_sources if "/skullbonez_tests/" in normalized else production_sources
            source = source_map.get(path.stem.lower())
            if source is None:
                continue
            current_objects.append(path)
            if path.stat().st_mtime_ns < source.stat().st_mtime_ns:
                stale_objects.append(path)
        if not current_objects:
            raise OSError(f"object root has no current-source objects: {root}")
        if stale_objects:
            names = ", ".join(path.name for path in stale_objects[:5])
            suffix = "" if len(stale_objects) <= 5 else f" (+{len(stale_objects) - 5} more)"
            raise OSError(f"object root contains stale current-source objects: {names}{suffix}")
        object_paths.extend(current_objects)
    dumpbin = _find_dumpbin()
    definitions: dict[str, set[Path]] = {}
    references: dict[str, set[Path]] = {}
    symbol_line = re.compile(
        r"^\s*[0-9A-F]+\s+[0-9A-F]+\s+(?P<section>\S+).*?\bExternal\b.*?\|\s+(?P<symbol>\S+)"
    )
    current_file: Path | None = None
    for start in range(0, len(object_paths), 40):
        result = subprocess.run(
            [str(dumpbin), "/nologo", "/symbols", *map(str, object_paths[start : start + 40])],
            check=True,
            capture_output=True,
            text=True,
            errors="replace",
        )
        for line in result.stdout.splitlines():
            if line.startswith("Dump of file "):
                current_file = Path(line[len("Dump of file ") :].strip()).resolve()
                continue
            match = symbol_line.match(line)
            if match is None or current_file is None:
                continue
            symbol = match.group("symbol")
            if not symbol.startswith("?"):
                continue
            destination = references if match.group("section") == "UNDEF" else definitions
            destination.setdefault(symbol, set()).add(current_file)
    demangled = {symbol: _demangle(symbol) for symbol in definitions}
    return definitions, references, demangled


@functools.lru_cache(maxsize=None)
def _demangled_function_parts(
    demangled: str,
) -> tuple[str, tuple[str, ...], str] | None:
    """Return the actual outer function prefix, parameters, and suffix."""
    pairs = matching_pairs(demangled, "(", ")")
    angle_depth = 0
    for index, character in enumerate(demangled):
        if character == "<":
            angle_depth += 1
        elif character == ">" and angle_depth:
            angle_depth -= 1
        elif character == "(" and angle_depth == 0:
            closing = pairs.get(index)
            if closing is None:
                return None
            parameters = tuple(split_top_level(demangled[index + 1 : closing]))
            if parameters == ("void",):
                parameters = ()
            return (
                demangled[:index].rstrip(),
                parameters,
                demangled[closing + 1 :].strip(),
            )
    return None


def _type_markers(parameter: str) -> set[str]:
    ignored = {
        "class",
        "const",
        "enum",
        "signed",
        "struct",
        "unsigned",
        "volatile",
    }
    return {
        word
        for word in re.findall(r"[A-Za-z_]\w*", parameter_type_identity(parameter))
        if word not in ignored
    }


def _compiled_type_markers(type_text: str) -> set[str]:
    """Return type words from demangled text, which carries no parameter name."""
    ignored = {
        "__cdecl",
        "__ptr64",
        "class",
        "const",
        "enum",
        "private",
        "protected",
        "public",
        "signed",
        "static",
        "struct",
        "unsigned",
        "volatile",
    }
    return {
        word
        for word in re.findall(r"[A-Za-z_]\w*", type_text)
        if word not in ignored
    }


def _candidate_symbols(
    definition_path: Path,
    candidate: Candidate,
    definition_signature: str,
    definitions: dict[str, set[Path]],
    demangled: dict[str, str],
    symbols_by_simple_name: dict[str, list[str]],
) -> tuple[str, ...]:
    object_stem = definition_path.stem.lower()
    candidates: list[tuple[int, str]] = []
    source_suffix = definition_signature.rsplit(")", 1)[-1]
    source_is_const = bool(re.search(r"\bconst\b", source_suffix))
    source_name_offset = candidate.signature.find(f"{candidate.qualified_name}(")
    source_return = (
        candidate.signature[:source_name_offset]
        if source_name_offset >= 0
        else ""
    )
    source_return_markers = _type_markers(source_return)
    for symbol in symbols_by_simple_name.get(
        candidate.simple_name.lstrip("~"),
        [],
    ):
        defining_objects = definitions[symbol]
        if not any(path.stem.lower() == object_stem for path in defining_objects):
            continue
        text = demangled[symbol]
        parts = _demangled_function_parts(text)
        if parts is None:
            continue
        function_prefix, parameters, function_suffix = parts
        # Invariant: a compiler-generated wrapper that merely names a callback
        # in a template argument is not the callback's emitted definition.
        if not function_prefix.endswith(candidate.qualified_name):
            continue
        if len(parameters) != candidate.arity:
            continue
        if source_is_const != bool(re.search(r"\bconst\b", function_suffix)):
            continue
        if any(
            ("&" in source or "*" in source)
            and bool(re.search(r"\bconst\b", source))
            != bool(re.search(r"\bconst\b", compiled))
            for source, compiled in zip(candidate.parameters, parameters)
        ):
            continue
        parameter_score = sum(
            len(_type_markers(source) & _compiled_type_markers(compiled))
            for source, compiled in zip(candidate.parameters, parameters)
        )
        compiled_return = function_prefix[: -len(candidate.qualified_name)]
        if (
            ("&" in source_return or "*" in source_return)
            and bool(re.search(r"\bconst\b", source_return))
            != bool(re.search(r"\bconst\b", compiled_return))
        ):
            continue
        return_score = len(
            source_return_markers & _compiled_type_markers(compiled_return)
        )
        score = parameter_score * 2 + return_score
        candidates.append((score, symbol))
    if not candidates:
        return ()
    best_score = max(score for score, _ in candidates)
    return tuple(sorted(symbol for score, symbol in candidates if score == best_score))


def _compiler_instantiation_mentions(
    candidate: Candidate,
    object_symbols: list[str],
    demangled: dict[str, str],
    direct_symbols: tuple[str, ...],
) -> tuple[str, ...]:
    marker = f"{candidate.qualified_name}("
    mentions: list[str] = []
    for symbol in object_symbols:
        if symbol in direct_symbols:
            continue
        parts = _demangled_function_parts(demangled[symbol])
        if parts is not None and parts[0].endswith(candidate.qualified_name):
            continue
        if marker in demangled[symbol]:
            mentions.append(symbol)
    return tuple(sorted(mentions))


def _compiler_specialization_base(demangled: str) -> str | None:
    """Return the source function name for one outer template specialization."""
    parts = _demangled_function_parts(demangled)
    if parts is None:
        return None
    prefix = parts[0].rstrip()
    if not prefix.endswith(">"):
        return None
    closing = len(prefix) - 1
    opening = next(
        (
            candidate_opening
            for candidate_opening, candidate_closing in matching_pairs(
                prefix, "<", ">"
            ).items()
            if candidate_closing == closing
        ),
        None,
    )
    if opening is None:
        return None
    match = re.search(
        r"((?:[A-Za-z_]\w*::)*[A-Za-z_]\w*)\s*$",
        prefix[:opening],
    )
    return match.group(1).split("::")[-1] if match else None


def _template_arguments_before(masked: str, call_opening: int) -> str | None:
    cursor = call_opening - 1
    while cursor >= 0 and masked[cursor].isspace():
        cursor -= 1
    if cursor < 0 or masked[cursor] != ">":
        return None
    depth = 1
    closing = cursor
    cursor -= 1
    while cursor >= 0 and depth:
        if masked[cursor] == ">":
            depth += 1
        elif masked[cursor] == "<":
            depth -= 1
        cursor -= 1
    if depth:
        return None
    return masked[cursor + 2 : closing]


def _candidate_specialization_symbols(
    definition_path: Path,
    candidate: Candidate,
    definitions: dict[str, set[Path]],
    demangled: dict[str, str],
) -> tuple[str, ...]:
    if not candidate.signature.startswith("template <"):
        return ()
    marker = f"{candidate.qualified_name}<"
    object_stem = definition_path.stem.lower()
    symbols: list[str] = []
    for symbol, object_paths in definitions.items():
        if not any(path.stem.lower() == object_stem for path in object_paths):
            continue
        parts = _demangled_function_parts(demangled[symbol])
        if (
            parts is not None
            and marker in parts[0]
            and len(parts[1]) == candidate.arity
        ):
            symbols.append(symbol)
    return tuple(sorted(symbols))


def scan_paths(
    repo: Path,
    production_files: list[Path],
    test_files: list[Path],
    object_roots: list[Path] | None = None,
) -> list[ReachabilityRow]:
    candidates_by_file: dict[Path, list[Candidate]] = {}
    masked_by_file: dict[Path, str] = {}
    production_occurrences: dict[Path, list[tuple[int, str, str, int]]] = {}
    test_occurrences: list[tuple[int, str, str, int]] = []
    agentic_test_occurrences: list[tuple[int, str, str, int]] = []

    for path in production_files:
        text = path.read_text(encoding="utf-8", errors="strict")
        masked = mask_cpp(text)
        call_masked = mask_cpp(text, preserve_literal_argument=True)
        candidates, _ = scan_file(path, repo, text=text, masked=masked)
        candidates_by_file[path] = candidates
        masked_by_file[path] = masked
        declaration_opens = {candidate.opening_paren for candidate in candidates}
        production_occurrences[path] = [
            occurrence
            for occurrence in _call_occurrences(call_masked)
            if occurrence[0] not in declaration_opens
        ]
    for path in test_files:
        text = path.read_text(encoding="utf-8", errors="strict")
        masked = mask_cpp(text)
        call_masked = mask_cpp(text, preserve_literal_argument=True)
        candidates, _ = scan_file(path, repo, text=text, masked=masked)
        declaration_opens = {candidate.opening_paren for candidate in candidates}
        occurrences = [
            occurrence
            for occurrence in _call_occurrences(call_masked)
            if occurrence[0] not in declaration_opens
        ]
        test_occurrences.extend(occurrences)
        if "Agentic" in path.parts:
            agentic_test_occurrences.extend(occurrences)

    reportable_definitions = _eligible_definitions(
        candidates_by_file,
        masked_by_file,
    )
    definitions = _graph_definitions(candidates_by_file, masked_by_file, repo)
    reportable_identity = {
        (path, candidate.opening_paren)
        for path, candidate in reportable_definitions
    }
    declaration_required_arities = _declaration_required_arities(candidates_by_file)
    nodes = {index: item for index, item in enumerate(definitions)}
    nodes_by_simple_name: dict[str, list[int]] = {}
    nodes_by_path: dict[Path, list[int]] = {}
    for index, (definition_path, candidate) in nodes.items():
        nodes_by_simple_name.setdefault(candidate.simple_name, []).append(index)
        nodes_by_path.setdefault(definition_path, []).append(index)
    reportable_indices = {
        index
        for index, (path, candidate) in nodes.items()
        if (path, candidate.opening_paren) in reportable_identity
    }
    required_arities = {
        index: declaration_required_arities.get(
            _identity(candidate),
            _required_arity(candidate),
        )
        for index, (_, candidate) in nodes.items()
    }
    external_seeds: set[int] = set()
    test_seeds: set[int] = set()
    incoming_own_tu = {index: 0 for index in nodes}
    edges: dict[int, set[int]] = {index: set() for index in nodes}
    ambiguous_external = {index: 0 for index in nodes}
    compiler_symbols: dict[int, tuple[str, ...]] = {index: () for index in nodes}
    compiler_mapping: dict[int, str] = {index: "missing" for index in nodes}
    compiler_mentions: dict[int, tuple[str, ...]] = {index: () for index in nodes}

    coff_definitions: dict[str, set[Path]] = {}
    coff_references: dict[str, set[Path]] = {}
    coff_demangled: dict[str, str] = {}
    if object_roots:
        coff_definitions, coff_references, coff_demangled = _coff_index(
            object_roots,
            production_files + test_files,
        )
        symbols_by_simple_name: dict[str, list[str]] = {}
        for symbol, text in coff_demangled.items():
            opening = text.find("(")
            if opening < 0:
                continue
            name_match = re.search(r"([A-Za-z_]\w*)\s*$", text[:opening])
            if name_match:
                symbols_by_simple_name.setdefault(name_match.group(1), []).append(symbol)
        symbols_by_object_stem: dict[str, list[str]] = {}
        for symbol, object_paths in coff_definitions.items():
            for object_path in object_paths:
                symbols_by_object_stem.setdefault(
                    object_path.stem.lower(),
                    [],
                ).append(symbol)
        for index, (definition_path, candidate) in nodes.items():
            definition_signature = _definition_signature(
                masked_by_file[definition_path],
                candidate,
            )
            symbols = _candidate_symbols(
                definition_path,
                candidate,
                definition_signature,
                coff_definitions,
                coff_demangled,
                symbols_by_simple_name,
            )
            if not symbols:
                symbols = _candidate_specialization_symbols(
                    definition_path,
                    candidate,
                    coff_definitions,
                    coff_demangled,
                )
                if symbols:
                    compiler_mapping[index] = "specialization-set"
            compiler_symbols[index] = symbols
            if len(symbols) == 1:
                compiler_mapping[index] = "exact"
            elif len(symbols) > 1 and compiler_mapping[index] != "specialization-set":
                compiler_mapping[index] = "ambiguous"
            compiler_mentions[index] = _compiler_instantiation_mentions(
                candidate,
                symbols_by_object_stem.get(definition_path.stem.lower(), []),
                coff_demangled,
                symbols,
            )
            if len(symbols) != 1:
                continue
            symbol = symbols[0]
            for reference_path in coff_references.get(symbol, set()):
                normalized = str(reference_path).replace("\\", "/").lower()
                if "/skullbonez_tests/" in normalized:
                    test_seeds.add(index)
                elif reference_path.stem.lower() != definition_path.stem.lower():
                    external_seeds.add(index)

        symbol_owners: dict[str, set[int]] = {}
        for owner_index, symbols in compiler_symbols.items():
            for symbol in symbols:
                symbol_owners.setdefault(symbol, set()).add(owner_index)

        # Concept: a non-type function/member pointer can be compiled into an
        # adapter without leaving an UNDEF relocation to the callback. The
        # emitted identity proves an adapter-to-callback edge, not a root: an
        # uncalled binder must leave its callback in the review inventory.
        for callee_index, mentioned_symbols in compiler_mentions.items():
            callee_path = nodes[callee_index][0]
            callee = nodes[callee_index][1]
            for symbol in mentioned_symbols:
                owners = symbol_owners.get(symbol, set())
                if owners:
                    for owner_index in owners:
                        edges[owner_index].add(callee_index)
                        if nodes[owner_index][0] == callee_path:
                            incoming_own_tu[callee_index] += 1
                    continue

                adapter_base = _compiler_specialization_base(
                    coff_demangled[symbol]
                )
                object_stems = {
                    path.stem.lower()
                    for path in coff_definitions.get(symbol, set())
                }
                source_callers: set[int] = set()
                if adapter_base:
                    for caller_path, occurrences in production_occurrences.items():
                        if caller_path.stem.lower() not in object_stems:
                            continue
                        brace_pairs = matching_pairs(
                            masked_by_file[caller_path], "{", "}"
                        )
                        caller_bodies = [
                            (
                                owner_index,
                                _definition_body(
                                    masked_by_file[caller_path],
                                    nodes[owner_index][1],
                                    brace_pairs,
                                ),
                            )
                            for owner_index in nodes_by_path.get(caller_path, [])
                        ]
                        for opening, _, simple_name, _ in occurrences:
                            if simple_name != adapter_base:
                                continue
                            arguments = _template_arguments_before(
                                masked_by_file[caller_path],
                                opening,
                            )
                            if arguments is None or not re.search(
                                rf"\b{re.escape(callee.simple_name.lstrip('~'))}\b",
                                arguments,
                            ):
                                continue
                            source_callers.update(
                                owner_index
                                for owner_index, body in caller_bodies
                                if body is not None and body[0] < opening < body[1]
                            )
                if source_callers:
                    for owner_index in source_callers:
                        edges[owner_index].add(callee_index)
                        if nodes[owner_index][0] == callee_path:
                            incoming_own_tu[callee_index] += 1
                    continue

                # A compiler-generated adapter with no source node can root its
                # callback only when another object actually references it.
                for reference_path in coff_references.get(symbol, set()):
                    normalized = str(reference_path).replace("\\", "/").lower()
                    if "/skullbonez_tests/" in normalized:
                        test_seeds.add(callee_index)
                    elif reference_path.stem.lower() != callee_path.stem.lower():
                        external_seeds.add(callee_index)

    # Concept: source scanning supplies same-TU edges. Cross-TU reachability
    # comes from decorated compiler symbols when object roots are available.
    for caller_path, occurrences in production_occurrences.items():
        caller_nodes: list[tuple[int, int, int]] = []
        if caller_path.suffix.lower() == ".cpp":
            brace_pairs = matching_pairs(masked_by_file[caller_path], "{", "}")
            for index in nodes_by_path.get(caller_path, []):
                _, candidate = nodes[index]
                body = _definition_body(masked_by_file[caller_path], candidate, brace_pairs)
                if body is not None:
                    caller_nodes.append((index, body[0], body[1]))

        for opening, qualified_call, simple_name, arity in occurrences:
            matches = [
                index
                for index in nodes_by_simple_name.get(simple_name, [])
                for _, candidate in (nodes[index],)
                if _call_matches(
                    qualified_call,
                    simple_name,
                    arity,
                    candidate,
                    required_arities[index],
                )
            ]
            if not matches:
                continue
            enclosing = [
                index
                for index, body_start, body_end in caller_nodes
                if body_start < opening < body_end
            ]
            if enclosing:
                caller = enclosing[0]
                same_tu_matches = [
                    index
                    for index in matches
                    if nodes[index][0] == caller_path
                ]
                if len(same_tu_matches) == 1:
                    callee = same_tu_matches[0]
                    edges[caller].add(callee)
                    incoming_own_tu[callee] += 1
                elif len(same_tu_matches) > 1:
                    for index in same_tu_matches:
                        ambiguous_external[index] += 1
                if same_tu_matches:
                    continue

            # Concept: decorated references are authoritative when emitted, but
            # inlining can erase a real cross-TU relocation. A unique lexical
            # match supplies that source edge; overloaded or receiver-ambiguous
            # calls remain explicitly unresolved.
            external_matches = [
                index
                for index in matches
                if nodes[index][0] != caller_path
            ]
            if len(external_matches) == 1:
                external_seeds.add(external_matches[0])
            elif len(external_matches) > 1:
                for index in external_matches:
                    ambiguous_external[index] += 1

    lexical_test_occurrences = (
        agentic_test_occurrences if object_roots else test_occurrences
    )
    for _, qualified_call, simple_name, arity in lexical_test_occurrences:
        matches = [
            index
            for index in nodes_by_simple_name.get(simple_name, [])
            for _, candidate in (nodes[index],)
            if _call_matches(
                qualified_call,
                simple_name,
                arity,
                candidate,
                required_arities[index],
            )
        ]
        if len(matches) == 1:
            test_seeds.add(matches[0])
        elif len(matches) > 1:
            for index in matches:
                ambiguous_external[index] += 1

    def closure(seeds: set[int]) -> set[int]:
        reached = set(seeds)
        pending = list(seeds)
        while pending:
            caller = pending.pop()
            for callee in edges[caller]:
                if callee not in reached:
                    reached.add(callee)
                    pending.append(callee)
        return reached

    production_reached = closure(external_seeds)
    test_reached = closure(test_seeds)

    rows: list[ReachabilityRow] = []
    for index, (definition_path, candidate) in nodes.items():
        if index not in reportable_indices:
            continue
        if index in production_reached:
            continue
        tests = 1 if index in test_reached else 0
        own_calls = incoming_own_tu[index]
        classification = _classification(own_calls, tests)
        rows.append(
            ReachabilityRow(
                file=candidate.file,
                line=candidate.line,
                name=candidate.qualified_name,
                signature=_definition_signature(masked_by_file[definition_path], candidate),
                required_arity=required_arities[index],
                total_arity=candidate.arity,
                own_tu_calls=own_calls,
                test_calls=tests,
                ambiguous_production_calls=ambiguous_external[index],
                symbol_mapping=(
                    compiler_mapping[index]
                    if object_roots
                    else "source-only"
                ),
                compiler_symbols=compiler_symbols[index],
                classification=classification,
            )
        )
    # Why: mutually exclusive preprocessor branches can contain the same
    # out-of-line definition twice in one source file. They are one signature
    # judgement, so merge their configuration alternatives into one row.
    merged: dict[tuple[str, str], ReachabilityRow] = {}
    for row in rows:
        prior = merged.get(row.key)
        if prior is None:
            merged[row.key] = row
            continue
        own_calls = max(prior.own_tu_calls, row.own_tu_calls)
        test_calls = max(prior.test_calls, row.test_calls)
        symbols = tuple(sorted(set(prior.compiler_symbols) | set(row.compiler_symbols)))
        mapping = (
            "exact"
            if len(symbols) == 1
            else "ambiguous"
            if symbols
            else "missing"
        )
        merged[row.key] = ReachabilityRow(
            file=row.file,
            line=min(prior.line, row.line),
            name=row.name,
            signature=row.signature,
            required_arity=row.required_arity,
            total_arity=row.total_arity,
            own_tu_calls=own_calls,
            test_calls=test_calls,
            ambiguous_production_calls=max(
                prior.ambiguous_production_calls,
                row.ambiguous_production_calls,
            ),
            symbol_mapping=mapping,
            compiler_symbols=symbols,
            classification=_classification(own_calls, test_calls),
        )
    return sorted(
        merged.values(),
        key=lambda row: (row.file.lower(), row.line, row.signature),
    )


def scan_repository(repo: Path, object_roots: list[Path] | None = None) -> list[ReachabilityRow]:
    production = _tracked_files(repo, ["SkullbonezSource"])
    tests = _tracked_files(repo, ["SkullbonezTests", "Agentic/Tests"])
    return scan_paths(repo, production, tests, object_roots)


def load_rulings(path: Path) -> dict[tuple[str, str], dict[str, str]]:
    payload = json.loads(path.read_text(encoding="utf-8", errors="strict"))
    raw_rulings = payload.get("rulings")
    if not isinstance(raw_rulings, list):
        raise ValueError("rulings must be an array")
    required = ("file", "signature", "owner", "disposition", "reason", "evidence")
    rulings: dict[tuple[str, str], dict[str, str]] = {}
    for index, raw in enumerate(raw_rulings):
        if not isinstance(raw, dict):
            raise ValueError(f"rulings[{index}] must be an object")
        values: dict[str, str] = {}
        for field in required:
            value = raw.get(field)
            if not isinstance(value, str) or not value.strip():
                raise ValueError(f"rulings[{index}].{field} must be a non-empty string")
            values[field] = value.strip()
        if values["disposition"] not in RULING_DISPOSITIONS:
            allowed = ", ".join(sorted(RULING_DISPOSITIONS))
            raise ValueError(f"rulings[{index}].disposition must be one of: {allowed}")
        plan = raw.get("plan", "")
        if not isinstance(plan, str):
            raise ValueError(f"rulings[{index}].plan must be a string")
        values["plan"] = plan.strip()
        if values["disposition"] == "repair-plan" and not values["plan"]:
            raise ValueError(f"rulings[{index}] repair-plan requires plan")
        key = (values["file"], values["signature"])
        if key in rulings:
            raise ValueError(f"duplicate ruling for {key[0]}: {key[1]}")
        rulings[key] = values
    return rulings


def apply_rulings(
    rows: list[ReachabilityRow],
    rulings: dict[tuple[str, str], dict[str, str]],
) -> tuple[list[dict[str, object]], list[str]]:
    output: list[dict[str, object]] = []
    diagnostics: list[str] = []
    current_keys = {row.key for row in rows}
    for row in rows:
        item = asdict(row)
        ruling = rulings.get(row.key)
        if ruling is None:
            item["ruling_status"] = "UNRULED"
            diagnostics.append(f"UNRULED {row.file}: {row.signature}")
        else:
            item["ruling_status"] = "RULED"
            item.update({f"ruling_{key}": value for key, value in ruling.items() if key not in {"file", "signature"}})
        output.append(item)
    for file_name, signature in sorted(set(rulings) - current_keys):
        diagnostics.append(f"STALE-RULING {file_name}: {signature}")
    return output, diagnostics


def validate_repair_plans(
    repo: Path,
    rulings: dict[tuple[str, str], dict[str, str]],
) -> list[str]:
    diagnostics: list[str] = []
    for (file_name, signature), ruling in rulings.items():
        if ruling["disposition"] != "repair-plan":
            continue
        plan_path = repo / ruling["plan"]
        if not plan_path.is_file():
            diagnostics.append(
                f"MISSING-REPAIR-PLAN {file_name}: {signature} -> {ruling['plan']}"
            )
    return diagnostics


def _write_fixture(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def _compile_fixture(
    compiler: Path,
    source: Path,
    object_path: Path,
    defines: tuple[str, ...] = (),
) -> None:
    object_path.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(
        [
            str(compiler),
            "/nologo",
            "/c",
            "/EHsc",
            "/std:c++20",
            *(f"/D{define}" for define in defines),
            f"/Fo{object_path}",
            str(source),
        ],
        cwd=source.parent,
        check=True,
        capture_output=True,
        text=True,
    )


def self_test() -> None:
    with tempfile.TemporaryDirectory(prefix="unreachable-symbols-") as temp:
        repo = Path(temp)
        source = repo / "SkullbonezSource"
        tests = repo / "SkullbonezTests"
        agentic_tests = repo / "Agentic" / "Tests"
        _write_fixture(
            source / "Api.h",
            """
#pragma once
int TestOnly(int value);
int Over(int value);
int Over(int left, int right);
int Conditional(int value);
int AutomationOnly(int value);
int Live(int value);
int WithDefault(int left, int right = 7);
int DefaultRoot(int seed);
int AgentOnly(int value);
struct Accessor {
    int value;
int& State();
    const int& State() const;
};
int& FreeState(Accessor& accessor);
const int& FreeState(const Accessor& accessor);
struct First {};
struct Second {};
int SameArity(First value);
int SameArity(Second value);
struct Lifecycle {
    Lifecycle();
    ~Lifecycle();
    void ConstructorTarget();
};
int Callback(int value);
template <int (*Function)(int)> __declspec(noinline) int Bind(int value) { return Function(value); }
int BindCallback(int value);
int DeadCallback(int value);
template <int (*Function)(int)> __declspec(noinline) int DeadBind(int value) { return Function(value); }
int DeadBindCallback(int value);
int InternalCollision(int value);
int LiteralRoot(const char* left, const char* right);
int UseLiteralRoot();
int VariadicRoot(const char* format, ...);
int UseVariadicRoot();
template <bool Enabled> int SpecializedRoot(int value);
int UseSpecializedRoot();
""",
        )
        _write_fixture(
            source / "Api.cpp",
            """
#include "Api.h"
static int Internal(int value) { return value + 1; }
int TestOnly(int value) { return value; }
int Over(int value) { return value; }
int Over(int left, int right) { return left + right; }
int Conditional(int value) { return value; }
int AutomationOnly(int value) { return value; }
int Live(int value) { return Internal(value); }
int WithDefault(int left, int right) { return left + right; }
int DefaultRoot(int seed) { return WithDefault(seed); }
int AgentOnly(int value) { return value; }
int& Accessor::State() { return value; }
const int& Accessor::State() const { return value; }
int& FreeState(Accessor& accessor) { return accessor.value; }
const int& FreeState(const Accessor& accessor) { return accessor.value; }
int SameArity(First value) { (void)value; return 1; }
int SameArity(Second value) { (void)value; return 2; }
Lifecycle::Lifecycle() { ConstructorTarget(); }
Lifecycle::~Lifecycle() {}
void Lifecycle::ConstructorTarget() {}
int Callback(int value) { return value; }
int BindCallback(int value) { return Bind<&Callback>(value); }
int DeadCallback(int value) { return value; }
int DeadBindCallback(int value) { return DeadBind<&DeadCallback>(value); }
namespace {
int InternalCollision(int value) { return value; }
}
int LiteralRoot(const char* left, const char* right) {
    return (left && right) ? 1 : 0;
}
int UseLiteralRoot() { return LiteralRoot("left", "right"); }
int VariadicRoot(const char* format, ...) { return format ? 1 : 0; }
int UseVariadicRoot() { return VariadicRoot("%d %d", 1, 2); }
template <bool Enabled> int SpecializedRoot(int value) {
    return Enabled ? value : -value;
}
int UseSpecializedRoot() {
    return SpecializedRoot<true>(1) + SpecializedRoot<false>(2);
}
""",
        )
        _write_fixture(
            source / "Caller.cpp",
            """
#include "Api.h"
int UseOverload() { return Over(1, 2); }
#if defined(_DEBUG)
int UseConditional() { return Conditional(3); }
#endif
#if defined(SKULLBONEZ_AUTOMATION_DIAGNOSTICS)
int UseAutomationOnly() { return AutomationOnly(6); }
#endif
int UseLive() { return Live(4); }
int UseLiteralCall() { return UseLiteralRoot(); }
int UseVariadicCall() { return UseVariadicRoot(); }
int UseLifecycle() { Lifecycle value = Lifecycle(); return 0; }
int UseCallback() { return BindCallback(5); }
int UseSpecializedCall() { return UseSpecializedRoot(); }
""",
        )
        _write_fixture(
            tests / "TestApi.cpp",
            """
#include "../SkullbonezSource/Api.h"
int TestUse() { return TestOnly(5); }
""",
        )
        _write_fixture(
            agentic_tests / "AgenticApiTest.cpp",
            """
#include "../../SkullbonezSource/Api.h"
int AgenticUse() { return AgentOnly(6); }
""",
        )
        production = sorted(source.glob("*"))
        test_files = sorted(tests.glob("*")) + sorted(agentic_tests.glob("*"))
        api_text = (source / "Api.cpp").read_text(encoding="utf-8")
        api_masked = mask_cpp(api_text)
        api_candidates, _ = scan_file(
            source / "Api.cpp",
            repo,
            text=api_text,
            masked=api_masked,
        )
        assert any(
            candidate.qualified_name == "Lifecycle::Lifecycle"
            for candidate in _lifecycle_candidates(
                source / "Api.cpp",
                repo,
                api_masked,
            )
        )
        caller_text = (source / "Caller.cpp").read_text(encoding="utf-8")
        assert any(
            occurrence[2] == "Lifecycle"
            for occurrence in _call_occurrences(
                mask_cpp(caller_text, preserve_literal_argument=True)
            )
        )
        source_rows = scan_paths(repo, production, test_files)
        default_source_row = next(row for row in source_rows if row.name == "WithDefault")
        assert default_source_row.required_arity == 1
        assert default_source_row.total_arity == 2
        assert default_source_row.classification == "own-tu-only"

        compiler = _find_cl()
        debug_objects = repo / "Debug"
        profile_objects = repo / "Profile"
        automation_objects = repo / "Automation"
        _compile_fixture(compiler, source / "Api.cpp", debug_objects / "Api.obj")
        _compile_fixture(
            compiler,
            source / "Caller.cpp",
            debug_objects / "Caller.obj",
            ("_DEBUG",),
        )
        _compile_fixture(compiler, source / "Api.cpp", profile_objects / "Api.obj")
        _compile_fixture(compiler, source / "Caller.cpp", profile_objects / "Caller.obj")
        _compile_fixture(compiler, source / "Api.cpp", automation_objects / "Api.obj")
        _compile_fixture(
            compiler,
            source / "Caller.cpp",
            automation_objects / "Caller.obj",
            ("SKULLBONEZ_AUTOMATION_DIAGNOSTICS",),
        )
        _compile_fixture(
            compiler,
            tests / "TestApi.cpp",
            profile_objects / "SKULLBONEZ_TESTS" / "TestApi.obj",
        )

        rows = scan_paths(
            repo,
            production,
            test_files,
            [automation_objects, debug_objects, profile_objects],
        )
        test_row = next(row for row in rows if row.name == "TestOnly")
        agentic_row = next(row for row in rows if row.name == "AgentOnly")
        overload_row = next(row for row in rows if row.name == "Over")
        default_row = next(row for row in rows if row.name == "WithDefault")
        state_rows = [row for row in rows if row.name == "Accessor::State"]
        free_state_rows = [row for row in rows if row.name == "FreeState"]
        same_arity_rows = [row for row in rows if row.name == "SameArity"]
        dead_callback_row = next(row for row in rows if row.name == "DeadCallback")
        assert test_row.classification == "test-only"
        assert test_row.symbol_mapping == "exact"
        assert agentic_row.classification == "test-only"
        assert agentic_row.symbol_mapping == "exact"
        assert overload_row.total_arity == 1 and overload_row.classification == "no-reference"
        assert default_row.required_arity == 1 and default_row.total_arity == 2
        assert default_row.classification == "own-tu-only"
        assert default_row.symbol_mapping == "exact"
        assert len(state_rows) == 2
        assert all(row.symbol_mapping == "exact" for row in state_rows)
        assert len(free_state_rows) == 2
        assert all(row.symbol_mapping == "exact" for row in free_state_rows)
        assert len(same_arity_rows) == 2, [
            (row.name, row.signature, row.symbol_mapping) for row in rows
        ]
        assert all(row.symbol_mapping == "exact" for row in same_arity_rows), [
            (row.signature, row.symbol_mapping, row.compiler_symbols)
            for row in same_arity_rows
        ]
        assert not any(row.name == "Callback" for row in rows), [
            (
                row.name,
                row.classification,
                row.symbol_mapping,
                row.compiler_symbols,
                row.own_tu_calls,
            )
            for row in rows
            if "Callback" in row.name or row.name in {"Bind", "DeadBind"}
        ]
        assert dead_callback_row.classification == "own-tu-only"
        assert dead_callback_row.symbol_mapping == "exact"
        assert not any(row.name == "InternalCollision" for row in rows)
        assert not any(row.name == "LiteralRoot" for row in rows)
        assert not any(row.name == "VariadicRoot" for row in rows)
        assert not any(row.name == "SpecializedRoot" for row in rows)
        assert not any(
            row.name == "Lifecycle::ConstructorTarget" for row in rows
        ), [
            (
                row.name,
                row.classification,
                row.symbol_mapping,
                row.compiler_symbols,
            )
            for row in rows
        ]
        assert not any(row.name == "Internal" for row in rows)
        assert not any(row.name == "Conditional" for row in rows)
        assert not any(row.name == "AutomationOnly" for row in rows)
        assert not any(row.name == "Live" for row in rows)
        assert not any(row.total_arity == 2 and row.name == "Over" for row in rows)

        try:
            scan_paths(repo, production, test_files, [debug_objects])
        except OSError as error:
            assert "requires distinct Automation, Debug, and Profile" in str(error)
        else:
            raise AssertionError("single-configuration COFF scan must fail closed")

        try:
            scan_paths(
                repo,
                production,
                test_files,
                [automation_objects, debug_objects, debug_objects],
            )
        except OSError as error:
            assert "requires distinct Automation, Debug, and Profile" in str(error)
        else:
            raise AssertionError("duplicate configuration roots must fail closed")

        api_path = source / "Api.cpp"
        api_stat = api_path.stat()
        newest_object = max(
            path.stat().st_mtime_ns
            for root in (automation_objects, debug_objects, profile_objects)
            for path in root.rglob("*.obj")
        )
        os.utime(
            api_path,
            ns=(api_stat.st_atime_ns, newest_object + 1_000_000_000),
        )
        try:
            scan_paths(
                repo,
                production,
                test_files,
                [automation_objects, debug_objects, profile_objects],
            )
        except OSError as error:
            assert "stale current-source objects" in str(error)
        else:
            raise AssertionError("stale COFF input must fail closed")
        os.utime(api_path, ns=(api_stat.st_atime_ns, api_stat.st_mtime_ns))

        ruled = {
            row.key: {
                "owner": "fixture",
                "disposition": "retain-owner",
                "reason": "fixture reason",
                "evidence": "fixture evidence",
                "plan": "",
            }
            for row in rows
        }
        _, diagnostics = apply_rulings(rows, ruled)
        assert not diagnostics
        _write_fixture(
            source / "Api.h",
            """
#pragma once
int TestOnly(int value);
int MovedOver(int value);
int Over(int left, int right);
int Conditional(int value);
int Live(int value);
int WithDefault(int left, int right = 7);
int DefaultRoot(int seed);
int AgentOnly(int value);
""",
        )
        _write_fixture(
            source / "Api.cpp",
            """
#include "Api.h"
static int Internal(int value) { return value + 1; }
int TestOnly(int value) { return value; }
int MovedOver(int value) { return value; }
int Over(int left, int right) { return left + right; }
int Conditional(int value) { return value; }
int Live(int value) { return Internal(value); }
int WithDefault(int left, int right) { return left + right; }
int DefaultRoot(int seed) { return WithDefault(seed); }
int AgentOnly(int value) { return value; }
""",
        )
        moved_rows = scan_paths(repo, production, test_files)
        _, diagnostics = apply_rulings(moved_rows, ruled)
        assert any(item.startswith("STALE-RULING") and "Over" in item for item in diagnostics)
        assert any(item.startswith("UNRULED") and "MovedOver" in item for item in diagnostics)

        _, unruled = apply_rulings(rows, {})
        assert any(item.startswith("UNRULED") for item in unruled)
    print("inventory_unreachable_symbols self-test: PASS")


def _render_text(rows: list[dict[str, object]], diagnostics: list[str]) -> str:
    counts: dict[str, int] = {}
    for row in rows:
        classification = str(row["classification"])
        counts[classification] = counts.get(classification, 0) + 1
    lines = [
        "Unreachable symbol inventory",
        f"rows: {len(rows)}",
        *[f"{name}: {count}" for name, count in sorted(counts.items())],
        f"blocking diagnostics: {len(diagnostics)}",
    ]
    for row in rows:
        lines.append(
            f"{row['classification']} {row['file']}:{row['line']} "
            f"{row['signature']} [{row['ruling_status']}]"
        )
    if diagnostics:
        lines.append("Diagnostics:")
        lines.extend(f"  {diagnostic}" for diagnostic in diagnostics)
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=Path.cwd())
    parser.add_argument("--rulings", type=Path, default=DEFAULT_RULINGS_PATH)
    parser.add_argument(
        "--no-rulings",
        action="store_true",
        help="Report current rows as unruled without loading the ruling file",
    )
    parser.add_argument(
        "--object-root",
        action="append",
        type=Path,
        help="Compiler-object root; defaults to Automation, Debug, and Profile under the repository",
    )
    parser.add_argument("--format", choices=("text", "json"), default="text")
    parser.add_argument("--strict", action="store_true")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return 0

    repo = args.repo.resolve()
    ruling_path = args.rulings if args.rulings.is_absolute() else repo / args.rulings
    object_roots = args.object_root or [Path("Automation"), Path("Debug"), Path("Profile")]
    object_roots = [path if path.is_absolute() else repo / path for path in object_roots]
    try:
        rows = scan_repository(repo, object_roots)
        rulings = {} if args.no_rulings else load_rulings(ruling_path)
        output, diagnostics = apply_rulings(rows, rulings)
        diagnostics.extend(validate_repair_plans(repo, rulings))
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 2

    if args.format == "json":
        print(json.dumps({"rows": output, "diagnostics": diagnostics}, indent=2))
    else:
        print(_render_text(output, diagnostics))
    if args.strict and diagnostics:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
