#!/usr/bin/env python3
"""
File: inventory_wide_signatures.py
Purpose:
  Inventory wide C++ function, method, and constructor signatures in tracked
  SkullbonezSource files without requiring a compiler database.

Summary:
  A comment/literal-masked balanced-token pass finds declaration-shaped
  parentheses, counts top-level parameters, groups matching declarations and
  definitions, and reports matching-arity lexical call sites. Operations at or
  above the configured review trigger must match a current owner ruling by file
  and normalized signature; prior report dispositions remain historical context
  only and never satisfy the gate.

Glossary:
  Declaration-shaped: A parenthesized form whose prefix and suffix look like a
    C++ declaration or definition rather than a call expression.
  Owner ruling: Current, reviewable judgement that either names the cohesive
    operation owner or routes the signature to an active repair plan.
  Review trigger: Arity at which a current qualitative ruling becomes mandatory;
    it is not an accepted maximum or an automatic defect.
  Owner borrow: Reference or pointer parameter whose type name denotes a live
    engine owner such as an Engine, Store, Controller, World, or Renderer.
  Matching-arity call: A lexical call-shaped occurrence with the same final
    name and top-level argument count; overload resolution is intentionally not
    claimed without a compiler database.

Invariants:
  - Comments, literals, raw strings, and preprocessor directives cannot create
    inventory rows or commas.
  - A changed, added, or removed review-trigger signature invalidates the ruling
    set instead of inheriting a same-name historical disposition.
  - The review trigger starts qualitative owner review; it is not a maximum
    arity, count allowance, or automatic defect.
  - The script is read-only unless an explicit --output path is supplied.
  - Results name lexical uncertainty instead of claiming semantic resolution.

Related:
  - AGENTS.md
"""

from __future__ import annotations

import argparse
import collections
import csv
import io
import json
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


SOURCE_SUFFIXES = {".cpp", ".h", ".hpp", ".inl"}
DEFAULT_RULINGS_PATH = Path("tools/wide_signature_ownership_rulings.json")
RULING_DISPOSITIONS = {"retain-owner", "repair-plan"}
CONTROL_NAMES = {
    "alignas",
    "catch",
    "decltype",
    "for",
    "if",
    "noexcept",
    "requires",
    "sizeof",
    "static_assert",
    "switch",
    "typeid",
    "while",
}
OWNER_TYPE_WORDS = re.compile(
    r"(?:AssetSystem|CameraCollection|Controller|Coordinator|Engine|Host|Manager|Owner|Pipeline|"
    r"Recorder|Renderer|Runtime|SceneWorld|Service|Store|System|Timeline|Tools|WorkerPool|World)$"
)
QUALIFIED_NAME_RE = re.compile(r"(?P<name>(?:~?[A-Za-z_]\w*::)*~?[A-Za-z_]\w*)\s*$")
WORD_RE = re.compile(r"[A-Za-z_]\w*")


@dataclass(frozen=True)
class Candidate:
    file: str
    line: int
    qualified_name: str
    simple_name: str
    scope_hint: str
    kind: str
    arity: int
    parameters: tuple[str, ...]
    signature: str
    is_definition: bool
    # Lifetime: these offsets belong to the masked text used for this scan.
    # They support sibling read-only inventories and are not ruling identity.
    opening_paren: int = -1
    closing_paren: int = -1


def tracked_source_files(repo: Path) -> list[Path]:
    result = subprocess.run(
        ["git", "ls-files", "-z", "--", "SkullbonezSource"],
        cwd=repo,
        check=True,
        capture_output=True,
    )
    paths = result.stdout.decode("utf-8", errors="strict").split("\0")
    return [
        repo / path
        for path in paths
        if path and Path(path).suffix.lower() in SOURCE_SUFFIXES and (repo / path).is_file()
    ]


def _blank_range(chars: list[str], start: int, end: int) -> None:
    for index in range(start, end):
        if chars[index] not in "\r\n":
            chars[index] = " "


def mask_cpp(text: str, preserve_literal_argument: bool = False) -> str:
    """Blank non-code regions while preserving offsets and line breaks.

    A caller that counts call arity may retain one neutral token per literal;
    literal contents stay blank, so commas and parentheses remain inert.
    """
    chars = list(text)
    length = len(text)
    index = 0
    line_start = True
    while index < length:
        if line_start:
            probe = index
            while probe < length and text[probe] in " \t":
                probe += 1
            if probe < length and text[probe] == "#":
                end = probe
                while end < length:
                    newline = text.find("\n", end)
                    if newline < 0:
                        end = length
                        break
                    back = newline - 1
                    while back >= probe and text[back] == "\r":
                        back -= 1
                    end = newline + 1
                    if back < probe or text[back] != "\\":
                        break
                _blank_range(chars, index, end)
                index = end
                line_start = True
                continue

        if text.startswith("//", index):
            end = text.find("\n", index + 2)
            end = length if end < 0 else end
            _blank_range(chars, index, end)
            index = end
            continue
        if text.startswith("/*", index):
            end = text.find("*/", index + 2)
            end = length if end < 0 else end + 2
            _blank_range(chars, index, end)
            index = end
            continue

        raw_match = re.match(r'(?:u8|u|U|L)?R"([^ ()\\\t\r\n]{0,16})\(', text[index:])
        if raw_match:
            delimiter = raw_match.group(1)
            close_marker = ")" + delimiter + '"'
            end = text.find(close_marker, index + raw_match.end())
            end = length if end < 0 else end + len(close_marker)
            _blank_range(chars, index, end)
            if preserve_literal_argument:
                chars[index] = "0"
            index = end
            continue

        quote_start = re.match(r"(?:u8|u|U|L)?(['\"])", text[index:])
        if quote_start:
            quote = quote_start.group(1)
            end = index + quote_start.end()
            while end < length:
                if text[end] == "\\":
                    end += 2
                    continue
                if text[end] == quote:
                    end += 1
                    break
                end += 1
            _blank_range(chars, index, min(end, length))
            if preserve_literal_argument:
                chars[index] = "0"
            index = min(end, length)
            continue

        line_start = text[index] == "\n"
        index += 1
    return "".join(chars)


