#!/usr/bin/env python3
"""Query chunked Skullbonez replay v2 artifacts without loading them into GPT."""

from __future__ import annotations

import argparse
import json
import math
import struct
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


MAGIC = b"SKREPV2\0"
HEADER = struct.Struct("<8sIIIIQQ")
CHUNK_ENTRY = struct.Struct("<4sQQII")
U32 = struct.Struct("<I")
COUNTED_U8 = struct.Struct("<B")
COUNTED_U16 = struct.Struct("<H")
COUNTED_I32 = struct.Struct("<i")
COUNTED_I64 = struct.Struct("<q")
COUNTED_FLOAT = struct.Struct("<f")
PAIR_I32 = struct.Struct("<ii")
BODY_RECORD = struct.Struct("<IiB3s64s")
FRAME_HEADER = struct.Struct("<QidfQHHBBH" + ("f" * 12) + "I")
BODY_POSE = struct.Struct("<Ifffffff")
BRANCH_RECORD = struct.Struct("<IIQQQQQIIQ")
HASH_RECORD = struct.Struct("<QidQQIHHB3s")
CHECKPOINT_HEADER = struct.Struct("<QidfQQHHBBH")
LAUNCHER_HEADER = struct.Struct("<iiBB2sff")
TORNADO_CONFIG = struct.Struct("<BB2s" + ("f" * 14))
SOLVER_STATS = struct.Struct("<iiiiiiiff")
SOLVER_BODY = struct.Struct("<I" + ("f" * 21) + "5s3siHHff")

FLAG_WATER_HIDDEN = 1 << 0
FLAG_TERRAIN_HIDDEN = 1 << 1
FLAG_FIXED_STEP = 1 << 2
FLAG_SCENE_PHYSICS = 1 << 3
FLAG_SCENE_TEXT = 1 << 4


class ReplayQueryError(RuntimeError):
    pass


@dataclass
class ChunkInfo:
    ident: str
    offset: int
    size: int
    record_count: int


@dataclass
class BodyInfo:
    dictionary_index: int
    body_id: int
    model_index: int
    shape_kind: int
    name: str

    @property
    def shape(self) -> str:
        return {
            1: "sphere",
            2: "box",
            3: "convexHull",
        }.get(self.shape_kind, "unknown")


@dataclass
class FrameIndex:
    frame_index: int
    presentation_offset: int
    body_count: int


@dataclass
class SolverHashInfo:
    frame_index: int
    scene_frame: int
    time_seconds: float
    presentation_hash: int
    solver_hash: int
    body_count: int
    contact_count: int
    pipeline_record_count: int
    checkpoint_boundary: bool


@dataclass
class BranchInfo:
    branch_id: int
    parent_branch_id: int
    start_frame: int
    first_retained_frame: int
    last_retained_frame: int
    source_frame: int
    source_solver_hash: int
    flags: int

    @property
    def restored(self) -> bool:
        return bool(self.flags & 1)

    @property
    def has_source_hash(self) -> bool:
        return bool(self.flags & 2)


@dataclass
class SolverCheckpointInfo:
    frame_index: int
    scene_frame: int
    time_seconds: float
    dt: float
    presentation_hash: int
    solver_hash: int
    body_count: int
    contact_count: int
    pipeline_record_count: int
    checkpoint_boundary: bool
    world_flags: int
    ray_line_count: int
    laser_shot_count: int
    snapshot_version: int
    snapshot_model_count: int
    persistent_contact_count: int
    contact_cache_count: int
    debug_contact_count: int
    pipeline_trace_count: int
    collision_cell_key_count: int
    bodies: list[dict[str, object]]


class ChunkReader:
    def __init__(self, raw: bytes, label: str) -> None:
        self.raw = raw
        self.label = label
        self.offset = 0

    def unpack(self, fmt: struct.Struct) -> tuple[object, ...]:
        if self.offset + fmt.size > len(self.raw):
            raise ReplayQueryError(f"{self.label} chunk ended unexpectedly")
        values = fmt.unpack_from(self.raw, self.offset)
        self.offset += fmt.size
        return values

    def u32(self) -> int:
        return int(self.unpack(U32)[0])

    def skip(self, count: int) -> None:
        if count < 0 or self.offset + count > len(self.raw):
            raise ReplayQueryError(f"{self.label} chunk ended unexpectedly")
        self.offset += count


def clean_name(raw: bytes) -> str:
    return raw.split(b"\0", 1)[0].decode("utf-8", errors="replace")


def hash_text(value: int) -> str:
    return f"0x{value:016X}"


def read_exact_range(data: bytes, offset: int, size: int, label: str) -> bytes:
    if offset < 0 or size < 0 or offset + size > len(data):
        raise ReplayQueryError(f"{label} chunk range is outside the file")
    return data[offset : offset + size]


