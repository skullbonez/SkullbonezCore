#!/usr/bin/env python3
"""Reject commits that omit the repository's detailed evidence notes."""

from __future__ import annotations

import argparse
import re
import tempfile
from pathlib import Path


REQUIRED_SECTIONS = ("Why", "Ownership", "What", "Validation", "Baselines/Artifacts", "Review")
MINIMUM_BODY_LENGTH = 320
MINIMUM_SECTION_LENGTH = 32


def _message_text(path: Path) -> str:
    lines = path.read_text(encoding="utf-8-sig").replace("\r\n", "\n").split("\n")
    return "\n".join(line for line in lines if not line.startswith("#")).rstrip()


def validate_message(path: Path) -> list[str]:
    content = _message_text(path)
    lines = content.split("\n")
    errors: list[str] = []
    if len(lines) < 3 or not lines[0].strip():
        return ["commit requires a subject and a substantive body"]
    if lines[1].strip():
        errors.append("subject must be followed by a blank line")

    body = "\n".join(lines[2:]).strip()
    if len(body) < MINIMUM_BODY_LENGTH:
        errors.append(
            f"body is too short to preserve rationale and evidence: length={len(body)} minimum={MINIMUM_BODY_LENGTH}"
        )

    values: dict[str, str] = {}
    previous_index = -1
    for section in REQUIRED_SECTIONS:
        match = re.search(rf"(?m)^{re.escape(section)}:\s+(.+?)\s*$", body)
        if match is None:
            errors.append(f"missing non-empty '{section}:' section")
            continue
        if match.start() <= previous_index:
            errors.append("required sections are out of order")
        value = match.group(1).strip()
        if len(value) < MINIMUM_SECTION_LENGTH or re.fullmatch(
            r"(?i:n/?a|none|unknown|tbd|todo|x)[.!]?", value
        ):
            errors.append(
                f"'{section}:' is not substantive enough: length={len(value)} minimum={MINIMUM_SECTION_LENGTH}"
            )
        values[section] = value
        previous_index = match.start()

    checks = (
        ("Ownership", r"(?i)\b(owner|ownership|authority)\b", "must identify an owner or unchanged authority"),
        ("Validation", r"(?i)\b(pass(?:ed)?|fail(?:ed)?|deferred|not applicable|exit(?:ed)?(?: code)? [0-9]+)\b", "must record an exact result or explicit deferral"),
        ("Baselines/Artifacts", r"(?i)\b(baseline|golden|artifact|binary|dll|testoutput)\b", "must disposition baselines and generated artifacts"),
        ("Review", r"(?i)\b(clean|finding|not required|deferred)\b", "must record a verdict, findings, or why review was not required"),
    )
    for section, pattern, message in checks:
        if section in values and re.search(pattern, values[section]) is None:
            errors.append(f"'{section}:' {message}")
    return errors


def _self_test() -> int:
    valid = """PLAN, TASK 2/3 - VERIFY COMMIT NOTES

Why: Preserve the implementation decision and its motivation in normal Git history for every future reviewer.
Ownership: The commit-note gate owns message validation; no product subsystem authority or runtime ownership moves.
What: Require six ordered evidence sections and reject empty, short, or placeholder commit bodies before Git records them.
Validation: The checker self-test passed its valid message and rejected both empty-body and placeholder negative controls.
Baselines/Artifacts: No baseline, golden, generated binary, vendor DLL, or TestOutput artifact changes under this policy check.
Review: Independent review is deferred until the owning implementation plan reaches its terminal closure checkpoint.
"""
    invalid_messages = (
        "PLAN, TASK 2/3 - EMPTY BODY\n",
        """PLAN, TASK 2/3 - PAD PLACEHOLDERS

Why: x
Ownership: x
What: x
Validation: x
Baselines/Artifacts: x
Review: xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
""",
    )
    with tempfile.TemporaryDirectory(prefix="skullbonez-commit-message-") as temp_dir:
        message_path = Path(temp_dir) / "message.txt"
        message_path.write_text(valid, encoding="utf-8")
        if validate_message(message_path):
            raise RuntimeError("valid detailed commit message was rejected")
        for invalid in invalid_messages:
            message_path.write_text(invalid, encoding="utf-8")
            if not validate_message(message_path):
                raise RuntimeError("invalid commit-message negative control was accepted")
    print("PASS: detailed commit-message positive and negative controls")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--message-file", type=Path)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        return _self_test()
    if args.message_file is None:
        parser.error("--message-file is required unless --self-test is used")
    errors = validate_message(args.message_file)
    if errors:
        print("Commit message rejected:")
        for error in errors:
            print(f"- {error}")
        print("Use a message file with Why, Ownership, What, Validation, Baselines/Artifacts, and Review sections.")
        return 1
    print(f"PASS: detailed commit notes accepted: {args.message_file}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