def matching_pairs(masked: str, opening: str, closing: str) -> dict[int, int]:
    stack: list[int] = []
    pairs: dict[int, int] = {}
    for index, char in enumerate(masked):
        if char == opening:
            stack.append(index)
        elif char == closing and stack:
            start = stack.pop()
            pairs[start] = index
    return pairs


def class_ranges(masked: str, brace_pairs: dict[int, int]) -> list[tuple[int, int, str]]:
    ranges: list[tuple[int, int, str]] = []
    pattern = re.compile(r"\b(?:class|struct)\s+(?:\[\[[^\]]*\]\]\s*)?(?P<name>[A-Za-z_]\w*)")
    for match in pattern.finditer(masked):
        brace = masked.find("{", match.end())
        semicolon = masked.find(";", match.end())
        if brace < 0 or (semicolon >= 0 and semicolon < brace) or brace not in brace_pairs:
            continue
        ranges.append((brace, brace_pairs[brace], match.group("name")))
    return ranges


def enclosing_class(position: int, ranges: Iterable[tuple[int, int, str]]) -> str | None:
    matches = [(end - start, name) for start, end, name in ranges if start < position < end]
    return min(matches)[1] if matches else None


def _angle_opens(fragment: str, index: int) -> bool:
    before = fragment[:index].rstrip()
    after = fragment[index + 1 :].lstrip()
    return bool(before and after and (before[-1].isalnum() or before[-1] in "_>:]") and
                (after[0].isalnum() or after[0] in "_:"))


def split_top_level(fragment: str) -> list[str]:
    if not fragment.strip() or fragment.strip() == "void":
        return []
    parts: list[str] = []
    start = 0
    paren = bracket = brace = angle = 0
    index = 0
    while index < len(fragment):
        char = fragment[index]
        if char == "(":
            paren += 1
        elif char == ")" and paren:
            paren -= 1
        elif char == "[":
            bracket += 1
        elif char == "]" and bracket:
            bracket -= 1
        elif char == "{":
            brace += 1
        elif char == "}" and brace:
            brace -= 1
        elif char == "<" and _angle_opens(fragment, index):
            angle += 1
        elif char == ">" and angle:
            angle -= 1
        elif char == "," and paren == bracket == brace == angle == 0:
            parts.append(fragment[start:index].strip())
            start = index + 1
        index += 1
    parts.append(fragment[start:].strip())
    return [part for part in parts if part]


def strip_top_level_default(parameter: str) -> str:
    paren = bracket = brace = angle = 0
    for index, char in enumerate(parameter):
        if char == "(":
            paren += 1
        elif char == ")" and paren:
            paren -= 1
        elif char == "[":
            bracket += 1
        elif char == "]" and bracket:
            bracket -= 1
        elif char == "{":
            brace += 1
        elif char == "}" and brace:
            brace -= 1
        elif char == "<" and _angle_opens(parameter, index):
            angle += 1
        elif char == ">" and angle:
            angle -= 1
        elif char == "=" and paren == bracket == brace == angle == 0:
            return parameter[:index].strip()
    return parameter.strip()


def parameter_identity(parameter: str) -> str:
    parameter = strip_top_level_default(parameter)
    function_pointer = re.search(r"[(*&]\s*([A-Za-z_]\w*)\s*\)", parameter)
    if function_pointer:
        return function_pointer.group(1)
    words = list(WORD_RE.finditer(parameter))
    if not words:
        return normalize_space(parameter)
    final = words[-1]
    final_word = final.group(0)
    type_only_words = {
        "auto", "bool", "char", "double", "float", "int", "long", "short", "signed", "unsigned", "void",
        "const", "volatile", "size_t", "uint8_t", "uint16_t", "uint32_t", "uint64_t", "int8_t", "int16_t",
        "int32_t", "int64_t",
    }
    before = parameter[: final.start()].rstrip()
    if final_word in type_only_words or before.endswith("::") or parameter.rstrip().endswith(("*", "&", ">")):
        return normalize_space(parameter)
    return final_word


def parameter_type_identity(parameter: str) -> str:
    parameter = strip_top_level_default(parameter)
    function_pointer = re.search(r"([(*&]\s*)([A-Za-z_]\w*)(\s*\))", parameter)
    if function_pointer:
        parameter = parameter[: function_pointer.start(2)] + parameter[function_pointer.end(2) :]
    else:
        identity = parameter_identity(parameter)
        words = list(WORD_RE.finditer(parameter))
        if words and identity == words[-1].group(0):
            final = words[-1]
            parameter = parameter[: final.start()] + parameter[final.end() :]
    parameter = re.sub(r"(?:\b[A-Za-z_]\w*::)+", "", parameter)
    return normalize_space(parameter)