class ReplayV2:
    def __init__(self, path: Path) -> None:
        self.path = path
        self.data = path.read_bytes()
        self.version = 0
        self.flags = 0
        self.file_size = len(self.data)
        self.chunks: dict[str, ChunkInfo] = {}
        self.manifest: dict[str, object] = {}
        self.bodies: list[BodyInfo] = []
        self.frames: list[FrameIndex] = []
        self.branches: list[BranchInfo] = []
        self.solver_hashes: list[SolverHashInfo] = []
        self.solver_checkpoints: list[SolverCheckpointInfo] = []
        self._parse_header()
        self._parse_manifest()
        self._parse_bodies()
        self._parse_index()
        self._parse_branches()
        self._parse_solver_hashes()
        self._parse_solver_checkpoints()

    def _parse_header(self) -> None:
        if len(self.data) < HEADER.size:
            raise ReplayQueryError("file is too small to be a replay v2 artifact")
        magic, version, header_size, chunk_count, flags, table_offset, file_size = HEADER.unpack_from(self.data, 0)
        if magic != MAGIC:
            if self.data[:1] == b"{":
                raise ReplayQueryError("this is a legacy JSON replay artifact, not v2 binary")
            raise ReplayQueryError("unrecognized replay magic")
        if version != 2:
            raise ReplayQueryError(f"unsupported replay version {version}")
        if header_size != HEADER.size:
            raise ReplayQueryError(f"unexpected v2 header size {header_size}")
        if file_size != len(self.data):
            raise ReplayQueryError(f"header file size {file_size} does not match actual size {len(self.data)}")

        self.version = version
        self.flags = flags
        self.file_size = file_size

        table_end = table_offset + chunk_count * CHUNK_ENTRY.size
        if table_offset < header_size or table_end > len(self.data):
            raise ReplayQueryError("chunk table is outside the file")

        cursor = table_offset
        for _ in range(chunk_count):
            ident_raw, offset, size, record_count, _reserved = CHUNK_ENTRY.unpack_from(self.data, cursor)
            cursor += CHUNK_ENTRY.size
            ident = ident_raw.decode("ascii", errors="replace")
            read_exact_range(self.data, offset, size, ident)
            self.chunks[ident] = ChunkInfo(ident, offset, size, record_count)

    def _chunk_bytes(self, ident: str) -> bytes:
        chunk = self.chunks.get(ident)
        if not chunk:
            raise ReplayQueryError(f"missing required {ident} chunk")
        return read_exact_range(self.data, chunk.offset, chunk.size, ident)

    def _parse_manifest(self) -> None:
        raw = self._chunk_bytes("MANI")
        try:
            self.manifest = json.loads(raw.decode("utf-8"))
        except json.JSONDecodeError as exc:
            raise ReplayQueryError(f"manifest JSON is invalid: {exc}") from exc

    def _parse_bodies(self) -> None:
        raw = self._chunk_bytes("BODY")
        if len(raw) < 4:
            raise ReplayQueryError("BODY chunk is too small")
        body_count = struct.unpack_from("<I", raw, 0)[0]
        cursor = 4
        bodies: list[BodyInfo] = []
        for dictionary_index in range(body_count):
            if cursor + BODY_RECORD.size > len(raw):
                raise ReplayQueryError("BODY chunk ended mid-record")
            body_id, model_index, shape_kind, _reserved, name_raw = BODY_RECORD.unpack_from(raw, cursor)
            cursor += BODY_RECORD.size
            bodies.append(
                BodyInfo(
                    dictionary_index=dictionary_index,
                    body_id=body_id,
                    model_index=model_index,
                    shape_kind=shape_kind,
                    name=clean_name(name_raw),
                )
            )
        self.bodies = bodies

    def _parse_index(self) -> None:
        raw = self._chunk_bytes("INDX")
        if len(raw) < 4:
            raise ReplayQueryError("INDX chunk is too small")
        frame_count = struct.unpack_from("<I", raw, 0)[0]
        cursor = 4
        frames: list[FrameIndex] = []
        for _ in range(frame_count):
            if cursor + 24 > len(raw):
                raise ReplayQueryError("INDX chunk ended mid-record")
            frame_index, presentation_offset, body_count, _reserved = struct.unpack_from("<QQII", raw, cursor)
            cursor += 24
            frames.append(FrameIndex(frame_index, presentation_offset, body_count))
        self.frames = frames

    def _parse_solver_hashes(self) -> None:
        chunk = self.chunks.get("HASH")
        if not chunk:
            self.solver_hashes = []
            return
        raw = read_exact_range(self.data, chunk.offset, chunk.size, "HASH")
        if len(raw) < 4:
            raise ReplayQueryError("HASH chunk is too small")
        hash_count = struct.unpack_from("<I", raw, 0)[0]
        if hash_count != chunk.record_count:
            raise ReplayQueryError("HASH chunk count does not match chunk table")
        cursor = 4
        hashes: list[SolverHashInfo] = []
        for _ in range(hash_count):
            if cursor + HASH_RECORD.size > len(raw):
                raise ReplayQueryError("HASH chunk ended mid-record")
            (
                frame_index,
                scene_frame,
                time_seconds,
                presentation_hash,
                solver_hash,
                body_count,
                contact_count,
                pipeline_record_count,
                checkpoint_boundary,
                _reserved,
            ) = HASH_RECORD.unpack_from(raw, cursor)
            cursor += HASH_RECORD.size
            hashes.append(
                SolverHashInfo(
                    frame_index=frame_index,
                    scene_frame=scene_frame,
                    time_seconds=time_seconds,
                    presentation_hash=presentation_hash,
                    solver_hash=solver_hash,
                    body_count=body_count,
                    contact_count=contact_count,
                    pipeline_record_count=pipeline_record_count,
                    checkpoint_boundary=bool(checkpoint_boundary),
                )
            )
        if cursor != len(raw):
            raise ReplayQueryError("HASH chunk has trailing bytes")
        self.solver_hashes = hashes

    def _parse_branches(self) -> None:
        chunk = self.chunks.get("BRAN")
        if not chunk:
            self.branches = []
            return
        raw = read_exact_range(self.data, chunk.offset, chunk.size, "BRAN")
        if len(raw) < 4:
            raise ReplayQueryError("BRAN chunk is too small")
        branch_count = struct.unpack_from("<I", raw, 0)[0]
        if branch_count != chunk.record_count:
            raise ReplayQueryError("BRAN chunk count does not match chunk table")
        cursor = 4
        branches: list[BranchInfo] = []
        for _ in range(branch_count):
            if cursor + BRANCH_RECORD.size > len(raw):
                raise ReplayQueryError("BRAN chunk ended mid-record")
            (
                branch_id,
                parent_branch_id,
                start_frame,
                first_retained_frame,
                last_retained_frame,
                source_frame,
                source_solver_hash,
                flags,
                _reserved,
                _reserved64,
            ) = BRANCH_RECORD.unpack_from(raw, cursor)
            cursor += BRANCH_RECORD.size
            branches.append(
                BranchInfo(
                    branch_id=int(branch_id),
                    parent_branch_id=int(parent_branch_id),
                    start_frame=int(start_frame),
                    first_retained_frame=int(first_retained_frame),
                    last_retained_frame=int(last_retained_frame),
                    source_frame=int(source_frame),
                    source_solver_hash=int(source_solver_hash),
                    flags=int(flags),
                )
            )
        if cursor != len(raw):
            raise ReplayQueryError("BRAN chunk has trailing bytes")
        self.branches = branches

    @staticmethod
    def _skip_counted(reader: ChunkReader, item: struct.Struct) -> int:
        count = reader.u32()
        reader.skip(item.size * count)
        return count

    @staticmethod
    def _parse_launcher_summary(reader: ChunkReader) -> tuple[int, int]:
        reader.unpack(LAUNCHER_HEADER)
        ray_line_count = reader.u32()
        reader.skip(32 * ray_line_count)
        laser_shot_count = reader.u32()
        reader.skip(60 * laser_shot_count)
        return ray_line_count, laser_shot_count

    @staticmethod
    def _parse_snapshot_summary(reader: ChunkReader) -> dict[str, int]:
        version, model_count, _next_sleep_id, _sleep_enabled, _collision_active, _reserved = reader.unpack(
            struct.Struct("<IiiBB2s")
        )
        reader.unpack(TORNADO_CONFIG)
        ReplayV2._skip_counted(reader, COUNTED_FLOAT)
        ReplayV2._skip_counted(reader, COUNTED_U8)
        ReplayV2._skip_counted(reader, COUNTED_U8)
        ReplayV2._skip_counted(reader, COUNTED_U8)
        ReplayV2._skip_counted(reader, COUNTED_U8)
        ReplayV2._skip_counted(reader, COUNTED_U8)
        ReplayV2._skip_counted(reader, COUNTED_FLOAT)
        ReplayV2._skip_counted(reader, COUNTED_FLOAT)
        ReplayV2._skip_counted(reader, COUNTED_U8)
        ReplayV2._skip_counted(reader, COUNTED_I32)
        ReplayV2._skip_counted(reader, COUNTED_I32)
        ReplayV2._skip_counted(reader, PAIR_I32)
        ReplayV2._skip_counted(reader, COUNTED_I32)
        ReplayV2._skip_counted(reader, COUNTED_U8)
        ReplayV2._skip_counted(reader, COUNTED_U8)
        ReplayV2._skip_counted(reader, COUNTED_U8)
        ReplayV2._skip_counted(reader, COUNTED_U8)
        ReplayV2._skip_counted(reader, COUNTED_U8)
        persistent_contact_count = reader.u32()
        reader.skip(140 * persistent_contact_count)
        contact_cache_count = reader.u32()
        reader.skip(20 * contact_cache_count)
        reader.unpack(SOLVER_STATS)
        ReplayV2._skip_counted(reader, COUNTED_U16)
        ReplayV2._skip_counted(reader, COUNTED_U16)
        debug_contact_count = reader.u32()
        reader.skip(68 * debug_contact_count)
        pipeline_trace_count = reader.u32()
        reader.skip(56 * pipeline_trace_count)
        collision_cell_key_count = ReplayV2._skip_counted(reader, COUNTED_I64)
        return {
            "version": int(version),
            "modelCount": int(model_count),
            "persistentContactCount": persistent_contact_count,
            "contactCacheCount": contact_cache_count,
            "debugContactCount": debug_contact_count,
            "pipelineTraceCount": pipeline_trace_count,
            "collisionCellKeyCount": collision_cell_key_count,
        }

    def _parse_solver_body(self, reader: ChunkReader) -> dict[str, object]:
        values = reader.unpack(SOLVER_BODY)
        dictionary_index = int(values[0])
        if dictionary_index >= len(self.bodies):
            raise ReplayQueryError("SCHK body dictionary index is out of range")
        floats = values[1:22]
        flags = values[22]
        sleep_island_visual_id = int(values[24])
        contact_count = int(values[25])
        max_penetration = float(values[27])
        normal_impulse_sum = float(values[28])
        body = self.bodies[dictionary_index]
        return {
            "dictionaryIndex": dictionary_index,
            "bodyId": body.body_id,
            "modelIndex": body.model_index,
            "name": body.name,
            "shape": body.shape,
            "position": [round_float(floats[0]), round_float(floats[1]), round_float(floats[2])],
            "linearVelocity": [round_float(floats[3]), round_float(floats[4]), round_float(floats[5])],
            "angularVelocity": [round_float(floats[6]), round_float(floats[7]), round_float(floats[8])],
            "orientation": [
                round_float(floats[9]),
                round_float(floats[10]),
                round_float(floats[11]),
                round_float(floats[12]),
            ],
            "mass": round_float(floats[13]),
            "inverseMass": round_float(floats[14]),
            "fixed": bool(flags[0]) if flags else False,
            "sleeping": bool(flags[1]) if len(flags) > 1 else False,
            "sleepSupported": bool(flags[2]) if len(flags) > 2 else False,
            "sleepInhibited": bool(flags[3]) if len(flags) > 3 else False,
            "collisionContact": bool(flags[4]) if len(flags) > 4 else False,
            "sleepIslandVisualId": sleep_island_visual_id,
            "contactCount": contact_count,
            "maxPenetration": round_float(max_penetration),
            "normalImpulseSum": round_float(normal_impulse_sum),
        }

    def _parse_solver_checkpoints(self) -> None:
        chunk = self.chunks.get("SCHK")
        if not chunk:
            self.solver_checkpoints = []
            return
        raw = read_exact_range(self.data, chunk.offset, chunk.size, "SCHK")
        reader = ChunkReader(raw, "SCHK")
        checkpoint_count = reader.u32()
        if checkpoint_count != chunk.record_count:
            raise ReplayQueryError("SCHK chunk count does not match chunk table")

        checkpoints: list[SolverCheckpointInfo] = []
        for _ in range(checkpoint_count):
            (
                frame_index,
                scene_frame,
                time_seconds,
                dt,
                presentation_hash,
                solver_hash,
                contact_count,
                pipeline_record_count,
                checkpoint_boundary,
                world_flags,
                _reserved,
            ) = reader.unpack(CHECKPOINT_HEADER)
            reader.skip(12 + 36)
            ray_line_count, laser_shot_count = self._parse_launcher_summary(reader)
            snapshot = self._parse_snapshot_summary(reader)
            body_count = reader.u32()
            body_records = [self._parse_solver_body(reader) for _ in range(body_count)]
            checkpoints.append(
                SolverCheckpointInfo(
                    frame_index=int(frame_index),
                    scene_frame=int(scene_frame),
                    time_seconds=float(time_seconds),
                    dt=float(dt),
                    presentation_hash=int(presentation_hash),
                    solver_hash=int(solver_hash),
                    body_count=body_count,
                    contact_count=int(contact_count),
                    pipeline_record_count=int(pipeline_record_count),
                    checkpoint_boundary=bool(checkpoint_boundary),
                    world_flags=int(world_flags),
                    ray_line_count=ray_line_count,
                    laser_shot_count=laser_shot_count,
                    snapshot_version=snapshot["version"],
                    snapshot_model_count=snapshot["modelCount"],
                    persistent_contact_count=snapshot["persistentContactCount"],
                    contact_cache_count=snapshot["contactCacheCount"],
                    debug_contact_count=snapshot["debugContactCount"],
                    pipeline_trace_count=snapshot["pipelineTraceCount"],
                    collision_cell_key_count=snapshot["collisionCellKeyCount"],
                    bodies=body_records,
                )
            )
        if reader.offset != len(raw):
            raise ReplayQueryError("SCHK chunk has trailing bytes")
        self.solver_checkpoints = checkpoints

    def summary(self) -> dict[str, object]:
        first = self.frames[0] if self.frames else None
        last = self.frames[-1] if self.frames else None
        first_hash = self.solver_hashes[0] if self.solver_hashes else None
        last_hash = self.solver_hashes[-1] if self.solver_hashes else None
        first_checkpoint = self.solver_checkpoints[0] if self.solver_checkpoints else None
        last_checkpoint = self.solver_checkpoints[-1] if self.solver_checkpoints else None
        chunks = [
            {
                "id": chunk.ident,
                "offset": chunk.offset,
                "size": chunk.size,
                "recordCount": chunk.record_count,
            }
            for chunk in self.chunks.values()
        ]
        return {
            "path": str(self.path),
            "format": self.manifest.get("format"),
            "version": self.version,
            "track": self.manifest.get("track"),
            "authoritative": self.manifest.get("authoritative", False),
            "frameCount": len(self.frames),
            "bodyDictionaryCount": len(self.bodies),
            "branchCount": len(self.branches),
            "solverHashCount": len(self.solver_hashes),
            "solverCheckpointCount": len(self.solver_checkpoints),
            "firstBranchId": self.branches[0].branch_id if self.branches else None,
            "lastBranchId": self.branches[-1].branch_id if self.branches else None,
            "firstSolverHashFrame": first_hash.frame_index if first_hash else None,
            "lastSolverHashFrame": last_hash.frame_index if last_hash else None,
            "firstSolverCheckpointFrame": first_checkpoint.frame_index if first_checkpoint else None,
            "lastSolverCheckpointFrame": last_checkpoint.frame_index if last_checkpoint else None,
            "firstFrame": first.frame_index if first else None,
            "lastFrame": last.frame_index if last else None,
            "durationSeconds": self._duration_seconds(),
            "fileBytes": self.file_size,
            "bodyPoseBytes": self.manifest.get("bodyPoseBytes"),
            "branchEntryBytes": self.manifest.get("branchEntryBytes", 0),
            "solverHashBytes": self.manifest.get("solverHashBytes", 0),
            "solverBodyBytes": self.manifest.get("solverBodyBytes", 0),
            "chunks": chunks,
        }

    def _duration_seconds(self) -> float | None:
        if len(self.frames) < 2:
            return None
        first = self.read_frame(self.frames[0], body_limit=0)
        last = self.read_frame(self.frames[-1], body_limit=0)
        return round(float(last["timeSeconds"]) - float(first["timeSeconds"]), 6)

    def selected_frames(self, frame_range: str | None) -> list[FrameIndex]:
        if not frame_range:
            return list(self.frames)
        start, end = parse_frame_range(frame_range)
        return [frame for frame in self.frames if start <= frame.frame_index <= end]

    def selected_hashes(self, frame_range: str | None) -> list[SolverHashInfo]:
        if not frame_range:
            return list(self.solver_hashes)
        start, end = parse_frame_range(frame_range)
        return [row for row in self.solver_hashes if start <= row.frame_index <= end]

    def selected_checkpoints(self, frame_range: str | None) -> list[SolverCheckpointInfo]:
        if not frame_range:
            return list(self.solver_checkpoints)
        start, end = parse_frame_range(frame_range)
        return [row for row in self.solver_checkpoints if start <= row.frame_index <= end]

    def find_frame(self, frame_index: int) -> FrameIndex:
        for frame in self.frames:
            if frame.frame_index == frame_index:
                return frame
        raise ReplayQueryError(f"frame {frame_index} not found")

    def resolve_body(self, ref: str) -> BodyInfo:
        numeric: int | None = None
        try:
            numeric = int(ref, 0)
        except ValueError:
            numeric = None
        if numeric is not None:
            for body in self.bodies:
                if body.body_id == numeric:
                    return body
            for body in self.bodies:
                if body.model_index == numeric:
                    return body
            for body in self.bodies:
                if body.dictionary_index == numeric:
                    return body
        for body in self.bodies:
            if body.name == ref:
                return body
        raise ReplayQueryError(f"body {ref!r} not found in dictionary")

    def read_frame(self, frame: FrameIndex, body_limit: int | None = 20) -> dict[str, object]:
        raw = self._chunk_bytes("PRES")
        if frame.presentation_offset + FRAME_HEADER.size > len(raw):
            raise ReplayQueryError("frame offset points outside PRES chunk")
        values = FRAME_HEADER.unpack_from(raw, frame.presentation_offset)
        (
            frame_index,
            scene_frame,
            time_seconds,
            dt,
            state_hash,
            contact_count,
            pipeline_record_count,
            checkpoint_boundary,
            flags,
            _reserved,
            gravity,
            fluid_height,
            fluid_density,
            eye_x,
            eye_y,
            eye_z,
            view_x,
            view_y,
            view_z,
            up_x,
            up_y,
            up_z,
            body_count,
        ) = values
        cursor = frame.presentation_offset + FRAME_HEADER.size
        body_records: list[dict[str, object]] = []
        limit = body_count if body_limit is None else min(body_count, max(body_limit, 0))
        for body_ordinal in range(body_count):
            if cursor + BODY_POSE.size > len(raw):
                raise ReplayQueryError("PRES chunk ended mid-body-pose")
            dictionary_index, px, py, pz, qx, qy, qz, qw = BODY_POSE.unpack_from(raw, cursor)
            cursor += BODY_POSE.size
            if body_ordinal >= limit:
                continue
            body = self.bodies[dictionary_index] if dictionary_index < len(self.bodies) else None
            body_records.append(
                {
                    "dictionaryIndex": dictionary_index,
                    "bodyId": body.body_id if body else None,
                    "modelIndex": body.model_index if body else None,
                    "name": body.name if body else "",
                    "shape": body.shape if body else "unknown",
                    "position": [round_float(px), round_float(py), round_float(pz)],
                    "orientation": [round_float(qx), round_float(qy), round_float(qz), round_float(qw)],
                }
            )

        return {
            "frameIndex": frame_index,
            "sceneFrame": scene_frame,
            "timeSeconds": round_float(time_seconds),
            "dt": round_float(dt),
            "stateHash": hash_text(state_hash),
            "contactCount": contact_count,
            "pipelineRecordCount": pipeline_record_count,
            "checkpointBoundary": bool(checkpoint_boundary),
            "world": {
                "gravity": round_float(gravity),
                "fluidHeight": round_float(fluid_height),
                "fluidDensity": round_float(fluid_density),
                "waterHidden": bool(flags & FLAG_WATER_HIDDEN),
                "terrainHidden": bool(flags & FLAG_TERRAIN_HIDDEN),
                "fixedStep": bool(flags & FLAG_FIXED_STEP),
                "scenePhysicsEnabled": bool(flags & FLAG_SCENE_PHYSICS),
                "sceneTextEnabled": bool(flags & FLAG_SCENE_TEXT),
            },
            "camera": {
                "eye": [round_float(eye_x), round_float(eye_y), round_float(eye_z)],
                "view": [round_float(view_x), round_float(view_y), round_float(view_z)],
                "up": [round_float(up_x), round_float(up_y), round_float(up_z)],
            },
            "bodyCount": body_count,
            "bodiesReturned": len(body_records),
            "bodies": body_records,
        }

    def body_samples(self, body: BodyInfo, frames: Iterable[FrameIndex], limit: int) -> list[dict[str, object]]:
        samples: list[dict[str, object]] = []
        raw = self._chunk_bytes("PRES")
        for frame in frames:
            if len(samples) >= limit:
                break
            if frame.presentation_offset + FRAME_HEADER.size > len(raw):
                raise ReplayQueryError("frame offset points outside PRES chunk")
            cursor = frame.presentation_offset + FRAME_HEADER.size
            for _ in range(frame.body_count):
                if cursor + BODY_POSE.size > len(raw):
                    raise ReplayQueryError("PRES chunk ended mid-body-pose")
                dictionary_index, px, py, pz, qx, qy, qz, qw = BODY_POSE.unpack_from(raw, cursor)
                cursor += BODY_POSE.size
                if dictionary_index != body.dictionary_index:
                    continue
                samples.append(
                    {
                        "frameIndex": frame.frame_index,
                        "bodyId": body.body_id,
                        "modelIndex": body.model_index,
                        "name": body.name,
                        "position": [round_float(px), round_float(py), round_float(pz)],
                        "orientation": [round_float(qx), round_float(qy), round_float(qz), round_float(qw)],
                    }
                )
                break
        return samples

    def hash_samples(self, frames: str | None, limit: int) -> list[dict[str, object]]:
        samples: list[dict[str, object]] = []
        for row in self.selected_hashes(frames):
            if len(samples) >= limit:
                break
            samples.append(
                {
                    "frameIndex": row.frame_index,
                    "sceneFrame": row.scene_frame,
                    "timeSeconds": round_float(row.time_seconds),
                    "presentationHash": hash_text(row.presentation_hash),
                    "solverHash": hash_text(row.solver_hash),
                    "bodyCount": row.body_count,
                    "contactCount": row.contact_count,
                    "pipelineRecordCount": row.pipeline_record_count,
                    "checkpointBoundary": row.checkpoint_boundary,
                }
            )
        return samples

    def branch_samples(self, limit: int) -> list[dict[str, object]]:
        samples: list[dict[str, object]] = []
        for row in self.branches:
            if len(samples) >= limit:
                break
            samples.append(
                {
                    "branchId": row.branch_id,
                    "parentBranchId": row.parent_branch_id,
                    "startFrame": row.start_frame,
                    "firstRetainedFrame": row.first_retained_frame,
                    "lastRetainedFrame": row.last_retained_frame,
                    "sourceFrame": row.source_frame,
                    "sourceSolverHash": hash_text(row.source_solver_hash) if row.has_source_hash else None,
                    "restored": row.restored,
                }
            )
        return samples

    def checkpoint_samples(self, frames: str | None, limit: int, body_limit: int) -> list[dict[str, object]]:
        samples: list[dict[str, object]] = []
        for row in self.selected_checkpoints(frames):
            if len(samples) >= limit:
                break
            samples.append(
                {
                    "frameIndex": row.frame_index,
                    "sceneFrame": row.scene_frame,
                    "timeSeconds": round_float(row.time_seconds),
                    "dt": round_float(row.dt),
                    "presentationHash": hash_text(row.presentation_hash),
                    "solverHash": hash_text(row.solver_hash),
                    "bodyCount": row.body_count,
                    "contactCount": row.contact_count,
                    "pipelineRecordCount": row.pipeline_record_count,
                    "checkpointBoundary": row.checkpoint_boundary,
                    "launcher": {
                        "rayLineCount": row.ray_line_count,
                        "laserShotCount": row.laser_shot_count,
                    },
                    "snapshot": {
                        "version": row.snapshot_version,
                        "modelCount": row.snapshot_model_count,
                        "persistentContactCount": row.persistent_contact_count,
                        "contactCacheCount": row.contact_cache_count,
                        "debugContactCount": row.debug_contact_count,
                        "pipelineTraceCount": row.pipeline_trace_count,
                        "collisionCellKeyCount": row.collision_cell_key_count,
                    },
                    "bodiesReturned": min(len(row.bodies), max(body_limit, 0)),
                    "bodies": row.bodies[: max(body_limit, 0)],
                }
            )
        return samples

    def export_skullscope(self, frames: list[FrameIndex], out_path: Path, run_id: str) -> int:
        out_path.parent.mkdir(parents=True, exist_ok=True)
        rows = 0
        with out_path.open("w", encoding="utf-8", newline="\n") as handle:
            run_row = {
                "kind": "run",
                "run": run_id,
                "scene": str(self.path),
                "fixed_step": 1,
                "renderer": "replay-v2",
                "solver": "presentation",
                "model_count": len(self.bodies),
                "frame_count": len(frames),
                "config": {"source": "replay_query export-skullscope", "track": "presentation"},
            }
            handle.write(json.dumps(run_row, separators=(",", ":")) + "\n")
            rows += 1
            for frame in frames:
                decoded = self.read_frame(frame, body_limit=None)
                frame_row = {
                    "kind": "frame",
                    "run": run_id,
                    "frame": decoded["frameIndex"],
                    "time_seconds": decoded["timeSeconds"],
                    "dt": decoded["dt"],
                    "body_count": decoded["bodyCount"],
                    "contact_count": decoded["contactCount"],
                }
                handle.write(json.dumps(frame_row, separators=(",", ":")) + "\n")
                rows += 1
                for body in decoded["bodies"]:
                    body_row = {
                        "kind": "body",
                        "run": run_id,
                        "frame": decoded["frameIndex"],
                        "body_id": body["bodyId"],
                        "name": body["name"],
                        "shape": body["shape"],
                        "pos": body["position"],
                        "q": body["orientation"],
                    }
                    handle.write(json.dumps(body_row, separators=(",", ":")) + "\n")
                    rows += 1
            handle.write(json.dumps({"kind": "end", "run": run_id}, separators=(",", ":")) + "\n")
            rows += 1
        return rows


