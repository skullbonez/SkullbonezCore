#!/usr/bin/env python3
"""
File: check_broadphase_pair_stream_oracle.py
Purpose:
  Decode, validate, summarize, and byte-compare BD0 broadphase pair streams.

Summary:
  The decoder turns the temporary Debug oracle's explicitly little-endian v2
  stream into reviewable per-workload totals. It validates every structural
  boundary and all five pair-list boundaries before an artifact can serve as a
  byte-exact baseline or comparison result.

Mental model:
  A stream is a file header, zero or more complete pass records, and one global
  trailer. Every pass contains raw-grid, post-augmentation, raw first-seen
  sleep, final solver, and final sleep lists; its footer repeats the ordinal and
  byte length so declared counts cannot hide truncation or field drift.

Glossary:
  Raw-grid candidates: Canonical candidates emitted by SpatialGrid before the
    fast-small sweep adds conservative pairs.
  Raw sleep rows: First-seen traversal-order rows admitted by geometry before
    fixed-body and joint pruning.
  Final solver candidates: The span returned to the next physics stage after
    augmentation and pruning.

Invariants:
  - Every scalar and pair is decoded explicitly as little-endian data.
  - Record magic, repeated ordinal, record length, global trailer offset, and
    sequential pass ordinals must all agree.
  - Every pair is normalized, in range, and unique within its boundary.
  - Raw-grid membership survives augmentation; final candidates and sleep rows
    are subsets of their respective pre-prune lists.
  - --require-identical compares complete file bytes, not summaries or hashes.
  - The tool is read-only unless --json-out explicitly names an output receipt.

Related:
  - Agentic/Reports/2026-08-02/broadphase-pair-dedup-cost-bd0-baseline.md
  - Agentic/Reports/2026-08-02/broadphase-pair-dedup-oracles/README.md
"""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any


FILE_MAGIC = b"SKOREBD0"
RECORD_MAGIC = b"SKOREPAS"
RECORD_END_MAGIC = b"SKOREPEN"
TRAILER_MAGIC = b"SKOREEND"
FORMAT_VERSION = 2
ENCODED_PAIR_BYTES = 8
GLOBAL_TRAILER_BYTES = 24

BOUNDARY_NAMES = (
    "raw_grid_candidates",
    "post_augmentation_candidates",
    "raw_first_seen_sleep_rows",
    "final_solver_candidates",
    "final_sleep_rows",
)


class OracleDecodeError(RuntimeError):
    """Raised when a stream cannot be accepted as a complete v2 artifact."""


@dataclass
class Cursor:
    data: bytes
    path: Path
    offset: int = 0

    def take(self, byte_count: int, field: str) -> bytes:
        end = self.offset + byte_count
        if byte_count < 0 or end > len(self.data):
            raise OracleDecodeError(
                f"{self.path}: truncated {field} at offset {self.offset}; "
                f"need {byte_count} bytes, have {len(self.data) - self.offset}"
            )
        value = self.data[self.offset : end]
        self.offset = end
        return value

    def expect(self, expected: bytes, field: str) -> None:
        actual = self.take(len(expected), field)
        if actual != expected:
            raise OracleDecodeError(
                f"{self.path}: invalid {field} at offset {self.offset - len(expected)}; "
                f"expected {expected!r}, got {actual!r}"
            )

    def u32(self, field: str) -> int:
        return struct.unpack("<I", self.take(4, field))[0]

    def u64(self, field: str) -> int:
        return struct.unpack("<Q", self.take(8, field))[0]


def _decode_pairs(cursor: Cursor, pair_count: int, body_count: int, boundary: str) -> list[tuple[int, int]]:
    pairs: list[tuple[int, int]] = []
    seen: set[tuple[int, int]] = set()
    for pair_index in range(pair_count):
        first, second = struct.unpack(
            "<ii", cursor.take(ENCODED_PAIR_BYTES, f"{boundary}[{pair_index}]")
        )
        pair = (first, second)
        if first < 0 or first >= second or second >= body_count:
            raise OracleDecodeError(
                f"{cursor.path}: invalid normalized pair {pair} in {boundary}[{pair_index}] "
                f"for body_count={body_count}"
            )
        if pair in seen:
            raise OracleDecodeError(
                f"{cursor.path}: duplicate pair {pair} in {boundary}[{pair_index}]"
            )
        seen.add(pair)
        pairs.append(pair)
    return pairs


def _range_summary(values: list[int]) -> dict[str, int]:
    if not values:
        return {"sum": 0, "min": 0, "max": 0}
    return {"sum": sum(values), "min": min(values), "max": max(values)}