def normalize_space(text: str) -> str:
    return re.sub(r"\s+", " ", text).strip()


def qualified_name_before(masked: str, opening: int) -> tuple[str, int] | None:
    end = opening
    while end > 0 and masked[end - 1].isspace():
        end -= 1
    start = end
    while start > 0 and (masked[start - 1].isalnum() or masked[start - 1] in "_:~"):
        start -= 1
    fragment = masked[start:end]
    match = QUALIFIED_NAME_RE.fullmatch(fragment)
    return (match.group("name"), start) if match else None


def statement_start(masked: str, name_start: int) -> int:
    previous = max(masked.rfind(";", 0, name_start), masked.rfind("{", 0, name_start), masked.rfind("}", 0, name_start))
    return previous + 1


def declaration_suffix(masked: str, close: int, paren_pairs: dict[int, int]) -> tuple[bool, bool]:
    """Return (declaration-shaped, definition)."""
    index = close + 1
    length = len(masked)
    while index < length:
        while index < length and masked[index].isspace():
            index += 1
        if index >= length:
            return False, False
        if masked.startswith("[[", index):
            end = masked.find("]]", index + 2)
            if end < 0:
                return False, False
            index = end + 2
            continue
        word = WORD_RE.match(masked, index)
        if word and word.group(0) in {"const", "constexpr", "final", "mutable", "noexcept", "override", "requires", "volatile"}:
            index = word.end()
            if word.group(0) == "noexcept":
                while index < length and masked[index].isspace():
                    index += 1
                if index in paren_pairs:
                    index = paren_pairs[index] + 1
            continue
        if masked.startswith("&&", index):
            index += 2
            continue
        if masked[index] == "&":
            index += 1
            continue
        if masked.startswith("->", index):
            index += 2
            while index < length and masked[index] not in "{;=:":
                index += 1
            continue
        if masked[index] == "{":
            return True, True
        if masked[index] == ";":
            return True, False
        if masked[index] == "=":
            return True, False
        if masked[index] == ":":
            return True, True
        return False, False
    return False, False


def declaration_prefix(masked: str, name_start: int, qualified_name: str, class_name: str | None) -> tuple[bool, str]:
    start = statement_start(masked, name_start)
    prefix = masked[start:name_start]
    compact = normalize_space(prefix)
    compact = re.sub(r"^(?:public|private|protected)\s*:\s*", "", compact)
    if not compact:
        return bool(class_name and qualified_name.lstrip("~") == class_name), ""
    if re.search(r"\b(?:break|case|co_return|continue|delete|else|goto|new|return|throw)\b", compact):
        return False, compact
    if re.search(r"(?<!:):(?!:)", compact):
        return False, compact
    if any(token in compact for token in ("=", ".", "->", "?")):
        return False, compact
    if "(" in compact or ")" in compact or "," in compact:
        return False, compact
    words = WORD_RE.findall(compact)
    if not words:
        return "::" in qualified_name, compact
    if words[-1] in CONTROL_NAMES:
        return False, compact
    return True, compact


def classify_parameters(parameters: tuple[str, ...]) -> str:
    counts = {"owner-borrow": 0, "value": 0, "flag": 0}
    for raw in parameters:
        parameter = strip_top_level_default(raw)
        if re.search(r"\bbool\b", parameter):
            counts["flag"] += 1
            continue
        type_words = WORD_RE.findall(parameter)
        borrowed = "&" in parameter or "*" in parameter
        owner_word = any(OWNER_TYPE_WORDS.search(word) for word in type_words)
        counts["owner-borrow" if borrowed and owner_word else "value"] += 1
    return ", ".join(f"{name}={count}" for name, count in counts.items())


def parameters_look_declarative(parameters: tuple[str, ...]) -> bool:
    """Reject constructor-style value initializers without rejecting unnamed parameter types."""
    if not parameters:
        return True
    expression_count = 0
    for raw in parameters:
        parameter = strip_top_level_default(raw).strip()
        words = WORD_RE.findall(parameter)
        numeric_only = not words and bool(re.search(r"\d", parameter))
        member_expression = "." in parameter or "->" in parameter
        arithmetic_expression = bool(re.search(r"(?:\+|/|%|(?<!-)-(?!>))", parameter))
        if numeric_only or member_expression or arithmetic_expression:
            expression_count += 1
    # Why: `Type value(a, b, ...)` has the same prefix/suffix shape as a
    # function declaration. A true parameter list can contain an expression
    # (for example an array extent), but a list made entirely of value
    # expressions is an initializer and must not enter the inventory.
    return expression_count != len(parameters)