def parse_frame_range(value: str) -> tuple[int, int]:
    if ":" not in value:
        frame = int(value, 0)
        return frame, frame
    start_text, end_text = value.split(":", 1)
    start = int(start_text, 0) if start_text else 0
    end = int(end_text, 0) if end_text else sys.maxsize
    if end < start:
        raise ReplayQueryError("--frames end must be greater than or equal to start")
    return start, end


def round_float(value: float) -> float:
    if not math.isfinite(value):
        return value
    return round(float(value), 6)


def print_json(payload: object) -> None:
    print(json.dumps(payload, indent=2, sort_keys=False))


def cmd_summary(replay: ReplayV2, _args: argparse.Namespace) -> None:
    print_json(replay.summary())


def cmd_frame(replay: ReplayV2, args: argparse.Namespace) -> None:
    frame = replay.find_frame(args.frame)
    limit = None if args.all_bodies else args.limit
    print_json(replay.read_frame(frame, body_limit=limit))


def cmd_body(replay: ReplayV2, args: argparse.Namespace) -> None:
    body = replay.resolve_body(args.body)
    frames = replay.selected_frames(args.frames)
    samples = replay.body_samples(body, frames, args.limit)
    print_json(
        {
            "body": {
                "dictionaryIndex": body.dictionary_index,
                "bodyId": body.body_id,
                "modelIndex": body.model_index,
                "name": body.name,
                "shape": body.shape,
            },
            "sampleCount": len(samples),
            "samples": samples,
        }
    )


