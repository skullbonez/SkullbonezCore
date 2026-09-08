"""Convert existing Solver Lab diagnostic evidence to indexed numeric records.

SKOBS1 stores the source SHA-256, a sorted scene-frame directory, then each
frame's contacts followed by solver iterations. All numbers are little endian;
floats are rounded to the same binary32 values retained by the JSON loader.
Original archives and recordings are never replaced.
"""

from __future__ import annotations

import argparse
import ctypes
from ctypes import wintypes
import hashlib
import json
import math
from pathlib import Path
import struct
import tempfile

MAGIC = b"SKOBS1\r\n"
CONTACT = struct.Struct("<iiII9f")
ITERATION = struct.Struct("<ii3f")
INDEX = struct.Struct("<iIII")
KINDS = (b'{"kind":"contact"', b'{"kind":"solver_iteration_summary"',
         b'{"kind":"frame"', b'{"kind":"solver_stats"')


def number(row, key):
    value = row[key]
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ValueError(f"Non-numeric {key}")
    value = float(value)
    if not math.isfinite(value) or abs(value) > 1.0e30:
        raise ValueError(f"Invalid {key}")
    return struct.unpack("<f", struct.pack("<f", value))[0]


def integer(row, key):
    value = number(row, key)
    if value < -1 or value > 10000000 or value != math.floor(value):
        raise ValueError(f"Invalid integer {key}")
    return int(value)


def pack_contact(row):
    feature = row["feature_id"]
    if type(feature) is not int or not 0 <= feature <= 0xffffffff:
        raise ValueError("Invalid feature_id")
    normal = row["normal"]
    if not isinstance(normal, list) or len(normal) != 3:
        raise ValueError("Invalid contact normal")
    values = [number({"v": value}, "v") for value in normal]
    values += [number(row, key) for key in (
        "penetration", "normal_impulse", "tangent_impulse",
        "pre_solve_normal_speed", "pre_solve_slip_speed", "slip_speed")]
    return CONTACT.pack(integer(row, "body_a"), integer(row, "body_b"), feature,
                        int(integer(row, "warm_started") != 0), *values)


def write_binary_diagnostics(source: Path, output: Path, expected_hash: str):
    # Lifetime: conversion is offline. Retain compact numeric bytes per frame
    # so interleaved JSON rows keep their original order within each row kind.
    frames = {}
    digest = hashlib.sha256()
    with source.open("rb") as stream:
        for line in stream:
            digest.update(line)
            if len(line.rstrip(b"\n")) > 65536:
                raise ValueError("Diagnostic row exceeds 64 KiB")
            if not line.startswith(KINDS):
                continue
            row = json.loads(line)
            frame = integer(row, "frame")
            if frame not in frames:
                if len(frames) >= 14402:
                    raise ValueError("Too many diagnostic frames")
                frames[frame] = [0, bytearray(), bytearray()]
            entry = frames[frame]
            kind = row["kind"]
            if kind == "frame":
                if entry[0] & 1:
                    raise ValueError("Duplicate diagnostic frame")
                entry[0] |= 1
            elif kind == "solver_stats":
                entry[0] |= 2
            elif kind == "contact":
                entry[1].extend(pack_contact(row))
            elif kind == "solver_iteration_summary":
                entry[2].extend(ITERATION.pack(
                    integer(row, "iteration"), integer(row, "dropped_iterations"),
                    number(row, "stopping_impulse_delta_sq"),
                    number(row, "normal_impulse_delta_sq"),
                    number(row, "tangent_impulse_delta_sq")))
    if digest.hexdigest() != expected_hash:
        raise ValueError(f"Source diagnostic hash mismatch: {source}")
    ordered = sorted(frames.items())
    temporary = output.with_suffix(output.suffix + ".tmp")
    with temporary.open("wb") as stream:
        stream.write(MAGIC + digest.digest() + struct.pack("<I", len(ordered)))
        for frame, (flags, contacts, iterations) in ordered:
            stream.write(INDEX.pack(frame, flags, len(contacts) // CONTACT.size,
                                    len(iterations) // ITERATION.size))
        for _, (_, contacts, iterations) in ordered:
            stream.write(contacts)
            stream.write(iterations)
    with temporary.open("rb") as stream:
        binary_hash = hashlib.file_digest(stream, "sha256").hexdigest()
    temporary.replace(output)
    print(f"{output}: {source.stat().st_size:,} -> {output.stat().st_size:,} bytes; "
          f"{len(ordered)} frames", flush=True)
    return {"path": output.name, "sha256": binary_hash, "sourceSha256": expected_hash}


def restore_archive(directory: Path, parts, output: Path):
    api = ctypes.WinDLL("cabinet", use_last_error=True)
    api.CreateDecompressor.argtypes = [wintypes.DWORD, ctypes.c_void_p,
                                      ctypes.POINTER(ctypes.c_void_p)]
    api.Decompress.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t,
                              ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
    api.CloseDecompressor.argtypes = [ctypes.c_void_p]
    handle = ctypes.c_void_p()
    if not api.CreateDecompressor(4, None, ctypes.byref(handle)):
        raise ctypes.WinError(ctypes.get_last_error())
    raw = ctypes.create_string_buffer(1024 * 1024)
    total = 0
    try:
        with output.open("wb") as target:
            for name in parts:
                if Path(name).name != name:
                    raise ValueError("Archive part must be a sibling file")
                with (directory / name).open("rb") as source:
                    if source.read(8) != b"SKDIAG1\n":
                        raise ValueError("Invalid diagnostic archive")
                    while True:
                        unpacked, packed = struct.unpack("<II", source.read(8))
                        if unpacked == packed == 0:
                            if source.read(1):
                                raise ValueError("Trailing archive bytes")
                            break
                        total += unpacked
                        if not 0 < unpacked <= len(raw) or not 0 < packed <= 2 * len(raw) or total > 2**31:
                            raise ValueError("Invalid archive block size")
                        data = source.read(packed)
                        size = ctypes.c_size_t()
                        if len(data) != packed or not api.Decompress(
                                handle, data, packed, raw, unpacked, ctypes.byref(size)) or size.value != unpacked:
                            raise ValueError("Diagnostic decompression failed")
                        target.write(raw.raw[:unpacked])
    finally:
        api.CloseDecompressor(handle)


def convert_bundle(bundle: Path):
    manifest_path = bundle / "comparison.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if manifest.get("status") != "complete":
        raise ValueError("Only complete captures can be converted")
    for side, metadata in zip(("A", "B"), manifest["sides"], strict=True):
        expected = metadata["diagnosticsSha256"]
        source = bundle / side / "physics.physicsdiag.ndjson"
        cached = Path(tempfile.gettempdir()) / "SkullbonezSolverLab" / (expected + ".physicsdiag.ndjson")
        with tempfile.TemporaryDirectory(prefix="solver-convert-") as scratch:
            if not source.exists():
                source = cached
            if not source.exists():
                source = Path(scratch) / "diagnostics.ndjson"
                restore_archive(bundle / side, metadata["diagnosticsArchive"], source)
            metadata["diagnosticsBinary"] = write_binary_diagnostics(
                source, bundle / side / "physics.skobs", expected)
    # Publish the manifest only after both complete binaries have been written.
    temporary = manifest_path.with_suffix(".json.tmp")
    temporary.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    temporary.replace(manifest_path)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("bundles", type=Path, nargs="+")
    args = parser.parse_args()
    for bundle in args.bundles:
        convert_bundle(bundle)