def scan_file(
    path: Path,
    repo: Path,
    *,
    text: str | None = None,
    masked: str | None = None,
) -> tuple[list[Candidate], dict[tuple[str, int], int]]:
    """Scan one file, optionally reusing a caller's matching text and mask."""
    if masked is not None and text is None:
        raise ValueError("a supplied mask requires its source text")
    text = text if text is not None else path.read_text(encoding="utf-8", errors="strict")
    masked = masked if masked is not None else mask_cpp(text)
    if len(masked) != len(text):
        raise ValueError("masked text must preserve source offsets")
    paren_pairs = matching_pairs(masked, "(", ")")
    brace_pairs = matching_pairs(masked, "{", "}")
    classes = class_ranges(masked, brace_pairs)
    declarations: list[Candidate] = []
    declaration_opens: set[int] = set()

    for opening, closing in sorted(paren_pairs.items()):
        name_result = qualified_name_before(masked, opening)
        if not name_result:
            continue
        qualified_name, name_start = name_result
        simple_name = qualified_name.split("::")[-1].lstrip("~")
        if simple_name in CONTROL_NAMES:
            continue
        parameters = tuple(split_top_level(masked[opening + 1 : closing]))
        arity = len(parameters)
        shaped, is_definition = declaration_suffix(masked, closing, paren_pairs)
        if not shaped:
            continue
        if not parameters_look_declarative(parameters):
            continue
        class_name = enclosing_class(opening, classes)
        prefix_ok, prefix = declaration_prefix(masked, name_start, qualified_name, class_name)
        if not prefix_ok:
            continue
        raw_name_parts = qualified_name.split("::")
        scope_hint = class_name or (raw_name_parts[-2] if len(raw_name_parts) >= 2 else "")
        if class_name and "::" not in qualified_name:
            qualified_name = f"{class_name}::{qualified_name}"
        declaration_opens.add(opening)
        kind = "function"
        if simple_name == (class_name or "") or qualified_name.split("::")[-2:-1] == [simple_name]:
            kind = "constructor"
        elif class_name or "::" in qualified_name:
            kind = "method"
        raw_signature = f"{prefix} {qualified_name}({masked[opening + 1:closing]})"
        declarations.append(
            Candidate(
                file=str(path.relative_to(repo)).replace("\\", "/"),
                line=text.count("\n", 0, name_start) + 1,
                qualified_name=qualified_name,
                simple_name=simple_name,
                scope_hint=scope_hint,
                kind=kind,
                arity=arity,
                parameters=tuple(normalize_space(parameter) for parameter in parameters),
                signature=normalize_space(raw_signature),
                is_definition=is_definition,
                opening_paren=opening,
                closing_paren=closing,
            )
        )

    calls: dict[tuple[str, int], int] = {}
    for opening, closing in sorted(paren_pairs.items()):
        if opening in declaration_opens:
            continue
        name_result = qualified_name_before(masked, opening)
        if not name_result:
            continue
        simple_name = name_result[0].split("::")[-1].lstrip("~")
        if simple_name in CONTROL_NAMES:
            continue
        arity = len(split_top_level(masked[opening + 1 : closing]))
        key = (simple_name, arity)
        calls[key] = calls.get(key, 0) + 1
    return declarations, calls


def load_prior_dispositions(report: Path | None) -> dict[str, str]:
    if report is None or not report.exists():
        return {}
    text = report.read_text(encoding="utf-8", errors="replace")
    dispositions: dict[str, str] = {}

    inventory_match = re.search(r"## Inventory\s+(.*?)(?=\n## )", text, flags=re.DOTALL)
    if inventory_match:
        for line in inventory_match.group(1).splitlines():
            cells = [cell.strip() for cell in line.strip().strip("|").split("|")]
            if len(cells) >= 5 and cells[0].startswith("`"):
                dispositions[cells[0].strip("`")] = cells[-1]

    survivor_match = re.search(
        r"\| Surviving invoked name .*?\n\|---.*?\n(.*?)(?=\nFinal proof:)",
        text,
        flags=re.DOTALL,
    )
    if survivor_match:
        for line in survivor_match.group(1).splitlines():
            cells = [cell.strip() for cell in line.strip().strip("|").split("|")]
            if len(cells) >= 3 and cells[0].startswith("`"):
                dispositions[cells[0].strip("`")] = cells[-1]
    return dispositions


def prior_disposition_for(candidate: Candidate, prior: dict[str, str], allow_simple_name: bool) -> str:
    exact = prior.get(candidate.qualified_name)
    if exact:
        return exact
    qualified_matches = [
        disposition
        for name, disposition in prior.items()
        if "::" in name and candidate.qualified_name.endswith(name)
    ]
    if len(qualified_matches) == 1:
        return qualified_matches[0]
    if candidate.qualified_name == candidate.simple_name or allow_simple_name:
        return prior.get(candidate.simple_name, "none")
    return "none"


def load_owner_rulings(path: Path) -> tuple[int, dict[tuple[str, str], dict[str, str]]]:
    """Load exact current-signature rulings and reject ambiguous policy data."""
    payload = json.loads(path.read_text(encoding="utf-8", errors="strict"))
    trigger = payload.get("review_trigger_arity")
    if not isinstance(trigger, int) or isinstance(trigger, bool) or trigger < 1:
        raise ValueError("review_trigger_arity must be a positive integer")
    raw_rulings = payload.get("rulings")
    if not isinstance(raw_rulings, list):
        raise ValueError("rulings must be an array")

    rulings: dict[tuple[str, str], dict[str, str]] = {}
    required_fields = ("file", "signature", "owner", "disposition", "reason", "evidence")
    for index, raw in enumerate(raw_rulings):
        if not isinstance(raw, dict):
            raise ValueError(f"rulings[{index}] must be an object")
        values: dict[str, str] = {}
        for field in required_fields:
            value = raw.get(field)
            if not isinstance(value, str) or not value.strip():
                raise ValueError(f"rulings[{index}].{field} must be a non-empty string")
            values[field] = value.strip()
        disposition = values["disposition"]
        if disposition not in RULING_DISPOSITIONS:
            allowed = ", ".join(sorted(RULING_DISPOSITIONS))
            raise ValueError(f"rulings[{index}].disposition must be one of: {allowed}")
        plan = raw.get("plan", "")
        if not isinstance(plan, str):
            raise ValueError(f"rulings[{index}].plan must be a string when present")
        values["plan"] = plan.strip()
        if disposition == "repair-plan" and not values["plan"]:
            raise ValueError(f"rulings[{index}] repair-plan disposition requires plan")
        # Invariant: file plus normalized signature is the ruling identity.
        # A rename, move, or parameter change therefore cannot inherit approval.
        key = (values["file"], values["signature"])
        if key in rulings:
            raise ValueError(f"duplicate ruling for {values['file']}: {values['signature']}")
        rulings[key] = values
    return trigger, rulings