def cmd_hashes(replay: ReplayV2, args: argparse.Namespace) -> None:
    samples = replay.hash_samples(args.frames, args.limit)
    print_json(
        {
            "hashCount": len(replay.solver_hashes),
            "samplesReturned": len(samples),
            "samples": samples,
        }
    )


def cmd_branches(replay: ReplayV2, args: argparse.Namespace) -> None:
    samples = replay.branch_samples(args.limit)
    print_json(
        {
            "branchCount": len(replay.branches),
            "samplesReturned": len(samples),
            "samples": samples,
            "note": None if replay.branches else "This v2 artifact does not include branch provenance chunks.",
        }
    )


def cmd_checkpoints(replay: ReplayV2, args: argparse.Namespace) -> None:
    samples = replay.checkpoint_samples(args.frames, args.limit, args.body_limit)
    print_json(
        {
            "checkpointCount": len(replay.solver_checkpoints),
            "samplesReturned": len(samples),
            "samples": samples,
            "note": None
            if replay.solver_checkpoints
            else "This v2 artifact does not include solver checkpoint chunks.",
        }
    )


def cmd_contacts(_replay: ReplayV2, args: argparse.Namespace) -> None:
    print_json(
        {
            "frames": args.frames,
            "contactCount": 0,
            "note": "Presentation v2 artifacts do not include standalone contact rows yet; use checkpoints or SkullScope traces when available.",
        }
    )


