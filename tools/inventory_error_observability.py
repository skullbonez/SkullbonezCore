#!/usr/bin/env python3
"""
File: inventory_error_observability.py
Purpose:
  Inventory the tracked production error-observability surface and reconcile
  every discovered site with an exact current-source owner ruling.

Summary:
  E0 needs a repeatable qualitative inventory, not a frozen count. This tool
  walks every tracked C++ file under SkullbonezSource, recognizes the bounded
  lexical shapes that can create, hide, or present an error, and fingerprints
  the exact source slice behind each row. Strict mode fails when a row is new,
  edited, deleted, unclassified, or backed by an inadequate description that
  has no named repair phase. Ignored CRT file outcomes are independent rows so
  a write to an otherwise valid sink cannot silently lose durability evidence.

  Retained executables are part of the same surface because a missing imported
  runtime fails before engine code can report anything. The artifact pass
  detects known non-system imports in tracked retained executables and reports
  a fingerprint-bound E5 row when the imported runtime is absent beside the
  executable.

Glossary:
  Site class: Lexical shape that exposed a candidate, such as an SbResult
    failure construction, raw stderr write, status return, or counter-only loss.
  Disposition: Owner judgement about the site's current semantic category; it
    is never an allowance or a count budget.
  Source fingerprint: SHA-256 of the exact normalized source slice (or retained
    executable plus import identity) that made the row discoverable.
  Repair phase: E1-E5 phase that owns replacing an inadequate description or
    non-central reporting path. A repair remains current evidence, not approval.

Invariants:
  - Repository discovery is driven by git ls-files, so ignored or untracked
    scratch cannot silently change production coverage.
  - A ruling identity includes file, location, site class, normalized operation,
    and source fingerprint; both edited code and deleted sites fail closed.
  - Strict mode accepts no unruled or stale row and no unowned repair.
  - Row totals are measurements only and are never compared with a ceiling.
  - The unreviewed-template mode cannot pass strict validation until an owner
    adjudicates every generated row; flipping review-state fields while keeping
    suggestion semantics is rejected.

Related:
  - Agentic/Reference/error-observability-reference.md
  - tools/cpp_source_scan.py
  - tools/inventory_function_complexity.py
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import struct
import subprocess
import sys
from bisect import bisect_right
from collections import Counter
from dataclasses import asdict, dataclass
from pathlib import Path, PurePosixPath
from tempfile import TemporaryDirectory
from typing import Iterable, Sequence

sys.path.insert(0, str(Path(__file__).resolve().parent))

from cpp_source_scan import mask_cpp  # noqa: E402


DEFAULT_RULINGS_PATH = Path("tools/error_observability_rulings.json")
REFERENCE_PATH = "Agentic/Reference/error-observability-reference.md"
REPAIR_PLAN = "Agentic/Plans/TODO/all-build-sb-error-observability.md"
SOURCE_ROOT = "SkullbonezSource"
SOURCE_SUFFIXES = {".cpp", ".h", ".hpp", ".inl"}
RETAINED_ARTIFACT_ROOT = "Agentic/Plans/Artifacts"

DISPOSITIONS = {
    "sb-warning",
    "recoverable-sb-error",
    "fatal-sb-error",
    "successful-fallback-value-state",
    "test-only-deliberate-failure",
    "runtime-assertion",
    "repair",
}
DESCRIPTION_CLASSIFICATIONS = {
    "actionable",
    "generic",
    "code-only",
    "expression-only",
    "context-free",
    "missing",
    "not-applicable",
}
INADEQUATE_DESCRIPTIONS = {"generic", "code-only", "expression-only", "context-free", "missing"}
REPAIR_PHASES = {"E1", "E2", "E3", "E4", "E5"}
REVIEW_STATUSES = {"unreviewed", "ratified"}
ADJUDICATION_STATUSES = {"unreviewed", "owner-reviewed"}
SHA256_RE = re.compile(r"[0-9a-f]{64}")

# Concept: These are discovery signals, not policy budgets. A signal only asks
# an owner to classify one exact current site; it never proves the site is an
# error by itself. The ruling records that qualitative answer.
ERROR_NAME_RE = re.compile(r"(?:Fail(?:ure)?|Error|Fatal|Warning)", re.IGNORECASE)
RECOVERY_NAME_RE = re.compile(
    r"(?:Fallback|Drop(?:ped)?|Overflow|Reject(?:ed)?|Truncat(?:e|ed|ion)|Ignored?|Silent|Degrad(?:e|ed)|Disable(?:d)?)",
    re.IGNORECASE,
)
STATUS_PRESENTATION_NAME_RE = re.compile(r"(?:Status|Message|Notification|Banner|Dialog)", re.IGNORECASE)
ERROR_TEXT_RE = re.compile(
    r"(?:fail(?:ed|ure)?|error|fatal|abort|exception|invalid|unable|unavailable|missing|overflow|exhausted|"
    r"out[ _-]of[ _-]range|cannot|could[ _-]not|denied|unsupported|corrupt|mismatch|violation|lost|dropped|"
    r"reject(?:ed|ion)?|truncat(?:e|ed|ion)|not[ _-]found|did[ _-]not|unwritable|skipped|stale|without|"
    r"not[ _-](?:in|set|reflected|available)|upload[ _-]drop|resource[ _-]set[ _-]with[ _-]uniform|dred|"
    r"device[ _-]removed|page[ _-]fault)",
    re.IGNORECASE,
)
STRONG_CONSTRAINT_TEXT_RE = re.compile(
    r"(?:must|requires?|cannot|could not|invalid|missing|unavailable|unsupported|exceed|exhaust|out of range|"
    r"mismatch|not found|non[- ]?finite|empty|denied|corrupt|overflow|illegal)",
    re.IGNORECASE,
)
PLATFORM_RESULT_TEXT_RE = re.compile(r"(?:HRESULT|Win32|errno|error|result|status|code)", re.IGNORECASE)
GENERIC_WORDS = {
    "abort",
    "assert",
    "assertion",
    "error",
    "failed",
    "failure",
    "fatal",
    "invalid",
    "invariant",
    "operation",
    "unknown",
    "unavailable",
    "warning",
}
PREDICATE_PREFIXES = ("is", "has", "can", "should", "needs", "supports", "enabled", "get")
PROBE_NAME_RE = re.compile(r"(?:FailAutomation|Probe|Test|Harness|MutationControl|NegativeFixture)", re.IGNORECASE)

CALL_TOKEN_RE = re.compile(
    r"(?P<callee>[A-Za-z_]\w*(?:(?:::|->|\.)[A-Za-z_]\w*)*)\s*(?P<open>\()"
)
CONTROL_NAMES = {"if", "for", "while", "switch", "catch", "sizeof", "alignof", "decltype"}
STDERR_WRITERS = {"fprintf", "vfprintf", "fputs", "fputc", "fwrite", "printf_s", "fprintf_s"}
EVENT_WRITERS = {"WriteEvent", "WriteEventf"}
DEBUGGER_SINKS = {"OutputDebugString", "OutputDebugStringA", "OutputDebugStringW", "DebugBreak", "__debugbreak"}
TERMINATORS = {
    "abort",
    "terminate",
    "TerminateProcess",
    "ExitProcess",
    "_exit",
    "quick_exit",
    "RaiseFailFastException",
}
FORMATTERS = {"snprintf", "vsnprintf", "sprintf_s", "vsprintf_s", "FormatMessage", "FormatMessageA", "FormatMessageW"}
CRT_IO_OUTCOME_NAMES = {
    "fopen",
    "fopen_s",
    "_wfopen",
    "_wfopen_s",
    "fprintf",
    "fprintf_s",
    "vfprintf",
    "fputs",
    "fputc",
    "fwrite",
    "fflush",
    "fclose",
}

COUNTER_MUTATION_RE = re.compile(
    r"(?:(?P<prefix_mutation>\+\+)\s*(?P<prefix_name>[A-Za-z_]\w*(?:(?:\.|->)[A-Za-z_]\w*)*)|"
    r"(?P<post_name>[A-Za-z_]\w*(?:(?:\.|->)[A-Za-z_]\w*)*)\s*"
    r"(?P<post_mutation>\+\+|\+=\s*[^;]+|=\s*(?:true|false|-?\d+)|\.fetch_add\s*\())",
    re.IGNORECASE,
)
STATUS_RETURN_RE = re.compile(r"\breturn\s+(?P<value>false|nullptr|std::nullopt|nullopt|-1)\s*;")
FUNCTION_OPEN_RE = re.compile(
    r"(?P<name>[~A-Za-z_]\w*(?:::\w+)*)\s*\([^;{}]*\)\s*"
    r"(?:const\s*)?(?:noexcept\s*)?(?:override\s*)?(?:final\s*)?\{",
    re.MULTILINE,
)

CPP_STRING_RE = re.compile(
    r"(?P<prefix>u8|u|U|L)?(?:"
    r"R\"(?P<delimiter>[^ ()\\\t\r\n]{0,16})\((?P<raw_body>.*?)\)(?P=delimiter)\"|"
    r"\"(?P<body>(?:\\.|[^\"\\])*)\""
    r")",
    re.DOTALL,
)
PRINTF_RE = re.compile(r"%(?:[-+ #0]*\d*(?:\.\d+)?(?:hh|h|ll|l|j|z|t|L)?[A-Za-z%])")

# E0 records the known package-owned runtimes whose absence makes a retained
# executable non-runnable before WinMain. E5 owns a complete PE import resolver;
# this bounded list deliberately does not pretend to be that future bundler.
KNOWN_NON_SYSTEM_IMPORTS = {"winpixeventruntime.dll", "dxcompiler.dll", "dxil.dll"}


@dataclass(frozen=True, order=True)
class Finding:
    path: str
    line: int
    column: int
    site_class: str
    operation: str
    source_fingerprint: str
    description_classification: str
    description: str
    evidence: str

    @property
    def identity(self) -> tuple[object, ...]:
        return (
            self.path,
            self.line,
            self.column,
            self.site_class,
            self.operation,
            self.source_fingerprint,
        )


@dataclass
class ScanResult:
    findings: list[Finding]
    source_files_scanned: int
    source_manifest_sha256: str
    diagnostics: list[str]


@dataclass(frozen=True)
class RulingIssue:
    identity: str
    message: str


@dataclass
class Evaluation:
    issues: list[RulingIssue]
    unruled: list[Finding]
    stale: list[dict[str, object]]


@dataclass(frozen=True)
class FunctionExtent:
    start: int
    end: int
    name: str


def _normalize_path(path: Path | str) -> str:
    return str(path).replace("\\", "/")


def _sha256_bytes(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def _source_fingerprint(fragment: str) -> str:
    normalized = fragment.replace("\r\n", "\n").strip()
    return _sha256_bytes(normalized.encode("utf-8"))


def _line_column(text: str, offset: int) -> tuple[int, int]:
    line = text.count("\n", 0, offset) + 1
    line_start = text.rfind("\n", 0, offset) + 1
    return line, offset - line_start + 1


def _compact(text: str, limit: int = 240) -> str:
    compact = " ".join(text.replace("\r", " ").replace("\n", " ").split())
    return compact if len(compact) <= limit else compact[: limit - 3] + "..."


def _normalize_operation(masked_fragment: str, fallback: str) -> str:
    operation = " ".join(masked_fragment.split())
    return operation if operation else fallback


def _find_matching(masked: str, opening: int, opening_char: str = "(", closing_char: str = ")") -> int | None:
    depth = 0
    for index in range(opening, len(masked)):
        char = masked[index]
        if char == opening_char:
            depth += 1
        elif char == closing_char:
            depth -= 1
            if depth == 0:
                return index
    return None


def _next_code(masked: str, start: int) -> int:
    while start < len(masked) and masked[start].isspace():
        start += 1
    return start


def _previous_boundary(masked: str, start: int) -> int:
    index = start - 1
    while index >= 0 and masked[index] not in ";{}\n":
        index -= 1
    return index + 1


def _statement_bounds(masked: str, offset: int) -> tuple[int, int]:
    start = _previous_boundary(masked, offset)
    end = offset
    nesting = 0
    while end < len(masked):
        char = masked[end]
        if char in "([{":
            nesting += 1
        elif char in ")]}" and nesting > 0:
            nesting -= 1
        if char == ";" and nesting == 0:
            return start, end + 1
        if char == "\n" and nesting == 0 and end > offset:
            return start, end
        end += 1
    return start, len(masked)


def _call_result_is_ignored(masked: str, callee_start: int, closing: int) -> bool:
    statement_start, statement_end = _statement_bounds(masked, callee_start)
    prefix = masked[statement_start:callee_start].strip()
    suffix = masked[closing + 1 : statement_end].strip()
    if not prefix and suffix == ";":
        return True
    explicit_discard = re.fullmatch(r"(?:\(\s*void\s*\)|static_cast\s*<\s*void\s*>\s*\()", prefix)
    return explicit_discard is not None and suffix in {";", ");"}


def _split_arguments(source: str, masked: str, opening: int, closing: int) -> list[str]:
    arguments: list[str] = []
    start = opening + 1
    depth = 0
    for index in range(opening + 1, closing):
        char = masked[index]
        if char in "([{":
            depth += 1
        elif char in ")]}" and depth > 0:
            depth -= 1
        elif char == "," and depth == 0:
            arguments.append(source[start:index].strip())
            start = index + 1
    tail = source[start:closing].strip()
    if tail or arguments:
        arguments.append(tail)
    return arguments


def _decode_cpp_string(body: str) -> str:
    replacements = {"n": " ", "r": " ", "t": " ", "\\": "\\", '"': '"', "0": " "}

    def replace(match: re.Match[str]) -> str:
        return replacements.get(match.group(1), match.group(0))

    return re.sub(r"\\(.)", replace, body, flags=re.DOTALL)


def _strings(fragment: str) -> list[str]:
    values: list[str] = []
    for match in CPP_STRING_RE.finditer(fragment):
        raw = match.group("raw_body")
        body = raw if raw is not None else _decode_cpp_string(match.group("body") or "")
        values.append(_compact(body, 512))
    return values


def _description_for_call(last_name: str, arguments: Sequence[str], site_class: str) -> str:
    if site_class == "runtime-assertion":
        return ""
    if site_class == "static-assertion":
        return " ".join(_strings(arguments[1])) if len(arguments) > 1 else ""
    if last_name in {"Failure", "FailureV", "SB_FATAL", "SbFatal"}:
        return " ".join(_strings(arguments[1])) if len(arguments) > 1 else ""
    if site_class == "dialog-sink":
        selected = arguments[1:3] if len(arguments) >= 3 else arguments
        return " ".join(value for argument in selected for value in _strings(argument))
    return " ".join(value for argument in arguments for value in _strings(argument))


def _classify_description(description: str, site_class: str) -> str:
    if site_class == "runtime-assertion":
        return "expression-only"
    if site_class == "bundle-import-mismatch":
        return "not-applicable"
    text = _compact(description, 1024).strip()
    if not text:
        return "missing"
    without_formats = PRINTF_RE.sub(" ", text)
    without_formats = re.sub(r"\b0x[0-9A-Fa-f]*\b", " ", without_formats)
    words = re.findall(r"[A-Za-z][A-Za-z0-9_-]*", without_formats.replace("_", " "))
    if not words:
        return "code-only"
    lowered = [word.lower() for word in words]
    if len(lowered) <= 3 and all(word in GENERIC_WORDS for word in lowered):
        return "generic"
    has_platform_result = (
        PLATFORM_RESULT_TEXT_RE.search(without_formats) is not None
        and PRINTF_RE.search(text) is not None
        and ERROR_TEXT_RE.search(without_formats) is not None
    )
    if len(lowered) < 4 or (
        STRONG_CONSTRAINT_TEXT_RE.search(without_formats) is None and not has_platform_result
    ):
        return "context-free"
    return "actionable"


def _looks_like_declaration_or_definition(masked: str, callee_start: int, closing: int, last_name: str) -> str:
    prefix_start = _previous_boundary(masked, callee_start)
    prefix = masked[prefix_start:callee_start].strip()
    if re.search(r"\breturn\b", prefix) or any(token in prefix for token in ("=", ".", "->", "!", "?")):
        return "call"
    suffix_end = min(len(masked), closing + 160)
    suffix = masked[closing + 1 : suffix_end]
    marker_match = re.search(r"[;{]", suffix)
    marker = marker_match.group(0) if marker_match else ""
    del last_name
    declaration_prefix = re.fullmatch(
        r"(?:\[\[[^\]]+\]\]\s*)?"
        r"(?:[A-Za-z_]\w*(?:::\w+)*(?:\s*<[^;{}()]*>)?\s*[&*]?\s*)+",
        prefix,
    )
    if declaration_prefix and marker == "{":
        return "definition"
    if declaration_prefix and marker == ";":
        return "declaration"
    return "call"


def _brace_pairs(masked: str) -> dict[int, int]:
    stack: list[int] = []
    pairs: dict[int, int] = {}
    for index, char in enumerate(masked):
        if char == "{":
            stack.append(index)
        elif char == "}" and stack:
            pairs[stack.pop()] = index
    return pairs


def _function_extents(masked: str) -> list[FunctionExtent]:
    brace_pairs = _brace_pairs(masked)
    extents: list[FunctionExtent] = []
    for match in FUNCTION_OPEN_RE.finditer(masked):
        name = match.group("name").split("::")[-1]
        if name in CONTROL_NAMES:
            continue
        opening = match.end() - 1
        closing = brace_pairs.get(opening)
        if closing is not None:
            extents.append(FunctionExtent(match.start(), closing + 1, match.group("name")))
    return extents


def _enclosing_function(extents: Sequence[FunctionExtent], offset: int) -> str:
    containing = [extent for extent in extents if extent.start <= offset < extent.end]
    if not containing:
        return "<unknown>"
    return min(containing, key=lambda extent: extent.end - extent.start).name


def _path_owner(path: str) -> str:
    parts = PurePosixPath(path).parts
    if len(parts) < 2 or parts[0] != SOURCE_ROOT:
        return "Validation Tooling"
    if parts[1] == "Runtime" and len(parts) >= 3:
        return f"Runtime/{parts[2]}"
    return parts[1]


def _repair_phase_for_path(path: str, site_class: str) -> str:
    """Return a conservative template hint; reviewed rulings may override it."""
    if site_class == "bundle-import-mismatch":
        return "E5"
    central_sink_path = path in {"SkullbonezSource/Core/Log.cpp", "SkullbonezSource/Core/FatalError.cpp"}
    if central_sink_path and site_class in {
        "pre-entry-fatal",
        "raw-stderr",
        "event-sink",
        "debugger-sink",
        "ignored-crt-io-outcome",
    }:
        return "E1"
    if site_class == "pre-entry-fatal" and path == "SkullbonezSource/Runtime/Startup/StartupCrashLogging.cpp":
        return "E3"
    return "E4"


def _operation_subject(operation: str) -> str:
    operation_body = operation.split(":", 1)[-1].strip()
    subject_end = len(operation_body)
    for marker in ("(", "="):
        marker_index = operation_body.find(marker)
        if marker_index >= 0:
            subject_end = min(subject_end, marker_index)
    return operation_body[:subject_end].strip()


def _operation_name(operation: str) -> str:
    return re.split(r"::|->|\.", _operation_subject(operation))[-1]


def _is_probe(path: str, operation: str) -> bool:
    # Invariant: argument text is never ownership evidence.  A normal event can
    # mention a test/probe value without becoming a deliberate-failure origin.
    subject = _operation_subject(operation)
    leaf_name = PurePosixPath(path).name
    return PROBE_NAME_RE.search(subject) is not None or PROBE_NAME_RE.search(leaf_name) is not None


def _is_value_query(name: str) -> bool:
    def semantic_prefix(prefix: str) -> bool:
        if name.lower() == prefix:
            return True
        return (
            name.lower().startswith(prefix)
            and len(name) > len(prefix)
            and (name[len(prefix)].isupper() or name[len(prefix)] == "_")
        )

    lowered = name.lower()
    if name == "FAILED" or lowered.startswith("validate"):
        return False
    if lowered in {"failed", "ferror", "commdlgextendederror", "shortwriteerror", "lasterror"}:
        return True
    if lowered.endswith("failed") and not lowered.startswith(("mark", "set", "record", "report", "request", "fail")):
        return True
    if lowered.startswith(("error", "failure")) and lowered.endswith(("message", "owner", "reason", "code", "name", "count", "status")):
        return True
    return any(
        semantic_prefix(prefix)
        for prefix in (*PREDICATE_PREFIXES, "clear", "reset", "name", "describe", "format", "current")
    )


def _make_finding(
    path: str,
    source: str,
    masked: str,
    start: int,
    end: int,
    site_class: str,
    operation: str,
    description: str,
) -> Finding:
    fragment = source[start:end]
    line, column = _line_column(source, start)
    return Finding(
        path=path,
        line=line,
        column=column,
        site_class=site_class,
        operation=_compact(operation, 320),
        source_fingerprint=_source_fingerprint(fragment),
        description_classification=_classify_description(description, site_class),
        description=_compact(description, 512),
        evidence=_compact(fragment),
    )


def scan_text(path: str, source: str) -> list[Finding]:
    """Return deterministic bounded lexical findings for one C++ source file."""
    masked = mask_cpp(source)
    function_extents = _function_extents(masked)
    findings: dict[tuple[object, ...], Finding] = {}
    occupied_call_spans: list[tuple[int, int]] = []

    def add(finding: Finding) -> None:
        findings[finding.identity] = finding

    for match in CALL_TOKEN_RE.finditer(masked):
        callee = match.group("callee")
        last_name = re.split(r"::|->|\.", callee)[-1]
        if last_name in CONTROL_NAMES:
            continue
        eligible_name = (
            last_name in {"Failure", "FailureV", "SB_FATAL", "SbFatal", "assert", "static_assert"}
            or last_name in EVENT_WRITERS
            or last_name.startswith("MessageBox")
            or last_name in DEBUGGER_SINKS
            or last_name in TERMINATORS
            or last_name in STDERR_WRITERS
            or ERROR_NAME_RE.search(last_name) is not None
            or RECOVERY_NAME_RE.search(last_name) is not None
            or last_name in FORMATTERS
            or last_name in CRT_IO_OUTCOME_NAMES
            or STATUS_PRESENTATION_NAME_RE.search(last_name) is not None
        )
        if not eligible_name:
            continue
        opening = match.start("open")
        closing = _find_matching(masked, opening)
        if closing is None:
            continue
        role = _looks_like_declaration_or_definition(masked, match.start("callee"), closing, last_name)
        if role == "declaration":
            continue
        arguments = _split_arguments(source, masked, opening, closing)
        invocation_mask = masked[match.start("callee") : closing + 1]
        operation = f"{role}:{_normalize_operation(invocation_mask, callee)}"
        site_class = ""

        if last_name == "Failure":
            if role == "definition" and "SbDiagnosticStore::Failure" in callee:
                site_class = "result-construction-owner"
            elif role == "definition" or ("." not in callee and "->" not in callee):
                site_class = "error-wrapper"
            else:
                site_class = "sb-result-failure"
        elif last_name == "FailureV":
            site_class = "result-construction-owner"
        elif last_name == "SB_FATAL":
            site_class = "sb-fatal"
        elif last_name == "SbFatal":
            site_class = "fatal-owner" if role == "definition" else "sb-fatal"
        elif last_name == "assert":
            site_class = "runtime-assertion"
        elif last_name == "static_assert":
            site_class = "static-assertion"
        elif last_name in EVENT_WRITERS:
            site_class = "event-sink"
        elif last_name.startswith("MessageBox"):
            site_class = "dialog-sink"
        elif last_name in DEBUGGER_SINKS:
            site_class = "debugger-sink"
        elif last_name in TERMINATORS:
            site_class = "pre-entry-fatal"
        elif last_name in STDERR_WRITERS and re.search(r"\bstderr\b", invocation_mask):
            site_class = "raw-stderr"
        elif ERROR_NAME_RE.search(last_name):
            site_class = "error-wrapper"
        elif RECOVERY_NAME_RE.search(last_name):
            site_class = "recovery-operation"
        else:
            description = _description_for_call(last_name, arguments, "message-template")
            destination = arguments[0] if arguments else ""
            if last_name in FORMATTERS and (
                ERROR_NAME_RE.search(destination) is not None
                or STATUS_PRESENTATION_NAME_RE.search(destination) is not None
                or ERROR_TEXT_RE.search(description) is not None
            ):
                site_class = "message-template"
            elif STATUS_PRESENTATION_NAME_RE.search(last_name) and ERROR_TEXT_RE.search(description):
                site_class = "status-presentation"

        discovered = False
        if site_class:
            description = _description_for_call(last_name, arguments, site_class)
            finding = _make_finding(
                path,
                source,
                masked,
                match.start("callee"),
                closing + 1,
                site_class,
                operation,
                description,
            )
            add(finding)
            discovered = True

        if last_name in CRT_IO_OUTCOME_NAMES and _call_result_is_ignored(masked, match.start("callee"), closing):
            # Hazard: these CRT calls report open/flush/close failure only in
            # their return value. Discarding it can erase the last durable
            # evidence that a diagnostic or artifact was not persisted.
            description = " ".join(value for argument in arguments for value in _strings(argument))
            add(
                _make_finding(
                    path,
                    source,
                    masked,
                    match.start("callee"),
                    closing + 1,
                    "ignored-crt-io-outcome",
                    f"ignored-outcome:{operation}",
                    description,
                )
            )
            discovered = True

        if discovered:
            occupied_call_spans.append((match.start("callee"), closing + 1))

    # Concept: A counter or sticky flag can be the only surviving evidence that
    # data was dropped. Reporting the mutation site lets E4 decide whether it is
    # a Debug warning, an SB error, or an ordinary value-state statistic.
    for match in COUNTER_MUTATION_RE.finditer(masked):
        name = match.group("prefix_name") or match.group("post_name")
        mutation = match.group("prefix_mutation") or match.group("post_mutation")
        assert name is not None and mutation is not None
        if ERROR_NAME_RE.search(name) is None and RECOVERY_NAME_RE.search(name) is None:
            continue
        start, end = _statement_bounds(masked, match.start())
        description = " ".join(_strings(source[start:end]))
        add(
            _make_finding(
                path,
                source,
                masked,
                start,
                end,
                "counter-only",
                f"counter-mutation:{name}:{_compact(mutation)}",
                description,
            )
        )

    # Status-only returns are bounded to predicate/operation functions or a
    # nearby error signal. This catches silent failure propagation without
    # turning every ordinary boolean return into an error row.
    for match in STATUS_RETURN_RE.finditer(masked):
        function_name = _enclosing_function(function_extents, match.start())
        context_start = max(0, _previous_boundary(masked, match.start()) - 320)
        context_mask = masked[context_start : match.end()]
        operation_name = function_name.split("::")[-1]
        operation_signal = re.search(
            r"(?:Try|Load|Save|Write|Read|Open|Create|Init|Parse|Validate|Compile|Present|Capture|Restore|Apply|Execute)",
            operation_name,
        )
        if (
            operation_signal is None
            and ERROR_NAME_RE.search(context_mask) is None
            and RECOVERY_NAME_RE.search(context_mask) is None
            and ERROR_TEXT_RE.search(" ".join(_strings(source[context_start : match.end()]))) is None
        ):
            continue
        start = _previous_boundary(masked, match.start())
        description = " ".join(_strings(source[context_start : match.end()]))
        add(
            _make_finding(
                path,
                source,
                masked,
                start,
                match.end(),
                "status-only",
                f"status-return:{function_name}:{match.group('value')}",
                description,
            )
        )

    # String-backed status fields and UI messages can otherwise evade call-name
    # discovery. Only error-like text in a statement whose code names a status,
    # message, reason, warning, or error owner becomes a candidate.
    occupied_call_spans.sort()
    occupied_starts = [start for start, _ in occupied_call_spans]
    for string_match in CPP_STRING_RE.finditer(source):
        occupied_index = bisect_right(occupied_starts, string_match.start()) - 1
        if occupied_index >= 0 and string_match.start() < occupied_call_spans[occupied_index][1]:
            continue
        start, end = _statement_bounds(masked, string_match.start())
        code = masked[start:end]
        if not code.strip() or re.search(r"(?:status|message|reason|warning|error)", code, re.IGNORECASE) is None:
            continue
        description = _decode_cpp_string(string_match.group("body") or string_match.group("raw_body") or "")
        if ERROR_TEXT_RE.search(description) is None:
            continue
        add(
            _make_finding(
                path,
                source,
                masked,
                start,
                end,
                "status-presentation",
                f"status-message:{_normalize_operation(code, '<status-message>')}",
                description,
            )
        )

    return sorted(findings.values())


def _git_ls_files(repo: Path, *paths: str) -> list[str]:
    result = subprocess.run(
        ["git", "ls-files", "-z", "--", *paths],
        cwd=repo,
        check=True,
        capture_output=True,
    )
    return sorted(name for name in result.stdout.decode("utf-8", errors="strict").split("\0") if name)


def _pe_import_names(data: bytes) -> set[str]:
    """Read normal and delay-load PE import names without external packages."""
    if len(data) < 0x40 or data[:2] != b"MZ":
        raise ValueError("retained executable is not a bounded PE image")
    pe_offset = struct.unpack_from("<I", data, 0x3C)[0]
    if pe_offset + 24 > len(data) or data[pe_offset : pe_offset + 4] != b"PE\0\0":
        raise ValueError("retained executable has no valid PE signature")
    coff = pe_offset + 4
    section_count = struct.unpack_from("<H", data, coff + 2)[0]
    optional_size = struct.unpack_from("<H", data, coff + 16)[0]
    optional = coff + 20
    if optional + optional_size > len(data):
        raise ValueError("retained executable optional header is truncated")
    magic = struct.unpack_from("<H", data, optional)[0]
    if magic == 0x20B:
        data_directories = optional + 112
        image_base = struct.unpack_from("<Q", data, optional + 24)[0]
    elif magic == 0x10B:
        data_directories = optional + 96
        image_base = struct.unpack_from("<I", data, optional + 28)[0]
    else:
        raise ValueError(f"retained executable has unsupported PE optional magic 0x{magic:04x}")
    if data_directories + 14 * 8 > optional + optional_size:
        raise ValueError("retained executable omits required import directories")

    section_table = optional + optional_size
    sections: list[tuple[int, int, int]] = []
    for index in range(section_count):
        row = section_table + index * 40
        if row + 40 > len(data):
            raise ValueError("retained executable section table is truncated")
        virtual_size, virtual_address, raw_size, raw_offset = struct.unpack_from("<IIII", data, row + 8)
        sections.append((virtual_address, max(virtual_size, raw_size), raw_offset))

    def file_offset(rva: int) -> int:
        for virtual_address, size, raw_offset in sections:
            if virtual_address <= rva < virtual_address + size:
                candidate = raw_offset + rva - virtual_address
                if candidate < len(data):
                    return candidate
        if rva < len(data):
            return rva
        raise ValueError(f"PE import RVA 0x{rva:x} is outside mapped sections")

    def c_string(rva: int) -> str:
        start = file_offset(rva)
        end = data.find(b"\0", start, min(len(data), start + 512))
        if end < 0:
            raise ValueError(f"PE import name at RVA 0x{rva:x} is not bounded")
        return data[start:end].decode("ascii", errors="strict").lower()

    imports: set[str] = set()
    import_rva, import_size = struct.unpack_from("<II", data, data_directories + 8)
    if import_rva:
        descriptor = file_offset(import_rva)
        limit = min(len(data), descriptor + max(import_size, 20))
        while descriptor + 20 <= limit:
            values = struct.unpack_from("<IIIII", data, descriptor)
            if not any(values):
                break
            imports.add(c_string(values[3]))
            descriptor += 20

    delay_rva, delay_size = struct.unpack_from("<II", data, data_directories + 13 * 8)
    if delay_rva:
        descriptor = file_offset(delay_rva)
        limit = min(len(data), descriptor + max(delay_size, 32))
        while descriptor + 32 <= limit:
            values = struct.unpack_from("<IIIIIIII", data, descriptor)
            if not any(values):
                break
            name_rva = values[1] if values[0] & 1 else values[1] - image_base
            imports.add(c_string(name_rva))
            descriptor += 32
    return imports


def _scan_bundle_mismatches(repo: Path) -> list[Finding]:
    findings: list[Finding] = []
    for relative in _git_ls_files(repo, RETAINED_ARTIFACT_ROOT):
        if not relative.lower().endswith(".exe"):
            continue
        executable = repo / relative
        data = executable.read_bytes()
        imports = _pe_import_names(data)
        for imported in sorted(imports & KNOWN_NON_SYSTEM_IMPORTS):
            if (executable.parent / imported).exists():
                continue
            operation = f"retained-bundle-import:{imported}"
            fingerprint = _sha256_bytes(data + b"\0" + imported.encode("ascii"))
            findings.append(
                Finding(
                    path=_normalize_path(relative),
                    line=1,
                    column=1,
                    site_class="bundle-import-mismatch",
                    operation=operation,
                    source_fingerprint=fingerprint,
                    description_classification="not-applicable",
                    description="",
                    evidence=f"imports {imported}; sibling runtime is absent",
                )
            )
    return findings


def scan_repository(repo: Path) -> ScanResult:
    diagnostics: list[str] = []
    try:
        tracked = [
            name
            for name in _git_ls_files(repo, SOURCE_ROOT)
            if Path(name).suffix.lower() in SOURCE_SUFFIXES
        ]
    except (OSError, subprocess.CalledProcessError, UnicodeError) as error:
        return ScanResult([], 0, _sha256_bytes(b""), [f"tracked-source discovery failed: {error}"])

    findings: list[Finding] = []
    manifest_rows: list[str] = []
    for relative in tracked:
        source_path = repo / relative
        try:
            source = source_path.read_text(encoding="utf-8")
        except (OSError, UnicodeError) as error:
            diagnostics.append(f"cannot read tracked source {relative}: {error}")
            continue
        manifest_rows.append(f"{_normalize_path(relative)}\0{_sha256_bytes(source.encode('utf-8'))}")
        findings.extend(scan_text(_normalize_path(relative), source))

    try:
        findings.extend(_scan_bundle_mismatches(repo))
    except (OSError, subprocess.CalledProcessError, UnicodeError, ValueError) as error:
        diagnostics.append(f"retained-bundle discovery failed: {error}")

    manifest = "\n".join(sorted(manifest_rows)).encode("utf-8")
    return ScanResult(sorted(findings), len(tracked), _sha256_bytes(manifest), diagnostics)


def _ruling_identity(row: dict[str, object]) -> tuple[object, ...]:
    return (
        row.get("path"),
        row.get("line"),
        row.get("column"),
        row.get("site_class"),
        row.get("operation"),
        row.get("source_fingerprint"),
    )


def _identity_text(identity: Sequence[object]) -> str:
    return f"{identity[0]}:{identity[1]}:{identity[2]} {identity[3]} {identity[4]}"


def _validate_ruling(row: object, index: int) -> tuple[dict[str, object] | None, list[RulingIssue]]:
    issues: list[RulingIssue] = []
    label = f"rulings[{index}]"
    if not isinstance(row, dict):
        return None, [RulingIssue(label, "must be an object")]
    required = {
        "path",
        "line",
        "column",
        "site_class",
        "operation",
        "source_fingerprint",
        "disposition",
        "description",
        "description_classification",
        "owner",
        "reason",
        "repair_phase",
        "adjudication",
    }
    missing = sorted(required - set(row))
    extra = sorted(set(row) - required)
    if missing:
        issues.append(RulingIssue(label, f"missing fields: {', '.join(missing)}"))
    if extra:
        issues.append(RulingIssue(label, f"unsupported fields: {', '.join(extra)}"))
    if issues:
        return None, issues

    for field in (
        "path",
        "site_class",
        "operation",
        "source_fingerprint",
        "disposition",
        "description",
        "description_classification",
        "owner",
        "reason",
        "repair_phase",
        "adjudication",
    ):
        if not isinstance(row[field], str):
            issues.append(RulingIssue(label, f"{field} must be a string"))
    for field in ("line", "column"):
        if not isinstance(row[field], int) or isinstance(row[field], bool) or row[field] < 1:
            issues.append(RulingIssue(label, f"{field} must be a positive integer"))
    if issues:
        return None, issues

    path = str(row["path"])
    if "\\" in path or PurePosixPath(path).as_posix() != path:
        issues.append(RulingIssue(label, "path must be canonical repository-relative POSIX text"))
    if SHA256_RE.fullmatch(str(row["source_fingerprint"])) is None:
        issues.append(RulingIssue(label, "source_fingerprint must be lowercase SHA-256"))
    if row["disposition"] not in DISPOSITIONS:
        issues.append(RulingIssue(label, f"unsupported disposition {row['disposition']!r}"))
    if row["description_classification"] not in DESCRIPTION_CLASSIFICATIONS:
        issues.append(RulingIssue(label, f"unsupported description classification {row['description_classification']!r}"))
    if row["adjudication"] not in ADJUDICATION_STATUSES:
        issues.append(RulingIssue(label, f"unsupported adjudication {row['adjudication']!r}"))
    if not str(row["owner"]).strip() or not str(row["reason"]).strip():
        issues.append(RulingIssue(label, "owner and reason must be non-empty"))
    repair_phase = str(row["repair_phase"])
    if repair_phase and repair_phase not in REPAIR_PHASES:
        issues.append(RulingIssue(label, f"repair_phase must be blank or one of {sorted(REPAIR_PHASES)}"))
    if row["disposition"] == "repair" and not repair_phase:
        issues.append(RulingIssue(label, "repair disposition requires a named E phase"))
    return (row if not issues else None), issues


def load_rulings(path: Path) -> tuple[str, list[dict[str, object]], list[RulingIssue]]:
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        return "", [], [RulingIssue("*", f"cannot load ruling file {path}: {error}")]
    if not isinstance(document, dict):
        return "", [], [RulingIssue("*", "ruling document must be an object")]
    expected_fields = {"schema_version", "review_status", "reference", "repair_plan", "rulings"}
    missing_fields = sorted(expected_fields - set(document))
    extra_fields = sorted(set(document) - expected_fields)
    if missing_fields or extra_fields:
        details: list[str] = []
        if missing_fields:
            details.append(f"missing fields: {', '.join(missing_fields)}")
        if extra_fields:
            details.append(f"unsupported fields: {', '.join(extra_fields)}")
        return "", [], [RulingIssue("*", "; ".join(details))]
    if document.get("schema_version") != 1:
        return "", [], [RulingIssue("*", "ruling schema_version must be 1")]
    review_status = document.get("review_status")
    if review_status not in REVIEW_STATUSES:
        return "", [], [RulingIssue("*", f"review_status must be one of {sorted(REVIEW_STATUSES)}")]
    if document.get("reference") != REFERENCE_PATH:
        return "", [], [RulingIssue("*", f"reference must be {REFERENCE_PATH}")]
    if document.get("repair_plan") != REPAIR_PLAN:
        return "", [], [RulingIssue("*", f"repair_plan must be {REPAIR_PLAN}")]
    raw_rows = document.get("rulings")
    if not isinstance(raw_rows, list):
        return "", [], [RulingIssue("*", "rulings must be an array")]
    rows: list[dict[str, object]] = []
    issues: list[RulingIssue] = []
    seen: set[tuple[object, ...]] = set()
    for index, raw in enumerate(raw_rows):
        row, row_issues = _validate_ruling(raw, index)
        issues.extend(row_issues)
        if row is None:
            continue
        identity = _ruling_identity(row)
        if identity in seen:
            issues.append(RulingIssue(_identity_text(identity), "duplicate ruling identity"))
            continue
        seen.add(identity)
        rows.append(row)
    identities = [_ruling_identity(row) for row in rows]
    if identities != sorted(identities):
        issues.append(RulingIssue("*", "rulings must be sorted by exact identity"))
    return str(review_status), rows, issues


def evaluate(
    findings: Sequence[Finding],
    review_status: str,
    rulings: Sequence[dict[str, object]],
    load_issues: Iterable[RulingIssue],
    repo: Path,
) -> Evaluation:
    issues = list(load_issues)
    if review_status != "ratified":
        issues.append(RulingIssue("*", "ruling document is not owner-ratified"))
    if not (repo / REPAIR_PLAN).is_file():
        issues.append(RulingIssue("*", f"repair plan does not exist: {REPAIR_PLAN}"))
    if not (repo / REFERENCE_PATH).is_file():
        issues.append(RulingIssue("*", f"durable reporting reference does not exist: {REFERENCE_PATH}"))

    findings_by_identity = {finding.identity: finding for finding in findings}
    rulings_by_identity = {_ruling_identity(row): row for row in rulings}
    unruled = [finding for finding in findings if finding.identity not in rulings_by_identity]
    stale = [row for row in rulings if _ruling_identity(row) not in findings_by_identity]

    for finding in findings:
        row = rulings_by_identity.get(finding.identity)
        if row is None:
            continue
        disposition = str(row["disposition"])
        classification = str(row["description_classification"])
        repair_phase = str(row["repair_phase"])
        identity_text = _identity_text(finding.identity)
        if row["description"] != finding.description:
            issues.append(RulingIssue(identity_text, "description evidence drifted from the current source slice"))
        if classification != finding.description_classification:
            issues.append(
                RulingIssue(
                    identity_text,
                    f"description classification drift: ruling={classification} current={finding.description_classification}",
                )
            )
        if row["adjudication"] != "owner-reviewed":
            issues.append(RulingIssue(identity_text, "row has not received owner-specific adjudication"))
        suggestion = _suggest_ruling(finding)
        suggested_fields = ("disposition", "description_classification", "owner", "reason", "repair_phase")
        if all(row[field] == suggestion[field] for field in suggested_fields):
            issues.append(RulingIssue(identity_text, "generated suggestion cannot masquerade as owner adjudication"))
        reason = str(row["reason"])
        owner = str(row["owner"])
        operation_anchor = _operation_name(finding.operation)
        has_exact_basis = (
            operation_anchor in reason
            or finding.site_class in reason
            or finding.path in reason
            or (bool(finding.description) and finding.description in reason)
        )
        if not reason.startswith(f"{owner} ") or len(reason) < 80 or not has_exact_basis:
            issues.append(
                RulingIssue(
                    identity_text,
                    "owner-reviewed ruling lacks an owner-led operation, site, path, or exact-description basis",
                )
            )
        if disposition == "successful-fallback-value-state":
            if owner not in reason or len(reason) < 80 or (
                operation_anchor not in reason and finding.site_class not in reason
            ):
                issues.append(
                    RulingIssue(
                        identity_text,
                        "successful value/fallback ruling lacks a concrete owner and operation/invariant basis",
                    )
                )
            if finding.site_class == "error-wrapper" and operation_anchor == "FAILED":
                issues.append(RulingIssue(identity_text, "FAILED(HRESULT operation) cannot be approved as value-only"))
            if finding.site_class == "status-only" and re.match(r"^(?:Validate|Hash)", operation_anchor):
                issues.append(RulingIssue(identity_text, "Validate*/Hash* failure return cannot be approved as a query"))
        if disposition in {"sb-warning", "recoverable-sb-error", "fatal-sb-error", "runtime-assertion"}:
            if classification in INADEQUATE_DESCRIPTIONS and not repair_phase:
                issues.append(RulingIssue(identity_text, "inadequate error/assertion description has no repair phase"))
        if finding.site_class in {
            "raw-stderr",
            "event-sink",
            "dialog-sink",
            "debugger-sink",
            "status-presentation",
            "counter-only",
            "status-only",
            "ignored-crt-io-outcome",
            "pre-entry-fatal",
            "bundle-import-mismatch",
        } and disposition not in {"successful-fallback-value-state", "test-only-deliberate-failure"} and not repair_phase:
            issues.append(RulingIssue(identity_text, "non-central or silent site has no repair phase"))

    return Evaluation(issues=issues, unruled=unruled, stale=stale)


def _suggest_ruling(finding: Finding) -> dict[str, object]:
    owner = _path_owner(finding.path)
    site_class = finding.site_class
    operation_name = _operation_name(finding.operation)
    description_inadequate = finding.description_classification in INADEQUATE_DESCRIPTIONS
    disposition = "repair"
    repair_phase = _repair_phase_for_path(finding.path, site_class)
    reason = "Current site requires owner adjudication under the complete E0 error surface."

    if site_class == "sb-result-failure":
        disposition = "recoverable-sb-error"
        repair_phase = _repair_phase_for_path(finding.path, site_class) if description_inadequate else ""
        reason = "Creates a failed SbResult through the Core diagnostic store at this exact operation."
    elif site_class == "result-construction-owner":
        disposition = "repair"
        repair_phase = "E2"
        reason = "Owns or invokes the central failed-result construction path that E2 must make durably observable."
    elif site_class == "sb-fatal":
        disposition = "fatal-sb-error"
        repair_phase = _repair_phase_for_path(finding.path, site_class) if description_inadequate else ""
        reason = "Terminates through the current SB fatal-invariant API at this exact operation."
    elif site_class == "fatal-owner":
        disposition = "repair"
        repair_phase = "E3"
        reason = "Implements the central fatal owner that E3 must route through the all-build packet policy."
    elif site_class == "runtime-assertion":
        disposition = "runtime-assertion"
        repair_phase = _repair_phase_for_path(finding.path, site_class)
        reason = "Uses the CRT runtime assertion path without the required owner and human invariant contract."
    elif site_class == "static-assertion":
        if description_inadequate:
            disposition = "repair"
            repair_phase = _repair_phase_for_path(finding.path, site_class)
            reason = "Compile-time invariant lacks the required non-empty actionable diagnostic string."
        else:
            disposition = "successful-fallback-value-state"
            repair_phase = ""
            reason = "Compile-time invariant is outside runtime packet emission and already carries a human diagnostic."
    elif site_class == "pre-entry-fatal":
        disposition = "fatal-sb-error"
        reason = "Terminates the process through a raw path that must preserve the fatal packet contract."
    elif site_class == "bundle-import-mismatch":
        disposition = "repair"
        repair_phase = "E5"
        reason = "Retained executable imports a known non-system runtime that is absent from its artifact directory."
    elif site_class in {"raw-stderr", "event-sink", "dialog-sink", "debugger-sink", "status-presentation", "message-template"}:
        if _is_probe(finding.path, finding.operation):
            disposition = "test-only-deliberate-failure"
            repair_phase = ""
            reason = "Validation/probe-only evidence deliberately reports a bounded test outcome, not a production SB origin."
        else:
            disposition = "repair"
            reason = "A non-central sink or message template requires owner-specific proof before it can be treated as informational."
    elif site_class == "error-wrapper":
        name = re.sub(r"[^A-Za-z0-9_]", "", operation_name)
        if _is_probe(finding.path, finding.operation):
            disposition = "test-only-deliberate-failure"
            repair_phase = ""
            reason = "Production probe harness deliberately manufactures or records a bounded validation failure."
        elif _is_value_query(name):
            disposition = "successful-fallback-value-state"
            repair_phase = ""
            reason = "Queries or formats an error-related value without creating or reporting a new SB error."
        elif "fatal" in name.lower():
            disposition = "fatal-sb-error"
            reason = "Custom fatal wrapper terminates or forwards a fatal condition outside the common SB fatal owner."
        elif "failure" in name.lower() or "fail" in name.lower():
            disposition = "repair"
            reason = "Failure-named wrapper requires E-phase adjudication of creation, propagation, or process promotion."
        else:
            disposition = "repair"
            reason = "Error-named wrapper or template requires exact owner adjudication before central migration."
    elif site_class == "recovery-operation":
        disposition = "repair"
        reason = "Recovery vocabulary alone cannot prove successful fallback; the owning invariant requires individual review."
    elif site_class == "status-only":
        function_name = finding.operation.split(":", 2)[1] if ":" in finding.operation else ""
        if _is_value_query(function_name.split("::")[-1]):
            disposition = "successful-fallback-value-state"
            repair_phase = ""
            reason = "Predicate/value query uses false or an empty sentinel as its documented non-error state."
        else:
            disposition = "repair"
            reason = "Operation collapses a potentially failing path to a boolean/sentinel without an SB result."
    elif site_class == "counter-only":
        if re.search(r":=\s*(?:0|false)$", finding.operation, re.IGNORECASE):
            disposition = "successful-fallback-value-state"
            repair_phase = ""
            reason = "Resets diagnostic counter/value state and does not create a new warning or error occurrence."
        else:
            disposition = "repair"
            reason = "Counter or sticky flag is the only local evidence of an error-like loss or rejection."
    elif site_class == "ignored-crt-io-outcome":
        disposition = "repair"
        reason = "The CRT open/write/flush/close return value is discarded, so persistence failure requires an owning E-phase repair."

    classification = finding.description_classification
    reason = f"UNREVIEWED SUGGESTION: {reason}"
    return {
        "path": finding.path,
        "line": finding.line,
        "column": finding.column,
        "site_class": finding.site_class,
        "operation": finding.operation,
        "source_fingerprint": finding.source_fingerprint,
        "disposition": disposition,
        "description": finding.description,
        "description_classification": classification,
        "owner": owner,
        "reason": reason,
        "repair_phase": repair_phase,
        "adjudication": "unreviewed",
    }


def write_unreviewed_template(path: Path, findings: Sequence[Finding]) -> None:
    if path.exists():
        raise FileExistsError(f"refusing to overwrite existing ruling file: {path}")
    document = {
        "schema_version": 1,
        "review_status": "unreviewed",
        "reference": REFERENCE_PATH,
        "repair_plan": REPAIR_PLAN,
        "rulings": [_suggest_ruling(finding) for finding in findings],
    }
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(document, indent=2, sort_keys=False) + "\n", encoding="utf-8")


def _inventory_digest(findings: Sequence[Finding]) -> str:
    identities = [list(finding.identity) for finding in findings]
    return _sha256_bytes(json.dumps(identities, separators=(",", ":"), ensure_ascii=True).encode("utf-8"))


def _payload(scan: ScanResult, evaluation: Evaluation, rulings: Sequence[dict[str, object]]) -> dict[str, object]:
    site_classes = Counter(finding.site_class for finding in scan.findings)
    descriptions = Counter(finding.description_classification for finding in scan.findings)
    dispositions = Counter(str(row["disposition"]) for row in rulings)
    repair_phases = Counter(str(row["repair_phase"]) for row in rulings if row["repair_phase"])
    return {
        "summary": {
            "source_files_scanned": scan.source_files_scanned,
            "source_manifest_sha256": scan.source_manifest_sha256,
            "findings": len(scan.findings),
            "inventory_sha256": _inventory_digest(scan.findings),
            "current_rulings": len(scan.findings) - len(evaluation.unruled),
            "unruled": len(evaluation.unruled),
            "stale_rulings": len(evaluation.stale),
            "issues": len(scan.diagnostics) + len(evaluation.issues),
            "site_classes": dict(sorted(site_classes.items())),
            "description_classifications": dict(sorted(descriptions.items())),
            "dispositions": dict(sorted(dispositions.items())),
            "repair_phases": dict(sorted(repair_phases.items())),
        },
        "findings": [asdict(finding) for finding in scan.findings],
        "scan_diagnostics": scan.diagnostics,
        "ruling_issues": [asdict(issue) for issue in evaluation.issues],
        "unruled": [asdict(finding) for finding in evaluation.unruled],
        "stale_rulings": evaluation.stale,
    }


def _render_text(payload: dict[str, object]) -> str:
    summary = payload["summary"]
    assert isinstance(summary, dict)
    lines = [
        "Error observability inventory",
        (
            f"source_files={summary['source_files_scanned']} findings={summary['findings']} "
            f"current_rulings={summary['current_rulings']} unruled={summary['unruled']} "
            f"stale={summary['stale_rulings']} issues={summary['issues']}"
        ),
        f"source_manifest_sha256={summary['source_manifest_sha256']}",
        f"inventory_sha256={summary['inventory_sha256']}",
        "site_classes=" + json.dumps(summary["site_classes"], sort_keys=True),
        "description_classifications=" + json.dumps(summary["description_classifications"], sort_keys=True),
        "dispositions=" + json.dumps(summary["dispositions"], sort_keys=True),
        "repair_phases=" + json.dumps(summary["repair_phases"], sort_keys=True),
    ]
    for diagnostic in payload["scan_diagnostics"]:
        lines.append(f"SCAN-ERROR {diagnostic}")
    for issue in payload["ruling_issues"]:
        lines.append(f"RULING-ERROR {issue['identity']}: {issue['message']}")
    for finding in payload["unruled"]:
        lines.append(
            f"UNRULED {finding['path']}:{finding['line']}:{finding['column']} "
            f"{finding['site_class']} {finding['operation']} fingerprint={finding['source_fingerprint']}"
        )
    for row in payload["stale_rulings"]:
        lines.append(
            f"STALE {row.get('path')}:{row.get('line')}:{row.get('column')} "
            f"{row.get('site_class')} {row.get('operation')} fingerprint={row.get('source_fingerprint')}"
        )
    return "\n".join(lines)


def _fixture_ruling(finding: Finding, *, disposition: str | None = None, repair_phase: str | None = None) -> dict[str, object]:
    row = _suggest_ruling(finding)
    row["adjudication"] = "owner-reviewed"
    row["reason"] = (
        f"{row['owner']} reviewed {finding.site_class} at {finding.operation}; "
        "the bounded self-test expectation owns this exact semantic classification."
    )
    if disposition is not None:
        row["disposition"] = disposition
    if repair_phase is not None:
        row["repair_phase"] = repair_phase
    return row


def _fixture_pe(import_name: str, *, delay_import: bool = False) -> bytes:
    data = bytearray(0x400)
    data[:2] = b"MZ"
    struct.pack_into("<I", data, 0x3C, 0x80)
    data[0x80:0x84] = b"PE\0\0"
    coff = 0x84
    struct.pack_into("<HHIIIHH", data, coff, 0x8664, 1, 0, 0, 0, 0xF0, 0x0022)
    optional = coff + 20
    struct.pack_into("<H", data, optional, 0x20B)
    struct.pack_into("<Q", data, optional + 24, 0x140000000)
    struct.pack_into("<I", data, optional + 108, 16)
    directory_offset = optional + 112 + (13 * 8 if delay_import else 8)
    struct.pack_into("<II", data, directory_offset, 0x1000, 64 if delay_import else 40)
    section = optional + 0xF0
    data[section : section + 8] = b".rdata\0\0"
    struct.pack_into("<IIII", data, section + 8, 0x200, 0x1000, 0x200, 0x200)
    if delay_import:
        struct.pack_into("<IIIIIIII", data, 0x200, 1, 0x1050, 0, 0, 0, 0, 0, 0)
    else:
        struct.pack_into("<IIIII", data, 0x200, 0, 0, 0, 0x1050, 0)
    encoded = import_name.encode("ascii") + b"\0"
    data[0x250 : 0x250 + len(encoded)] = encoded
    return bytes(data)


def self_test() -> int:
    failures: list[str] = []
    fixture = r'''
// SB_FATAL("comment", "must not be scanned");
const char* decoy = "assert(false) and MessageBoxA are data";
SbResult Build( SbDiagnosticStore& diagnostics )
{
    FILE* rawFile = nullptr;
    fopen_s( &rawFile, "fixture.log", "wb" );
    fprintf( rawFile, "fixture row=%d", row );
    vfprintf( rawFile, format, args );
    fputs( "fixture line", rawFile );
    if ( fprintf( rawFile, "checked row=%d", row ) < 0 )
        return false;
    fflush( rawFile );
    fclose( rawFile );
    if ( fflush( rawFile ) != 0 )
        return false;
    if ( badInput )
    {
        return diagnostics.Failure( "Fixture/Build", "Input width must be positive. width=%d", width );
    }
    if ( resourceFailed )
        return diagnostics.Failure( "Fixture/Build", "CreateCommittedResource failed" );
    if ( FAILED( device->CreateCommittedResource() ) )
        return false;
    MarkSweptFallback( 1 );
    Log().WriteEventf( "projection diverged at frame=%d", frame );
    std::fprintf( stderr, "failed\n" );
    MessageBoxA( nullptr, "Asset is missing", "Fixture", 0 );
    CopyStatusMessage( state, "device unavailable" );
    MarkFailed();
    if ( ParserFailed() )
        Log().WriteEventf( "scene_finished test_complete=%d", state.testComplete );
    snprintf( message, sizeof( message ), "projection diverged at frame %d", frame );
    snprintf( status, sizeof( status ), "fps=%d", fps );
    ++state.droppedEventCount;
    ApplyFallbackValue();
    assert( ready );
    static_assert( sizeof( int ) >= 4, "int storage must cover four bytes" );
    return false;
}
bool ValidateLoadedReflection()
{
    return false;
}
bool HashGraphicsDesc()
{
    if ( FAILED( device->CreateQueryHeap() ) )
        return false;
    return true;
}
[[noreturn]] void FatalAllocationFailure()
{
    std::abort();
}
'''
    findings = scan_text("SkullbonezSource/Fixture.cpp", fixture)
    classes = Counter(finding.site_class for finding in findings)
    required_classes = {
        "sb-result-failure",
        "event-sink",
        "raw-stderr",
        "dialog-sink",
        "status-presentation",
        "counter-only",
        "ignored-crt-io-outcome",
        "recovery-operation",
        "runtime-assertion",
        "static-assertion",
        "status-only",
        "error-wrapper",
        "pre-entry-fatal",
    }
    missing_classes = sorted(required_classes - set(classes))
    if missing_classes:
        failures.append(f"site-class fixtures missing: {missing_classes}")
    if any("comment" in finding.evidence or "decoy" in finding.operation for finding in findings):
        failures.append("comments or string data created a call finding")
    if findings != scan_text("SkullbonezSource/Fixture.cpp", fixture):
        failures.append("same input did not produce deterministic findings")

    mark_failed = next((finding for finding in findings if finding.operation == "call:MarkFailed()"), None)
    if mark_failed is None or _suggest_ruling(mark_failed)["disposition"] != "repair":
        failures.append("failure mutator was mistaken for a value query")
    parser_failed = next((finding for finding in findings if finding.operation == "call:ParserFailed()"), None)
    if parser_failed is None or _suggest_ruling(parser_failed)["disposition"] != "successful-fallback-value-state":
        failures.append("failure predicate was mistaken for a mutating origin")
    scene_event = next((finding for finding in findings if "state.testComplete" in finding.operation), None)
    if scene_event is None or _is_probe(scene_event.path, scene_event.operation):
        failures.append("probe vocabulary in an argument changed event ownership")
    generic_message = next((finding for finding in findings if "message, sizeof" in finding.operation), None)
    if generic_message is None or _suggest_ruling(generic_message)["disposition"] != "repair":
        failures.append("generic message-template destination bypassed owner review")
    value_status = next((finding for finding in findings if "status, sizeof" in finding.operation), None)
    if value_status is None or _suggest_ruling(value_status)["disposition"] != "repair":
        failures.append("ordinary status formatter bypassed owner-specific adjudication")

    ignored_crt = [finding for finding in findings if finding.site_class == "ignored-crt-io-outcome"]
    if len(ignored_crt) != 7:
        failures.append(f"ignored CRT I/O fixture count was {len(ignored_crt)}, expected 7")
    ignored_non_stderr_fprintf = [
        finding
        for finding in ignored_crt
        if "fprintf( rawFile" in finding.operation and finding.description.startswith("fixture row")
    ]
    if len(ignored_non_stderr_fprintf) != 1:
        failures.append("ignored non-stderr fprintf outcome was not uniquely inventoried")
    ignored_operation_names = {
        match.group(1)
        for finding in ignored_crt
        if (match := re.search(r"call:(?:std::)?([A-Za-z_]+)\(", finding.operation)) is not None
    }
    if not {"fprintf", "vfprintf", "fputs", "fopen_s", "fflush", "fclose"}.issubset(ignored_operation_names):
        failures.append("ignored CRT I/O fixtures did not cover Log/diagnostic write variants")
    if any("checked row" in finding.description for finding in ignored_crt):
        failures.append("handled fprintf outcome was inventoried as ignored")
    failed_hresult = next((finding for finding in findings if finding.operation.startswith("call:FAILED(")), None)
    if failed_hresult is None or _suggest_ruling(failed_hresult)["disposition"] != "repair":
        failures.append("FAILED(HRESULT operation) was accepted as an ordinary predicate")
    validate_return = next(
        (finding for finding in findings if finding.operation == "status-return:ValidateLoadedReflection:false"), None
    )
    if validate_return is None or _suggest_ruling(validate_return)["disposition"] != "repair":
        failures.append("Validate* failure return was accepted as an ordinary predicate")
    hash_return = next((finding for finding in findings if finding.operation == "status-return:HashGraphicsDesc:false"), None)
    if hash_return is None or _suggest_ruling(hash_return)["disposition"] != "repair":
        failures.append("Hash* operation was mistaken for a Has* predicate")
    fallback_call = next((finding for finding in findings if finding.operation.startswith("call:MarkSweptFallback(")), None)
    if fallback_call is None or _suggest_ruling(fallback_call)["disposition"] != "repair":
        failures.append("fallback vocabulary bypassed owner-specific adjudication")
    diverged_event = next((finding for finding in findings if finding.description.startswith("projection diverged")), None)
    if diverged_event is None or _suggest_ruling(diverged_event)["disposition"] != "repair":
        failures.append("error event without generic error vocabulary was accepted as informational")
    context_free_failure = next(
        (finding for finding in findings if finding.description == "CreateCommittedResource failed"), None
    )
    if context_free_failure is None or context_free_failure.description_classification != "context-free":
        failures.append("operation-plus-failed message was incorrectly marked actionable")

    description_cases = {
        "generic": _classify_description("failed", "raw-stderr"),
        "code-only": _classify_description("0x%08X", "raw-stderr"),
        "expression-only": _classify_description("", "runtime-assertion"),
        "context-free": _classify_description("Asset is missing", "dialog-sink"),
        "missing": _classify_description("", "event-sink"),
        "actionable": _classify_description("Input width must be positive. width=%d", "sb-result-failure"),
    }
    for expected, actual in description_cases.items():
        if actual != expected:
            failures.append(f"description fixture {expected} classified as {actual}")

    with TemporaryDirectory() as temporary:
        repo = Path(temporary)
        (repo / "Agentic/Plans/TODO").mkdir(parents=True)
        (repo / REPAIR_PLAN).write_text("# fixture\n", encoding="utf-8")
        (repo / "Agentic/Reference").mkdir(parents=True)
        (repo / REFERENCE_PATH).write_text("# fixture\n", encoding="utf-8")
        rulings = [_fixture_ruling(finding) for finding in findings]
        ruling_document = {
            "schema_version": 1,
            "review_status": "ratified",
            "reference": REFERENCE_PATH,
            "repair_plan": REPAIR_PLAN,
            "rulings": rulings,
        }
        ruling_path = repo / "fixture-rulings.json"
        ruling_path.write_text(json.dumps(ruling_document), encoding="utf-8")
        loaded_status, loaded_rows, schema_issues = load_rulings(ruling_path)
        if loaded_status != "ratified" or loaded_rows != rulings or schema_issues:
            failures.append("exact deterministic ruling schema did not load")
        ruling_document["unexpected"] = True
        ruling_path.write_text(json.dumps(ruling_document), encoding="utf-8")
        if not load_rulings(ruling_path)[2]:
            failures.append("unsupported top-level ruling field was accepted")

        exact = evaluate(findings, "ratified", rulings, [], repo)
        if exact.issues or exact.unruled or exact.stale:
            failures.append("exact current rulings did not pass")

        prohibited_value_rows = list(rulings)
        for prohibited_finding in (failed_hresult, hash_return):
            assert prohibited_finding is not None
            row_index = next(
                index
                for index, row in enumerate(prohibited_value_rows)
                if _ruling_identity(row) == prohibited_finding.identity
            )
            prohibited_value_rows[row_index] = {
                **prohibited_value_rows[row_index],
                "disposition": "successful-fallback-value-state",
                "repair_phase": "",
            }
        prohibited_values = evaluate(findings, "ratified", prohibited_value_rows, [], repo)
        if not any("cannot be approved" in issue.message for issue in prohibited_values.issues):
            failures.append("FAILED/Hash false-pass ruling bypassed strict semantic controls")

        masquerading = []
        for finding in findings:
            row = _suggest_ruling(finding)
            row["adjudication"] = "owner-reviewed"
            masquerading.append(row)
        unchanged_suggestions = evaluate(findings, "ratified", masquerading, [], repo)
        if not any("cannot masquerade" in issue.message for issue in unchanged_suggestions.issues):
            failures.append("generated suggestions passed after a mechanical adjudication-state flip")

        transformed_suggestions = []
        for finding in findings:
            row = _suggest_ruling(finding)
            row["adjudication"] = "owner-reviewed"
            row["reason"] = str(row["reason"]).removeprefix("UNREVIEWED SUGGESTION: ")
            transformed_suggestions.append(row)
        transformed = evaluate(findings, "ratified", transformed_suggestions, [], repo)
        if not any("lacks an owner-led" in issue.message for issue in transformed.issues):
            failures.append("mechanically transformed suggestions passed without owner-authored bases")

        missing = evaluate(findings, "ratified", rulings[:-1], [], repo)
        if len(missing.unruled) != 1:
            failures.append("missing ruling did not create one unruled row")

        edited = list(rulings)
        edited[0] = {**edited[0], "source_fingerprint": "0" * 64}
        drift = evaluate(findings, "ratified", edited, [], repo)
        if len(drift.unruled) != 1 or len(drift.stale) != 1:
            failures.append("fingerprint drift did not create unruled and stale rows")

        description_edited = list(rulings)
        description_edited[0] = {**description_edited[0], "description": "reviewer substituted evidence"}
        description_drift = evaluate(findings, "ratified", description_edited, [], repo)
        if not any("description evidence drifted" in issue.message for issue in description_drift.issues):
            failures.append("description evidence drift was accepted")

        stale = evaluate(findings[:-1], "ratified", rulings, [], repo)
        if len(stale.stale) != 1:
            failures.append("deleted site did not make its ruling stale")

        unreviewed = evaluate(findings, "unreviewed", rulings, [], repo)
        if not any("not owner-ratified" in issue.message for issue in unreviewed.issues):
            failures.append("unreviewed template was accepted")

        assert_finding = next(finding for finding in findings if finding.site_class == "runtime-assertion")
        assert_rows = [row for row in rulings if _ruling_identity(row) != assert_finding.identity]
        assert_rows.append(_fixture_ruling(assert_finding, disposition="runtime-assertion", repair_phase=""))
        inadequate = evaluate(findings, "ratified", assert_rows, [], repo)
        if not any("inadequate error/assertion" in issue.message for issue in inadequate.issues):
            failures.append("inadequate assertion description passed without a repair phase")

        binary = repo / "retained.exe"
        binary.write_bytes(_fixture_pe("WinPixEventRuntime.dll"))
        imported = "winpixeventruntime.dll"
        if _pe_import_names(binary.read_bytes()) != {imported}:
            failures.append("bounded PE import parser did not recover the fixture import")
        delay_imported = "dxcompiler.dll"
        delay_binary = _fixture_pe("dxcompiler.dll", delay_import=True)
        if _pe_import_names(delay_binary) != {delay_imported}:
            failures.append("bounded PE delay-import parser did not recover the fixture import")
        fingerprint = _sha256_bytes(binary.read_bytes() + b"\0" + imported.encode("ascii"))
        bundle_finding = Finding(
            path="Agentic/Plans/Artifacts/fixture/retained.exe",
            line=1,
            column=1,
            site_class="bundle-import-mismatch",
            operation=f"retained-bundle-import:{imported}",
            source_fingerprint=fingerprint,
            description_classification="not-applicable",
            description="",
            evidence="imports runtime; sibling is absent",
        )
        bundle_row = _suggest_ruling(bundle_finding)
        if bundle_row["repair_phase"] != "E5" or bundle_row["disposition"] != "repair":
            failures.append("retained import mismatch was not assigned to E5 repair")

        artifact_root = repo / RETAINED_ARTIFACT_ROOT
        normal_dir = artifact_root / "normal"
        delay_dir = artifact_root / "delay"
        present_dir = artifact_root / "present"
        normal_dir.mkdir(parents=True)
        delay_dir.mkdir(parents=True)
        present_dir.mkdir(parents=True)
        (normal_dir / "normal.exe").write_bytes(_fixture_pe("WinPixEventRuntime.dll"))
        (delay_dir / "delay.exe").write_bytes(delay_binary)
        (present_dir / "present.exe").write_bytes(_fixture_pe("dxil.dll"))
        (present_dir / "dxil.dll").write_bytes(b"fixture runtime")
        subprocess.run(["git", "init", "-q"], cwd=repo, check=True, capture_output=True)
        subprocess.run(
            ["git", "add", "--", _normalize_path(artifact_root.relative_to(repo))],
            cwd=repo,
            check=True,
            capture_output=True,
        )
        directory_findings = _scan_bundle_mismatches(repo)
        directory_operations = {(finding.path, finding.operation) for finding in directory_findings}
        expected_directory_operations = {
            (
                "Agentic/Plans/Artifacts/normal/normal.exe",
                "retained-bundle-import:winpixeventruntime.dll",
            ),
            ("Agentic/Plans/Artifacts/delay/delay.exe", "retained-bundle-import:dxcompiler.dll"),
        }
        if directory_operations != expected_directory_operations:
            failures.append("end-to-end retained bundle-directory discovery did not preserve missing/present imports")

    if failures:
        for failure in failures:
            print(f"SELF_TEST_FAIL: {failure}", file=sys.stderr)
        return 1
    print(
        "SELF_TEST_PASS: site classes, ignored/handled CRT I/O, negative classification controls, "
        "owner-adjudication isolation, stale/currentness, and normal/delay PE bundle discovery are enforced."
    )
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=Path("."))
    parser.add_argument("--rulings", type=Path, default=DEFAULT_RULINGS_PATH)
    parser.add_argument("--format", choices=("text", "json"), default="text")
    parser.add_argument("--strict", action="store_true")
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument(
        "--write-unreviewed-template",
        type=Path,
        help="write a non-overwriting ruling template that strict mode rejects until owner review",
    )
    args = parser.parse_args()

    if args.self_test:
        if args.strict or args.write_unreviewed_template is not None or args.format != "text" or args.repo != Path(".") or args.rulings != DEFAULT_RULINGS_PATH:
            parser.error("--self-test cannot be combined with repository, ruling, output, strict, or template modes")
        return self_test()

    repo = args.repo.resolve()
    scan = scan_repository(repo)
    if args.write_unreviewed_template is not None:
        output = args.write_unreviewed_template
        output = output if output.is_absolute() else repo / output
        try:
            write_unreviewed_template(output, scan.findings)
        except (OSError, FileExistsError) as error:
            print(f"ERROR: {error}", file=sys.stderr)
            return 1
        print(
            f"WROTE_UNREVIEWED_TEMPLATE path={_normalize_path(output)} findings={len(scan.findings)} "
            f"inventory_sha256={_inventory_digest(scan.findings)}"
        )
        return 1 if scan.diagnostics else 0

    rulings_path = args.rulings if args.rulings.is_absolute() else repo / args.rulings
    review_status, rulings, load_issues = load_rulings(rulings_path)
    evaluation = evaluate(scan.findings, review_status, rulings, load_issues, repo)
    payload = _payload(scan, evaluation, rulings)
    if args.format == "json":
        print(json.dumps(payload, indent=2, sort_keys=True))
    else:
        print(_render_text(payload))

    if args.strict and (scan.diagnostics or evaluation.issues or evaluation.unruled or evaluation.stale):
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