def apply_owner_rulings(
    rows: list[dict[str, object]],
    review_trigger: int,
    rulings: dict[tuple[str, str], dict[str, str]],
) -> list[str]:
    """Annotate inventory rows and return blocking currentness diagnostics."""
    current_keys: set[tuple[str, str]] = set()
    diagnostics: list[str] = []
    for row in rows:
        triggered = int(row["arity"]) >= review_trigger
        row["review_triggered"] = triggered
        if not triggered:
            row["ruling_status"] = "NOT-TRIGGERED"
            row["ruling_disposition"] = ""
            row["ruling_owner"] = ""
            row["ruling_reason"] = ""
            row["ruling_evidence"] = ""
            row["ruling_plan"] = ""
            continue

        key = (str(row["file"]), str(row["signature"]))
        current_keys.add(key)
        ruling = rulings.get(key)
        if ruling is None:
            row["ruling_status"] = "UNRULED"
            row["ruling_disposition"] = ""
            row["ruling_owner"] = ""
            row["ruling_reason"] = ""
            row["ruling_evidence"] = ""
            row["ruling_plan"] = ""
            diagnostics.append(f"UNRULED {key[0]}: {key[1]}")
            continue

        row["ruling_status"] = "RULED"
        row["ruling_disposition"] = ruling["disposition"]
        row["ruling_owner"] = ruling["owner"]
        row["ruling_reason"] = ruling["reason"]
        row["ruling_evidence"] = ruling["evidence"]
        row["ruling_plan"] = ruling["plan"]

    # Why: stale entries would otherwise make the file look complete while a
    # repair silently narrowed, moved, or deleted the operation it judged.
    for file_name, signature in sorted(set(rulings) - current_keys):
        diagnostics.append(f"STALE-RULING {file_name}: {signature}")
    return diagnostics


def group_candidates(candidates: Iterable[Candidate]) -> list[tuple[Candidate, tuple[Candidate, ...]]]:
    groups: dict[tuple[str, str, int, tuple[str, ...]], list[Candidate]] = {}
    for candidate in candidates:
        identities = tuple(parameter_type_identity(parameter) for parameter in candidate.parameters)
        key = (candidate.scope_hint, candidate.simple_name, candidate.arity, identities)
        groups.setdefault(key, []).append(candidate)

    # A namespace-scope definition may spell its qualifier while its declaration
    # appears unqualified inside the namespace block. Merge the unscoped form
    # only when one scoped candidate has the same name, arity, and parameter
    # identities; ambiguity remains as separate rows rather than being guessed.
    for key in list(groups):
        scope, simple_name, arity, identities = key
        if scope:
            continue
        matches = [
            candidate_key
            for candidate_key in groups
            if candidate_key[0] and candidate_key[1:] == (simple_name, arity, identities)
        ]
        if len(matches) == 1:
            groups[matches[0]].extend(groups.pop(key))
    selected: list[tuple[Candidate, tuple[Candidate, ...]]] = []
    for group in groups.values():
        ordered = tuple(sorted(group, key=lambda row: (not row.is_definition, row.file, row.line)))
        selected.append((ordered[0], ordered))
    return sorted(selected, key=lambda item: (-item[0].arity, item[0].qualified_name.lower(), item[0].file, item[0].line))


def inventory(repo: Path, threshold: int, prior_report: Path | None) -> list[dict[str, object]]:
    candidates: list[Candidate] = []
    calls: dict[tuple[str, int], int] = {}
    for path in tracked_source_files(repo):
        file_candidates, file_calls = scan_file(path, repo)
        candidates.extend(file_candidates)
        for key, count in file_calls.items():
            calls[key] = calls.get(key, 0) + count

    prior = load_prior_dispositions(prior_report)
    grouped = group_candidates(candidates)
    simple_name_counts = collections.Counter(candidate.simple_name for candidate, _ in grouped)
    rows: list[dict[str, object]] = []
    for candidate, group in grouped:
        if candidate.arity < threshold:
            continue
        disposition = prior_disposition_for(candidate, prior, simple_name_counts[candidate.simple_name] == 1)
        rows.append(
            {
                "signature": candidate.signature,
                "name": candidate.qualified_name,
                "kind": candidate.kind,
                "file": candidate.file,
                "line": candidate.line,
                "sites": [f"{item.file}:{item.line}" for item in group],
                "arity": candidate.arity,
                "parameter_kinds": classify_parameters(candidate.parameters),
                "lexical_matching_arity_calls": calls.get((candidate.simple_name, candidate.arity), 0),
                "prior_disposition": disposition,
            }
        )
    return rows