def decode_oracle(path: Path) -> dict[str, Any]:
    data = path.read_bytes()
    if len(data) < 16 + GLOBAL_TRAILER_BYTES:
        raise OracleDecodeError(f"{path}: stream is too small ({len(data)} bytes)")

    cursor = Cursor(data=data, path=path)
    cursor.expect(FILE_MAGIC, "file_magic")
    version = cursor.u32("version")
    if version != FORMAT_VERSION:
        raise OracleDecodeError(f"{path}: expected version {FORMAT_VERSION}, got {version}")
    pair_bytes = cursor.u32("encoded_pair_bytes")
    if pair_bytes != ENCODED_PAIR_BYTES:
        raise OracleDecodeError(
            f"{path}: expected {ENCODED_PAIR_BYTES}-byte pairs, got {pair_bytes}"
        )

    content_end = len(data) - GLOBAL_TRAILER_BYTES
    ordinals: list[int] = []
    bodies: list[int] = []
    capacities: list[int] = []
    cleared_words_values: list[int] = []
    explicit_clear_values: list[int] = []
    committed_values: list[int] = []
    grid_geometry_values: list[int] = []
    total_geometry_values: list[int] = []
    record_byte_values: list[int] = []
    boundary_counts: dict[str, list[int]] = {name: [] for name in BOUNDARY_NAMES}

    while cursor.offset < content_end:
        record_start = cursor.offset
        cursor.expect(RECORD_MAGIC, "record_magic")
        ordinal = cursor.u64("pass_ordinal")
        expected_ordinal = len(ordinals)
        if ordinal != expected_ordinal:
            raise OracleDecodeError(
                f"{path}: pass ordinal {ordinal} is not sequential; expected {expected_ordinal}"
            )

        body_count = cursor.u64("body_count")
        reserved_capacity = cursor.u64("reserved_body_capacity")
        cleared_words = cursor.u64("cleared_words")
        explicit_clear_bytes = cursor.u64("explicit_memset_bytes")
        committed_bytes = cursor.u64("pair_seen_committed_bytes")
        grid_geometry = cursor.u64("grid_geometry_invocations")
        total_geometry = cursor.u64("total_geometry_invocations")

        if body_count > reserved_capacity:
            raise OracleDecodeError(
                f"{path}: pass {ordinal} body_count={body_count} exceeds capacity={reserved_capacity}"
            )
        pair_identities = body_count * (body_count - 1) // 2 if body_count > 1 else 0
        expected_words = (pair_identities + 63) // 64
        if cleared_words != expected_words or explicit_clear_bytes != cleared_words * 8:
            raise OracleDecodeError(
                f"{path}: pass {ordinal} invalid clear accounting: words={cleared_words} "
                f"bytes={explicit_clear_bytes}, expected words={expected_words}"
            )
        reserved_pair_identities = (
            reserved_capacity * (reserved_capacity - 1) // 2 if reserved_capacity > 1 else 0
        )
        expected_committed = ((reserved_pair_identities + 63) // 64) * 8
        if committed_bytes != expected_committed:
            raise OracleDecodeError(
                f"{path}: pass {ordinal} committed_bytes={committed_bytes}, "
                f"expected {expected_committed} for capacity={reserved_capacity}"
            )
        if grid_geometry > total_geometry:
            raise OracleDecodeError(
                f"{path}: pass {ordinal} grid geometry calls exceed total calls"
            )

        raw_grid_count = cursor.u32("raw_grid_candidate_count")
        augmented_count = cursor.u32("post_augmentation_candidate_count")
        raw_sleep_count = cursor.u32("raw_first_seen_sleep_count")
        raw_grid_pairs = _decode_pairs(cursor, raw_grid_count, body_count, BOUNDARY_NAMES[0])
        augmented_pairs = _decode_pairs(cursor, augmented_count, body_count, BOUNDARY_NAMES[1])
        raw_sleep_pairs = _decode_pairs(cursor, raw_sleep_count, body_count, BOUNDARY_NAMES[2])

        final_candidate_count = cursor.u32("final_solver_candidate_count")
        final_sleep_count = cursor.u32("final_sleep_count")
        final_candidate_pairs = _decode_pairs(
            cursor, final_candidate_count, body_count, BOUNDARY_NAMES[3]
        )
        final_sleep_pairs = _decode_pairs(cursor, final_sleep_count, body_count, BOUNDARY_NAMES[4])

        if not set(raw_grid_pairs).issubset(augmented_pairs):
            raise OracleDecodeError(f"{path}: pass {ordinal} augmentation dropped a raw-grid pair")
        if not set(final_candidate_pairs).issubset(augmented_pairs):
            raise OracleDecodeError(f"{path}: pass {ordinal} final solver list added a post-prune pair")
        if not set(final_sleep_pairs).issubset(raw_sleep_pairs):
            raise OracleDecodeError(f"{path}: pass {ordinal} final sleep list added a post-prune row")

        record_content_end = cursor.offset
        cursor.expect(RECORD_END_MAGIC, "record_end_magic")
        record_end_ordinal = cursor.u64("record_end_ordinal")
        record_content_bytes = cursor.u64("record_content_bytes")
        if record_end_ordinal != ordinal:
            raise OracleDecodeError(
                f"{path}: pass {ordinal} footer ordinal is {record_end_ordinal}"
            )
        expected_record_bytes = record_content_end - record_start
        if record_content_bytes != expected_record_bytes:
            raise OracleDecodeError(
                f"{path}: pass {ordinal} footer length={record_content_bytes}, "
                f"expected {expected_record_bytes}"
            )
        if cursor.offset > content_end:
            raise OracleDecodeError(f"{path}: pass {ordinal} overlaps the global trailer")

        ordinals.append(ordinal)
        bodies.append(body_count)
        capacities.append(reserved_capacity)
        cleared_words_values.append(cleared_words)
        explicit_clear_values.append(explicit_clear_bytes)
        committed_values.append(committed_bytes)
        grid_geometry_values.append(grid_geometry)
        total_geometry_values.append(total_geometry)
        record_byte_values.append(record_content_bytes)
        for boundary, count in zip(
            BOUNDARY_NAMES,
            (
                raw_grid_count,
                augmented_count,
                raw_sleep_count,
                final_candidate_count,
                final_sleep_count,
            ),
            strict=True,
        ):
            boundary_counts[boundary].append(count)

    if cursor.offset != content_end:
        raise OracleDecodeError(
            f"{path}: record area ended at {cursor.offset}, expected trailer at {content_end}"
        )
    cursor.expect(TRAILER_MAGIC, "trailer_magic")
    trailer_pass_count = cursor.u64("trailer_pass_count")
    trailer_content_bytes = cursor.u64("trailer_content_bytes")
    if trailer_pass_count != len(ordinals):
        raise OracleDecodeError(
            f"{path}: trailer pass_count={trailer_pass_count}, decoded {len(ordinals)}"
        )
    if trailer_content_bytes != content_end:
        raise OracleDecodeError(
            f"{path}: trailer content_bytes={trailer_content_bytes}, expected {content_end}"
        )
    if cursor.offset != len(data):
        raise OracleDecodeError(f"{path}: {len(data) - cursor.offset} trailing bytes remain")

    return {
        "path": path.as_posix(),
        "bytes": len(data),
        "sha256": hashlib.sha256(data).hexdigest().upper(),
        "version": version,
        "pass_count": len(ordinals),
        "ordinal_first": ordinals[0] if ordinals else None,
        "ordinal_last": ordinals[-1] if ordinals else None,
        "body_counts": sorted(set(bodies)),
        "reserved_body_capacities": sorted(set(capacities)),
        "cleared_words": _range_summary(cleared_words_values),
        "explicit_memset_bytes": _range_summary(explicit_clear_values),
        "pair_seen_committed_bytes": _range_summary(committed_values),
        "grid_geometry_invocations": _range_summary(grid_geometry_values),
        "total_geometry_invocations": _range_summary(total_geometry_values),
        "record_content_bytes": _range_summary(record_byte_values),
        "boundaries": {
            boundary: _range_summary(counts) for boundary, counts in boundary_counts.items()
        },
    }


def _parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("oracles", nargs="+", type=Path, help="v2 .bin streams to validate")
    parser.add_argument(
        "--require-identical",
        action="store_true",
        help="require every stream to be byte-identical to the first",
    )
    parser.add_argument("--json-out", type=Path, help="optional structured validation receipt")
    return parser.parse_args()


def main() -> int:
    args = _parse_args()
    try:
        summaries = [decode_oracle(path) for path in args.oracles]
        if args.require_identical:
            reference = args.oracles[0].read_bytes()
            for path in args.oracles[1:]:
                if path.read_bytes() != reference:
                    raise OracleDecodeError(
                        f"{path}: stream differs byte-for-byte from {args.oracles[0]}"
                    )
    except (OSError, OracleDecodeError) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return 1

    receipt = {
        "schema_version": 1,
        "oracle_format_version": FORMAT_VERSION,
        "require_identical": args.require_identical,
        "identical": True if args.require_identical else None,
        "streams": summaries,
    }
    if args.json_out:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")

    for summary in summaries:
        print(
            "PASS: "
            f"{summary['path']} passes={summary['pass_count']} bytes={summary['bytes']} "
            f"sha256={summary['sha256']}"
        )
    if args.require_identical:
        print(f"PASS: {len(summaries)} streams are byte-identical")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