def cmd_export_skullscope(replay: ReplayV2, args: argparse.Namespace) -> None:
    frames = replay.selected_frames(args.frames)
    if not frames:
        raise ReplayQueryError("selected frame window is empty")
    rows = replay.export_skullscope(frames, Path(args.out), args.run_id)
    print_json(
        {
            "out": args.out,
            "run": args.run_id,
            "frames": len(frames),
            "rows": rows,
        }
    )


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("replay", help="Path to a .skreplay v2 binary artifact")
    subparsers = parser.add_subparsers(dest="command", required=True)

    summary = subparsers.add_parser("summary", help="Print bounded artifact metadata")
    summary.set_defaults(func=cmd_summary)

    frame = subparsers.add_parser("frame", help="Print one frame by replay frame index")
    frame.add_argument("frame", type=lambda text: int(text, 0))
    frame.add_argument("--limit", type=int, default=20, help="Body records to print")
    frame.add_argument("--all-bodies", action="store_true", help="Print every body in the frame")
    frame.set_defaults(func=cmd_frame)

    body = subparsers.add_parser("body", help="Print sampled pose rows for one body")
    body.add_argument("body", help="Body id, model index, dictionary index, or exact body name")
    body.add_argument("--frames", help="Inclusive replay frame window, for example 1200:1260")
    body.add_argument("--limit", type=int, default=120)
    body.set_defaults(func=cmd_body)

    hashes = subparsers.add_parser("hashes", help="Print per-frame presentation and solver hashes")
    hashes.add_argument("--frames", help="Inclusive replay frame window, for example 1200:1260")
    hashes.add_argument("--limit", type=int, default=120)
    hashes.set_defaults(func=cmd_hashes)

    branches = subparsers.add_parser("branches", help="Print saved branch provenance records")
    branches.add_argument("--limit", type=int, default=20)
    branches.set_defaults(func=cmd_branches)

    checkpoints = subparsers.add_parser("checkpoints", help="Print sparse solver checkpoint summaries")
    checkpoints.add_argument("--frames", help="Inclusive replay frame window, for example 1200:1260")
    checkpoints.add_argument("--limit", type=int, default=20)
    checkpoints.add_argument("--body-limit", type=int, default=3)
    checkpoints.set_defaults(func=cmd_checkpoints)

    contacts = subparsers.add_parser("contacts", help="Report contact availability for a frame window")
    contacts.add_argument("--frames", help="Inclusive replay frame window")
    contacts.set_defaults(func=cmd_contacts)

    export = subparsers.add_parser("export-skullscope", help="Export a bounded SkullScope-compatible NDJSON slice")
    export.add_argument("--frames", required=True, help="Inclusive replay frame window, for example 1200:1260")
    export.add_argument("--out", required=True, help="Destination .physicsdiag.ndjson path")
    export.add_argument("--run-id", default="replay_v2_slice")
    export.set_defaults(func=cmd_export_skullscope)

    return parser


def main(argv: list[str]) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        replay = ReplayV2(Path(args.replay))
        args.func(replay, args)
    except (OSError, ValueError, ReplayQueryError) as exc:
        print(f"replay_query: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