def markdown(rows: list[dict[str, object]]) -> str:
    output = [
        "| Signature | Definition/declaration | Arity | Parameter kinds | Matching-arity calls | Current ruling | Owner reason |",
        "|---|---|---:|---|---:|---|---|",
    ]
    for row in rows:
        signature = str(row["signature"]).replace("|", "\\|")
        status = str(row["ruling_status"])
        disposition = str(row["ruling_disposition"])
        owner = str(row["ruling_owner"])
        if status == "RULED":
            ruling = f"`{disposition}` — {owner}"
        else:
            ruling = f"`{status}`"
        reason = str(row["ruling_reason"]).replace("|", "\\|")
        sites = "<br>".join(f"`{site}`" for site in row["sites"])
        output.append(
            f"| `{signature}` | {sites} ({row['kind']}) | {row['arity']} | "
            f"{row['parameter_kinds']} | {row['lexical_matching_arity_calls']} | {ruling} | {reason} |"
        )
    return "\n".join(output) + "\n"


def csv_text(rows: list[dict[str, object]]) -> str:
    stream = io.StringIO(newline="")
    if rows:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0].keys()), lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)
    return stream.getvalue()


W1_DEFECT_RULES = (
    {
        "files": (
            "SkullbonezSource/Runtime/UI/GameUI/UI.cpp",
            "SkullbonezSource/Runtime/UI/GameUI/UIWindowInteractionOwner.cpp",
        ),
        "names": ("InGameUI::UpdateInput", "UIWindowInteractionOwner::UpdateInput"),
        "category": "missing-domain-value-record",
        "wave": "W2/UI-input",
        "reason": (
            "The same immutable device, viewport, editor, camera, and scene-selection snapshot crosses the UI facade "
            "and interaction owner; one GameUI-owned input-frame value has a real per-frame lifetime and one writer."
        ),
    },
    {
        "files": ("SkullbonezSource/Physics/PhysicsBodyStore.cpp", "SkullbonezSource/Physics/PhysicsEngine.cpp"),
        "names": ("PhysicsBodyStore::RestoreReplayBodyState", "PhysicsEngine::RestoreReplayBodyState"),
        "category": "missing-domain-value-record",
        "wave": "W2/physics-restore",
        "reason": (
            "One Physics-owned pose, velocity, mass, and inertia restore value is forwarded unchanged through the "
            "engine facade into the body store; the value is not a Replay type and grants no owner authority."
        ),
    },
    {
        "files": ("SkullbonezSource/Rendering/DX12/RenderBackendDX12.DynamicGeometry.cpp", "SkullbonezSource/Rendering/DX12/RenderBackendDX12.h"),
        "names": ("Dx12GeometryOwner::CreateInstancedMesh",),
        "category": "missing-domain-value-record",
        "wave": "W3/DX12-mesh",
        "reason": (
            "Static layout and upload-location values repeat across the public and native DX12 overload boundary; "
            "named mesh-layout and upload records express those two lifetimes without borrowing another owner."
        ),
    },
    {
        "files": ("SkullbonezSource/Rendering/DX12/RenderBackendDX12.Textures.cpp", "SkullbonezSource/Rendering/DX12/RenderBackendDX12.h"),
        "names": ("Dx12TextureOwner::CreateTexture2D",),
        "category": "flag-policy-value",
        "wave": "W3/DX12-texture",
        "reason": (
            "Mip generation and filtering are creation policy while pixel data and extent are one upload value; "
            "typed policy plus a texture-upload record removes positional booleans and preserves cold ownership."
        ),
    },
    {
        "files": ("SkullbonezSource/Rendering/RenderCommandTypes.h",),
        "names": ("MakePassRasterStateBucket",),
        "category": "flag-policy-value",
        "wave": "W3/DX12-raster",
        "reason": (
            "RasterStateDesc already owns depth-test, depth-write, and blend policy; the helper should accept that "
            "domain value instead of reconstructing it from three positional booleans."
        ),
    },
    {
        "files": (
            "SkullbonezSource/Runtime/Diagnostics/DiagnosticsRuntime.cpp",
            "SkullbonezSource/Runtime/Diagnostics/RuntimeDiagnostics.cpp",
        ),
        "names": (
            "DiagnosticsRuntime::LogReplayRestoreResult",
            "RuntimeDiagnostics::LogReplayRestoreResult",
            "DiagnosticsRuntime::LogReplayScrubProbe",
            "RuntimeDiagnostics::LogReplayScrubProbe",
            "DiagnosticsRuntime::LogReplayRestoreProbe",
            "RuntimeDiagnostics::LogReplayRestoreProbe",
        ),
        "category": "missing-domain-value-record",
        "wave": "W3/replay-diagnostics",
        "reason": (
            "Each complete diagnostic schema is forwarded field-for-field through two logging layers. Converting both "
            "layers together is new evidence beyond the prior ruling and gives each bounded log row one value lifetime."
        ),
    },
)


W1_FAMILY_REASONS = {
    "Assets": "Cold asset decode publishes one slot transaction; explicit source, extent, channel, and decode policy values stay locally readable.",
    "Maths": "The 3x3 scalar constructor is the canonical fixed matrix shape; aggregating nine ordered cells adds no domain boundary.",
    "Physics": "Physics hot-path and stage operations keep compact stores, spans, scalar policy, and outputs explicit; wrapping them would create a hot-path bag or hide mutation authority.",
    "Rendering": "The concrete render command or cold resource boundary records one fixed GPU operation; explicit resources, geometry, and raster values preserve binding and lifetime visibility.",
    "Runtime": "The top-level runtime, replay, validation, or stress transaction sequences exact owners synchronously; a wrapper would be a multi-owner context bag with no independent value lifetime.",
    "Scene": "The parser transaction consumes one authored schema row and writes one scene-owned result; explicit path and component outputs preserve error attribution.",
    "UI": "The immediate UI draw/input transaction consumes explicit layout, color, hit-test, and widget values once; a broad widget argument record would merely rename local geometry.",
    "World": "Terrain classification or rendering is a bounded geometry transaction over explicit hot values; an aggregate would obscure units and branch inputs without gaining ownership.",
}


