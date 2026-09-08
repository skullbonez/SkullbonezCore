"""Archive an existing comparison without changing its recordings or evidence.

Windows XPRESS-Huffman blocks keep diagnostics streamable with a bounded native
decoder. Runtime command traffic is not needed to inspect a completed capture.
"""

from __future__ import annotations

import argparse
import ctypes
from ctypes import wintypes
import hashlib
import json
from pathlib import Path
import shutil
import struct
from convert_solver_diagnostics import write_binary_diagnostics

BLOCK_BYTES = 1024 * 1024
PART_BYTES = 48 * 1024 * 1024
MAGIC = b"SKDIAG1\n"


def cabinet_api():
    api = ctypes.WinDLL("cabinet", use_last_error=True)
    api.CreateCompressor.argtypes = [wintypes.DWORD, ctypes.c_void_p, ctypes.POINTER(ctypes.c_void_p)]
    api.Compress.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t,
                            ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
    api.CloseCompressor.argtypes = [ctypes.c_void_p]
    return api


def compress_diagnostics(source: Path, destination: Path) -> tuple[list[str], str]:
    api = cabinet_api()
    handle = ctypes.c_void_p()
    if not api.CreateCompressor(4, None, ctypes.byref(handle)):
        raise ctypes.WinError(ctypes.get_last_error())
    digest = hashlib.sha256()
    parts = []
    output = None
    packed = ctypes.create_string_buffer(BLOCK_BYTES * 2)
    try:
        with source.open("rb") as stream:
            while raw := stream.read(BLOCK_BYTES):
                digest.update(raw)
                size = ctypes.c_size_t()
                if not api.Compress(handle, raw, len(raw), packed, len(packed), ctypes.byref(size)):
                    raise ctypes.WinError(ctypes.get_last_error())
                if output is None or output.tell() + size.value + 16 > PART_BYTES:
                    if output:
                        output.write(struct.pack("<II", 0, 0))
                        output.close()
                    name = f"diagnostics-{len(parts):03d}.skdiag"
                    parts.append(name)
                    output = (destination / name).open("wb")
                    output.write(MAGIC)
                output.write(struct.pack("<II", len(raw), size.value))
                output.write(packed.raw[:size.value])
        if output:
            output.write(struct.pack("<II", 0, 0))
    finally:
        if output:
            output.close()
        api.CloseCompressor(handle)
    return parts, digest.hexdigest()


def package(source: Path, destination: Path, title: str):
    manifest = json.loads((source / "comparison.json").read_text())
    if manifest.get("status") != "complete":
        raise ValueError("Only complete comparisons can be published")
    destination.mkdir(parents=True, exist_ok=False)
    shutil.copytree(source / "inputs", destination / "inputs")
    manifest["title"] = title
    manifest["packaging"] = {
        "diagnostics": "lossless Windows XPRESS-Huffman blocks, SKDIAG1",
        "omitted": ["runtime.skarness.ndjson transport logs", "session database and local command reports"],
    }
    for side, metadata in zip(("A", "B"), manifest["sides"], strict=True):
        recording = source / metadata["path"]
        if hashlib.file_digest(recording.open("rb"), "sha256").hexdigest() != metadata["sha256"]:
            raise ValueError(f"Recording hash mismatch: {side}")
        shutil.copyfile(recording, destination / metadata["path"])
        side_directory = destination / side
        side_directory.mkdir()
        diagnostics = source / side / "physics.physicsdiag.ndjson"
        parts, digest = compress_diagnostics(diagnostics, side_directory)
        if metadata.get("diagnosticsSha256", digest) != digest:
            raise ValueError(f"Diagnostic hash mismatch: {side}")
        metadata["diagnosticsSha256"] = digest
        metadata["diagnosticsBytes"] = diagnostics.stat().st_size
        metadata["diagnosticsArchive"] = parts
        metadata["diagnosticsBinary"] = write_binary_diagnostics(
            diagnostics, side_directory / "physics.skobs", digest)
        for filename in ("stdout.log", "stderr.log"):
            if (source / side / filename).exists():
                shutil.copyfile(source / side / filename, side_directory / filename)
        print(f"{title}: {side} archived ({len(parts)} parts)", flush=True)
    (destination / "comparison.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    parser.add_argument("--title", required=True)
    args = parser.parse_args()
    package(args.source, args.destination, args.title)