def w1_ruling(row: dict[str, object]) -> dict[str, str]:
    file_name = str(row["file"])
    name = str(row["name"])
    for rule in W1_DEFECT_RULES:
        if file_name in rule["files"] and name in rule["names"]:
            return {"category": str(rule["category"]), "wave": str(rule["wave"]), "reason": str(rule["reason"])}
    prior = str(row["prior_disposition"])
    if prior != "none":
        return {"category": "accepted-with-reason", "wave": "none", "reason": prior}
    area = file_name.split("/")[1]
    return {
        "category": "intentional-transaction-boundary",
        "wave": "none",
        "reason": W1_FAMILY_REASONS[area],
    }


def w1_markdown(rows: list[dict[str, object]]) -> str:
    rulings = [w1_ruling(row) for row in rows]
    category_counts = collections.Counter(ruling["category"] for ruling in rulings)
    wave_counts = collections.Counter(ruling["wave"] for ruling in rulings if ruling["wave"] != "none")
    output = [
        "# Wide Signature W1 Owner Rulings",
        "",
        "Date: 2026-07-23",
        "Owner: skullbonez",
        "Source inventory: generated from the current tree by this script.",
        "",
        "## Ratified Rubric",
        "",
        "| Disposition | Meaning |",
        "|---|---|",
        "| `intentional-transaction-boundary` | Explicit width preserves one synchronous operation, ordering, units, hot data, or exact owner authority. |",
        "| `missing-domain-value-record` | Parameters already travel as one cohesive value with a real owner and lifetime. |",
        "| `flag-policy-value` | Positional booleans are policy and should become a typed enum/value. |",
        "| `ownership-smell-routed` | Authority belongs to another owner/plan; cross-link instead of duplicating work. |",
        "| `accepted-with-reason` | A prior or row-specific reason proves width is load-bearing. |",
        "",
        "A descriptor is allowed only when it is a named domain value with one writer and no retained owner authority. "
        "No row is routed as an ownership smell: current evidence either identifies a bounded value/policy refactor in this plan "
        "or proves an explicit transaction boundary. The corrected scan removed two matrix initializers before these rulings.",
        "",
        "## Totals",
        "",
        "| Disposition | Rows |",
        "|---|---:|",
    ]
    for category in (
        "intentional-transaction-boundary",
        "missing-domain-value-record",
        "flag-policy-value",
        "ownership-smell-routed",
        "accepted-with-reason",
    ):
        output.append(f"| `{category}` | {category_counts.get(category, 0)} |")
    output.extend(["", "| Refactor wave/family | Rows |", "|---|---:|"])
    for wave, count in sorted(wave_counts.items()):
        output.append(f"| `{wave}` | {count} |")
    output.extend(
        [
            "",
            "## Row-Level Rulings",
            "",
            "Every corrected W0 row appears exactly once below. Matching-arity call counts remain lexical evidence, not deletion authority.",
            "",
            "| Signature | Sites | Arity | Calls | Disposition | Wave | Owner reason |",
            "|---|---|---:|---:|---|---|---|",
        ]
    )
    for row, ruling in zip(rows, rulings):
        signature = str(row["signature"]).replace("|", "\\|")
        sites = "<br>".join(f"`{site}`" for site in row["sites"])
        reason = ruling["reason"].replace("|", "\\|")
        output.append(
            f"| `{signature}` | {sites} | {row['arity']} | {row['lexical_matching_arity_calls']} | "
            f"`{ruling['category']}` | `{ruling['wave']}` | {reason} |"
        )
    return "\n".join(output) + "\n"


def self_test() -> None:
    sample = r'''
class Owner {
public:
    Owner(int a, int b, int c, int d, int e, int f, bool enabled);
    void Method(World& world, const Vector3& point, int a, int b, int c, bool x, bool y);
};
void Free(int a, int b, int c, int d, int e, int f, int g) { Method(a, b, c, d, e, f, g); }
// void Fake(int a, int b, int c, int d, int e, int f, int g);
const char* literal = "Fake(1,2,3,4,5,6,7)";
'''
    masked = mask_cpp(sample)
    assert "Fake" not in masked
    assert len(split_top_level("int a, std::array<int, 2> b, bool c")) == 3
    pairs = matching_pairs(masked, "(", ")")
    assert pairs
    assert classify_parameters(("World& world", "const Vector3& point", "bool enabled")) == (
        "owner-borrow=1, value=1, flag=1"
    )
    assert parameters_look_declarative(("float", "float", "bool enabled"))
    assert not parameters_look_declarative(("1.0f", "0.0f", "-2.0f"))
    assert not parameters_look_declarative(("v.x + a", "v.y - b", "v.z / scale"))
    defect_row = {
        "file": "SkullbonezSource/Physics/PhysicsEngine.cpp",
        "name": "PhysicsEngine::RestoreReplayBodyState",
        "prior_disposition": "old accepted reason",
    }
    assert w1_ruling(defect_row)["wave"] == "W2/physics-restore"
    carried_row = {"file": "SkullbonezSource/Runtime/X.cpp", "name": "X", "prior_disposition": "carry"}
    assert w1_ruling(carried_row)["category"] == "accepted-with-reason"
    family_row = {"file": "SkullbonezSource/UI/X.cpp", "name": "X", "prior_disposition": "none"}
    assert w1_ruling(family_row)["category"] == "intentional-transaction-boundary"
    scoped = Candidate("sample.cpp", 1, "PhysicsEngine::Step", "Step", "PhysicsEngine", "method", 7, (), "", True)
    unique = Candidate("sample.cpp", 2, "ReplayAuthoring::Tick", "Tick", "ReplayAuthoring", "method", 7, (), "", True)
    prior = {"Step": "unrelated simple-name ruling", "Tick": "unique carried ruling"}
    assert prior_disposition_for(scoped, prior, allow_simple_name=False) == "none"
    assert prior_disposition_for(unique, prior, allow_simple_name=True) == "unique carried ruling"

    exact_signature = "void Exact(int a, int b, int c, int d, int e, int f, int g, int h, int i, int j, int k, int l)"
    over_signature = exact_signature.replace("Exact", "Over").replace(")", ", int m)")
    sample_rows: list[dict[str, object]] = [
        {"file": "sample.cpp", "signature": exact_signature, "arity": 12},
        {"file": "sample.cpp", "signature": over_signature, "arity": 13},
        {"file": "sample.cpp", "signature": "void Narrow(int a)", "arity": 1},
    ]
    sample_ruling = {
        "owner": "SampleOwner",
        "disposition": "retain-owner",
        "reason": "One synchronous sample operation.",
        "evidence": "sample evidence",
        "plan": "",
    }
    complete_rulings = {
        ("sample.cpp", exact_signature): sample_ruling,
        ("sample.cpp", over_signature): sample_ruling,
    }
    assert not apply_owner_rulings(sample_rows, 12, complete_rulings)
    assert sample_rows[0]["ruling_status"] == "RULED"
    assert sample_rows[1]["ruling_status"] == "RULED"
    assert sample_rows[2]["ruling_status"] == "NOT-TRIGGERED"

    unruled_rows = [{"file": "sample.cpp", "signature": exact_signature, "arity": 12}]
    assert apply_owner_rulings(unruled_rows, 12, {})
    changed_rows = [{"file": "sample.cpp", "signature": exact_signature + " const", "arity": 12}]
    changed_diagnostics = apply_owner_rulings(changed_rows, 12, complete_rulings)
    assert any(item.startswith("UNRULED") for item in changed_diagnostics)
    assert any(item.startswith("STALE-RULING") for item in changed_diagnostics)
    print("PASS: wide-signature inventory self-test")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=Path("."), help="Repository root (default: current directory)")
    parser.add_argument("--threshold", type=int, default=7, help="Minimum parameter count (default: 7)")
    parser.add_argument("--format", choices=("markdown", "json", "csv", "w1-markdown"), default="markdown")
    parser.add_argument("--output", type=Path, help="Optional output path; stdout is used otherwise")
    parser.add_argument(
        "--prior-report",
        type=Path,
        default=None,
        help="Optional prior Markdown inventory used to carry forward dispositions",
    )
    parser.add_argument(
        "--rulings",
        type=Path,
        default=DEFAULT_RULINGS_PATH,
        help="Current qualitative owner rulings (default: tools/wide_signature_ownership_rulings.json)",
    )
    parser.add_argument(
        "--strict",
        action="store_true",
        help="Fail when a review-trigger signature is unruled or a ruling is stale",
    )
    parser.add_argument("--self-test", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.self_test:
        self_test()
        return 0
    repo = args.repo.resolve()
    # No prior inventory is the normal case; dispositions then come only from
    # the rulings file. A relative path still resolves against the repository.
    if args.prior_report is None:
        prior_report = None
    elif args.prior_report.is_absolute():
        prior_report = args.prior_report
    else:
        prior_report = repo / args.prior_report
    rulings_path = args.rulings if args.rulings.is_absolute() else repo / args.rulings
    try:
        review_trigger, rulings = load_owner_rulings(rulings_path)
    except (OSError, json.JSONDecodeError, ValueError) as error:
        print(f"ERROR: invalid wide-signature rulings: {error}", file=sys.stderr)
        return 2
    rows = inventory(repo, min(args.threshold, review_trigger), prior_report)
    diagnostics = apply_owner_rulings(rows, review_trigger, rulings)
    rendered_rows = [row for row in rows if int(row["arity"]) >= args.threshold]
    if args.format == "json":
        rendered = json.dumps(rendered_rows, indent=2) + "\n"
    elif args.format == "csv":
        rendered = csv_text(rendered_rows)
    elif args.format == "w1-markdown":
        rendered = w1_markdown(rendered_rows)
    else:
        rendered = markdown(rendered_rows)
    if args.output:
        output = args.output if args.output.is_absolute() else repo / args.output
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(rendered, encoding="utf-8", newline="\n")
        print(
            f"PASS: wide-signature rows={len(rendered_rows)} threshold={args.threshold} "
            f"review-trigger={review_trigger}"
        )
    else:
        sys.stdout.write(rendered)
    if args.strict and diagnostics:
        for diagnostic in diagnostics:
            print(f"ERROR: {diagnostic}", file=sys.stderr)
        return 1
    if args.strict:
        print(
            f"PASS: all signatures at or above review trigger {review_trigger} have current owner rulings",
            file=sys.stderr,
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
