#!/usr/bin/env python3
#
# File: tools/replay_query.py
# Purpose:
#   Query the versioned ReplayV2Artifact file family without loading the full
#   binary artifact into GPT or validation output.
#
# Summary:
#   Replay artifacts are chunked binary files. Parsing keeps their bytes local;
#   commands emit bounded table/range selections as text or JSON for validation,
#   debugging, and agent analysis.
#
# Glossary:
#   ReplayV2Artifact: Established API/file-family name for chunked .skreplay
#     files; each file header declares its actual wire version.
#   Chunk table: Header directory that names each stored replay section.
#   Visual-state row: Versioned per-body packet holding every replay-owned field
#     needed to reproduce the saved presentation hash.
#   Scene object id: Durable cross-system identity encoded as a fixed-width
#     scalar at this cold artifact boundary.
#   SkullScope slice: Bounded NDJSON export derived from selected replay frames.
#
# Invariants:
#   - Binary struct layouts must match the runtime replay writer.
#   - Decoded identity labels use the scene-owned name even though the artifact
#     stores only the unchanged integer scalar.
#   - Query commands must stay bounded; callers should not dump whole artifacts
#     into model context.
#
# Related:
#   - tools/replay_query.bat
#   - tools/check_replay_v2_artifact.py
#   - SkullbonezSource/Runtime/Replay/ReplayEventCommand.h
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
F64 = struct.Struct("<d")
PAIR_I32 = struct.Struct("<ii")
BODY_RECORD_V2 = struct.Struct("<IiB3s64s")
BODY_RECORD_V3 = struct.Struct("<IiBB2sf64s")
FRAME_HEADER = struct.Struct("<QidfQHHBBH" + ("f" * 12) + "I")
BODY_POSE_V2 = struct.Struct("<Ifffffff")
BODY_VISUAL_STATE_V3 = struct.Struct("<I" + ("f" * 13) + "B3siHHff")
BRANCH_RECORD = struct.Struct("<IIQQQQQIIQ")
EVENT_RECORD = struct.Struct("<QIIIHHIiiiiQQQ128sI")
EVENT_CURSOR_RECORD = struct.Struct("<QIIQ")
HASH_RECORD = struct.Struct("<QidQQIHHB3s")
CHECKPOINT_HEADER = struct.Struct("<QidfQQHHBBH")
LAUNCHER_HEADER = struct.Struct("<iiBB2sff")
TORNADO_CONFIG = struct.Struct("<BB2s" + ("f" * 14))
TORNADO_SYSTEM_HEADER = struct.Struct("<BB2sI")
TORNADO_VORTEX_CONFIG_BYTES = TORNADO_CONFIG.size + COUNTED_FLOAT.size * 9
SOLVER_STATS = struct.Struct("<iiiiiiiff")
# Nested solver-snapshot v3 joint order: topology ordinal, endpoint scene ids,
# two local anchors, slack/stiffness/damping/accumulated impulse, group, flags.
# Snapshot v4 appends counted uint8 motion-eligibility state after these rows;
# v5 widens tornado elapsed time and v6 widens sleep counters to uint32.
# the nested schema is independent of outer replay artifact versions 2 through 5.
POINT_JOINT_RECORD = struct.Struct("<3I10fIB")
SOLVER_BODY = struct.Struct("<I" + ("f" * 21) + "5s3siHHff")
VISUAL_PACKET_RECORD = struct.Struct("<" + ("Q" * 5) + ("I" * 9) + ("f" * 6) + ("Q" * 18) + ("I" * 13))

FLAG_WATER_HIDDEN = 1 << 0
FLAG_TERRAIN_HIDDEN = 1 << 1
FLAG_FIXED_STEP = 1 << 2
FLAG_SCENE_PHYSICS = 1 << 3
FLAG_SCENE_TEXT = 1 << 4
PRESENTATION_PACKET_FNV_OFFSET = 1469598103934665603
PRESENTATION_PACKET_FNV_PRIME = 1099511628211


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
    mass: float = 0.0
    fixed: bool = False

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
class VisualPacketInfo:
    source_frame: int
    reveal_frame: int
    semantic_hash: int
    visual_state_hash: int
    exact_packet_hash: int
    schema_version: int
    target_id: int
    branch_id: int
    event_cursor: int
    topology_version: int
    published_frame_count: int
    prediction_enabled: int
    prediction_building: int
    prediction_complete: int
    camera_eye_x: float
    camera_eye_y: float
    camera_eye_z: float
    camera_up_x: float
    camera_up_y: float
    camera_up_z: float
    combined_line_hash: int
    ordinary_line_hash: int
    priority_line_hash: int
    priority_line_canonical_hash: int
    ordinary_ribbon_hash: int
    priority_ribbon_hash: int
    priority_ribbon_canonical_hash: int
    expanded_vertex_hash: int
    ordinary_expanded_vertex_hash: int
    dropped_segment_count: int
    replay_reserve_growth_events: int
    combined_line_bytes: int
    ordinary_line_bytes: int
    priority_line_bytes: int
    ordinary_ribbon_bytes: int
    priority_ribbon_bytes: int
    expanded_vertex_bytes: int
    ordinary_expanded_vertex_bytes: int
    has_geometry: int
    trajectory_record_count: int
    future_node_count: int
    retained_marker_count: int
    ghost_request_count: int
    combined_line_vertex_count: int
    ordinary_line_vertex_count: int
    priority_line_vertex_count: int
    ordinary_ribbon_segment_count: int
    priority_ribbon_segment_count: int
    expanded_vertex_count: int
    ordinary_expanded_vertex_count: int
    segment_count: int


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
class EventInfo:
    frame_index: int
    sequence: int
    branch_id: int
    parent_branch_id: int
    kind: int
    payload_version: int
    flags: int
    values: tuple[int, int, int, int]
    data0: int
    source_frame: int
    source_solver_hash: int
    text: str

    @property
    def kind_name(self) -> str:
        # Wire kind 2 belonged to the deleted omnibus runtime-command queue.
        # Keeping it unmapped is deliberate: old mixed-owner events must be
        # reported as unsupported instead of acquiring a compatibility alias.
        return {
            1: "timelineStart",
            3: "branchRestore",
            4: "worldOverride",
            5: "launcherConfig",
            6: "launcherFire",
            7: "generatedSceneConfig",
            8: "editorPlace",
            9: "editorTransform",
            10: "ownerAction",
        }.get(self.kind, "unknown")


@dataclass
class EventCursorInfo:
    frame_index: int
    event_cursor: int
    flags: int
    solver_hash: int


@dataclass
class SolverCheckpointInfo:
    frame_index: int
    scene_frame: int
    time_seconds: float
    dt: float
    presentation_hash: int
    solver_hash: int
    event_cursor: int
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
    point_joint_count: int
    motion_eligibility_state_count: int
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


def float32_bits_text(value: float) -> str:
    return f"0x{struct.unpack('<I', struct.pack('<f', value))[0]:08X}"


def float64_bits_text(value: float) -> str:
    return f"0x{struct.unpack('<Q', struct.pack('<d', value))[0]:016X}"


def float_from_i32_bits(value: int) -> float:
    return struct.unpack("<f", struct.pack("<I", value & 0xFFFFFFFF))[0]


def decode_ray9_payload(text: str) -> dict[str, object] | None:
    prefix = "ray9:"
    payload = text[len(prefix) :] if text.startswith(prefix) else ""
    if len(payload) != 72:
        return None
    try:
        floats = [
            struct.unpack("<f", struct.pack("<I", int(payload[i : i + 8], 16)))[0]
            for i in range(0, len(payload), 8)
        ]
    except ValueError:
        return None
    return {
        "origin": [round_float(v) for v in floats[0:3]],
        "direction": [round_float(v) for v in floats[3:6]],
        "cameraUp": [round_float(v) for v in floats[6:9]],
    }


def decode_place6_payload(text: str) -> dict[str, object] | None:
    prefix = "place6:"
    payload = text[len(prefix) :] if text.startswith(prefix) else ""
    if len(payload) != 48:
        return None
    try:
        floats = [
            struct.unpack("<f", struct.pack("<I", int(payload[i : i + 8], 16)))[0]
            for i in range(0, len(payload), 8)
        ]
    except ValueError:
        return None
    return {
        "terrainPoint": [round_float(v) for v in floats[0:3]],
        "placementScale": [round_float(v) for v in floats[3:6]],
    }


def decode_transform_payload(text: str) -> dict[str, object] | None:
    prefix7 = "xform7:"
    prefix8 = "xform8:"
    value_count = 0
    if text.startswith(prefix7):
        payload = text[len(prefix7) :]
        value_count = 7
    elif text.startswith(prefix8):
        payload = text[len(prefix8) :]
        value_count = 8
    else:
        payload = ""
    if len(payload) != value_count * 8:
        return None
    try:
        floats = [
            struct.unpack("<f", struct.pack("<I", int(payload[i : i + 8], 16)))[0]
            for i in range(0, len(payload), 8)
        ]
    except ValueError:
        return None
    return {
        "position": [round_float(v) for v in floats[0:3]],
        "orientation": [round_float(v) for v in floats[3:7]],
        "scaleFactor": round_float(floats[7]) if value_count == 8 else None,
    }


def decoded_event_payload(row: EventInfo) -> dict[str, object] | None:
    if row.kind == 10:
        owner_action_names = {
            1001: "SceneLoadBrowserIndex",
            1002: "SceneLoadDemo",
            1003: "SceneReset",
            1004: "SceneCreate",
            1005: "SceneSaveDefaults",
            2001: "CaptureScreenshot",
            3001: "RenderSaveOrdinaryDefaults",
            3002: "RenderSaveCinematicDefaults",
        }
        return {
            "ownerAction": owner_action_names.get(row.values[0], "Unknown"),
            "ownerActionCode": row.values[0],
            "index": row.values[1],
            "flags": row.flags,
            "text": row.text,
        }
    if row.kind == 4:
        return {
            "gravity": round_float(float_from_i32_bits(row.values[0])),
            "fluidHeight": round_float(float_from_i32_bits(row.values[1])),
            "fluidDensity": round_float(float_from_i32_bits(row.values[2])),
            "changed": {
                "gravity": bool(row.flags & 1),
                "fluidHeight": bool(row.flags & 2),
                "fluidDensity": bool(row.flags & 4),
            },
        }
    if row.kind == 5:
        return {
            "impulseStrength": round_float(float_from_i32_bits(row.values[0])),
            "projectileSpeed": round_float(float_from_i32_bits(row.values[1])),
            "changed": {
                "impulseStrength": bool(row.flags & 1),
                "projectileSpeed": bool(row.flags & 2),
            },
        }
    if row.kind == 6:
        decoded = decode_ray9_payload(row.text) or {}
        decoded.update(
            {
                "mode": "projectile" if row.values[0] else "laser",
                "impulseStrength": round_float(float_from_i32_bits(row.values[1])),
                "projectileSpeed": round_float(float_from_i32_bits(row.values[2])),
                "modelCountBeforeFire": row.values[3],
            }
        )
        return decoded
    if row.kind == 7:
        override_id = (row.flags >> 8) & 0x3
        return {
            "modelCount": row.values[0],
            "solverBallCount": row.values[1],
            "solverBoxCount": row.values[2],
            "rngSeed": row.values[3],
            "exactSolverCounts": bool(row.flags & 1),
            "uiModelCountOverride": bool(row.flags & 2),
            "uiSolverCountOverride": bool(row.flags & 4),
            "objectTypeOverride": {0: "mixed", 1: "allBalls", 2: "allBoxes"}.get(override_id, "unknown"),
        }
    if row.kind == 8:
        decoded = decode_place6_payload(row.text) or {}
        decoded.update(
            {
                "objectType": row.values[0],
                "fixed": bool(row.flags & 1),
                "terrainAlign": bool(row.flags & 2),
                "modelCountBeforePlace": row.values[3],
            }
        )
        return decoded
    if row.kind == 9:
        decoded = decode_transform_payload(row.text) or {}
        decoded.update(
            {
                "modelIndex": row.values[0],
                "sceneObjectId": row.values[1],
                "modelCountAtCommit": row.values[2],
                "scaleAxis": row.values[3] if row.flags & 4 else None,
                "translated": bool(row.flags & 1),
                "rotated": bool(row.flags & 2),
                "scaled": bool(row.flags & 4),
            }
        )
        return decoded
    return None


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
        self.events: list[EventInfo] = []
        self.event_cursors: list[EventCursorInfo] = []
        self.solver_hashes: list[SolverHashInfo] = []
        self.solver_checkpoints: list[SolverCheckpointInfo] = []
        self.visual_packets: list[VisualPacketInfo] = []
        self._parse_header()
        self._parse_manifest()
        self._parse_bodies()
        self._parse_index()
        self._parse_branches()
        self._parse_events()
        self._parse_event_cursors()
        self._parse_solver_hashes()
        self._parse_solver_checkpoints()
        self._parse_visual_packets()

    def _parse_header(self) -> None:
        if len(self.data) < HEADER.size:
            raise ReplayQueryError("file is too small to be a replay v2 artifact")
        magic, version, header_size, chunk_count, flags, table_offset, file_size = HEADER.unpack_from(self.data, 0)
        if magic != MAGIC:
            if self.data[:1] == b"{":
                raise ReplayQueryError("this is a legacy JSON replay artifact, not v2 binary")
            raise ReplayQueryError("unrecognized replay magic")
        if version not in (2, 3, 4, 5):
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
        record_struct = BODY_RECORD_V3 if self.version >= 3 else BODY_RECORD_V2
        for dictionary_index in range(body_count):
            if cursor + record_struct.size > len(raw):
                raise ReplayQueryError("BODY chunk ended mid-record")
            if self.version >= 3:
                body_id, model_index, shape_kind, fixed, _reserved, mass, name_raw = record_struct.unpack_from(raw, cursor)
            else:
                body_id, model_index, shape_kind, _reserved, name_raw = record_struct.unpack_from(raw, cursor)
                mass = 0.0
                fixed = 0
            cursor += record_struct.size
            bodies.append(
                BodyInfo(
                    dictionary_index=dictionary_index,
                    body_id=body_id,
                    model_index=model_index,
                    shape_kind=shape_kind,
                    name=clean_name(name_raw),
                    mass=mass,
                    fixed=bool(fixed),
                )
            )
        if cursor != len(raw):
            raise ReplayQueryError("BODY chunk has trailing or version-mismatched bytes")
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

    def _parse_events(self) -> None:
        chunk = self.chunks.get("EVNT")
        if not chunk:
            self.events = []
            return
        raw = read_exact_range(self.data, chunk.offset, chunk.size, "EVNT")
        if len(raw) < 4:
            raise ReplayQueryError("EVNT chunk is too small")
        event_count = struct.unpack_from("<I", raw, 0)[0]
        if event_count != chunk.record_count:
            raise ReplayQueryError("EVNT chunk count does not match chunk table")
        cursor = 4
        events: list[EventInfo] = []
        for _ in range(event_count):
            if cursor + EVENT_RECORD.size > len(raw):
                raise ReplayQueryError("EVNT chunk ended mid-record")
            (
                frame_index,
                sequence,
                branch_id,
                parent_branch_id,
                kind,
                payload_version,
                flags,
                value0,
                value1,
                value2,
                value3,
                data0,
                source_frame,
                source_solver_hash,
                text_raw,
                _reserved,
            ) = EVENT_RECORD.unpack_from(raw, cursor)
            cursor += EVENT_RECORD.size
            events.append(
                EventInfo(
                    frame_index=int(frame_index),
                    sequence=int(sequence),
                    branch_id=int(branch_id),
                    parent_branch_id=int(parent_branch_id),
                    kind=int(kind),
                    payload_version=int(payload_version),
                    flags=int(flags),
                    values=(int(value0), int(value1), int(value2), int(value3)),
                    data0=int(data0),
                    source_frame=int(source_frame),
                    source_solver_hash=int(source_solver_hash),
                    text=clean_name(text_raw),
                )
            )
        if cursor != len(raw):
            raise ReplayQueryError("EVNT chunk has trailing bytes")
        self.events = events

    def _parse_event_cursors(self) -> None:
        chunk = self.chunks.get("ECUR")
        if not chunk:
            self.event_cursors = []
            return
        raw = read_exact_range(self.data, chunk.offset, chunk.size, "ECUR")
        if len(raw) < 4:
            raise ReplayQueryError("ECUR chunk is too small")
        cursor_count = struct.unpack_from("<I", raw, 0)[0]
        if cursor_count != chunk.record_count:
            raise ReplayQueryError("ECUR chunk count does not match chunk table")
        cursor = 4
        rows: list[EventCursorInfo] = []
        for _ in range(cursor_count):
            if cursor + EVENT_CURSOR_RECORD.size > len(raw):
                raise ReplayQueryError("ECUR chunk ended mid-record")
            frame_index, event_cursor, flags, solver_hash = EVENT_CURSOR_RECORD.unpack_from(raw, cursor)
            cursor += EVENT_CURSOR_RECORD.size
            rows.append(
                EventCursorInfo(
                    frame_index=int(frame_index),
                    event_cursor=int(event_cursor),
                    flags=int(flags),
                    solver_hash=int(solver_hash),
                )
            )
        if cursor != len(raw):
            raise ReplayQueryError("ECUR chunk has trailing bytes")
        self.event_cursors = rows

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
        if version < 1 or version > 6:
            raise ReplayQueryError(f"unsupported solver snapshot version {version}")
        tornado_system_vortex_count = 0
        if version >= 2:
            _enabled, _visualize_velocity_field, _reserved, tornado_system_vortex_count = reader.unpack(
                TORNADO_SYSTEM_HEADER
            )
            reader.skip(TORNADO_VORTEX_CONFIG_BYTES * tornado_system_vortex_count)
            reader.unpack(F64 if version >= 5 else COUNTED_FLOAT)
        ReplayV2._skip_counted(reader, COUNTED_FLOAT)
        ReplayV2._skip_counted(reader, COUNTED_U8)
        ReplayV2._skip_counted(reader, COUNTED_U8)
        ReplayV2._skip_counted(reader, COUNTED_U8)
        sleep_counter_count = ReplayV2._skip_counted(reader, U32 if version >= 6 else COUNTED_U8)
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
        point_joint_count = ReplayV2._skip_counted(reader, POINT_JOINT_RECORD) if version >= 3 else 0
        motion_eligibility_state_count = ReplayV2._skip_counted(reader, COUNTED_U8) if version >= 4 else 0
        sleep_pose_anchor_position_count = ReplayV2._skip_counted(reader, struct.Struct("<3f")) if version >= 6 else 0
        sleep_pose_anchor_orientation_count = ReplayV2._skip_counted(reader, struct.Struct("<4f")) if version >= 6 else 0
        sleep_pose_anchor_valid_count = ReplayV2._skip_counted(reader, COUNTED_U8) if version >= 6 else 0
        return {
            "version": int(version),
            "modelCount": int(model_count),
            "persistentContactCount": persistent_contact_count,
            "contactCacheCount": contact_cache_count,
            "debugContactCount": debug_contact_count,
            "pipelineTraceCount": pipeline_trace_count,
            "collisionCellKeyCount": collision_cell_key_count,
            "pointJointCount": point_joint_count,
            "motionEligibilityStateCount": motion_eligibility_state_count,
            "sleepPoseAnchorPositionCount": sleep_pose_anchor_position_count,
            "sleepPoseAnchorOrientationCount": sleep_pose_anchor_orientation_count,
            "sleepPoseAnchorValidCount": sleep_pose_anchor_valid_count,
            "sleepCounterCount": sleep_counter_count,
            "tornadoSystemVortexCount": tornado_system_vortex_count,
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

    def _event_cursor_for_checkpoint(self, frame_index: int, solver_hash: int) -> int:
        for row in self.event_cursors:
            if row.frame_index == frame_index and (row.solver_hash == 0 or row.solver_hash == solver_hash):
                return row.event_cursor
        return 0

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
                    event_cursor=self._event_cursor_for_checkpoint(int(frame_index), int(solver_hash)),
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
                    point_joint_count=snapshot["pointJointCount"],
                    motion_eligibility_state_count=snapshot["motionEligibilityStateCount"],
                    bodies=body_records,
                )
            )
        if reader.offset != len(raw):
            raise ReplayQueryError("SCHK chunk has trailing bytes")
        self.solver_checkpoints = checkpoints

    def _parse_visual_packets(self) -> None:
        chunk = self.chunks.get("RVIS")
        if not chunk:
            self.visual_packets = []
            return
        raw = read_exact_range(self.data, chunk.offset, chunk.size, "RVIS")
        if len(raw) < 4:
            raise ReplayQueryError("RVIS chunk is truncated")
        packet_count = U32.unpack_from(raw, 0)[0]
        if packet_count != chunk.record_count:
            raise ReplayQueryError("RVIS chunk count does not match chunk table")
        expected_bytes = 4 + packet_count * VISUAL_PACKET_RECORD.size
        if len(raw) != expected_bytes:
            raise ReplayQueryError("RVIS chunk has trailing or version-mismatched bytes")
        packets: list[VisualPacketInfo] = []
        cursor = 4
        for index in range(packet_count):
            values = VISUAL_PACKET_RECORD.unpack_from(raw, cursor)
            cursor += VISUAL_PACKET_RECORD.size
            packet = VisualPacketInfo(*values)
            if packet.reveal_frame != index or packet.source_frame <= 0:
                raise ReplayQueryError(f"RVIS row {index} has invalid frame identity")
            if packet.semantic_hash == 0 or packet.visual_state_hash == 0 or packet.exact_packet_hash == 0:
                raise ReplayQueryError(f"RVIS row {index} has an empty packet hash")
            if packet.schema_version != 1:
                raise ReplayQueryError(
                    f"ticks[{index}].schemaVersion is invalid: {packet.schema_version}"
                )
            if packet.target_id <= 0:
                raise ReplayQueryError(
                    f"ticks[{index}].targetId is invalid: {packet.target_id}"
                )
            if (
                packet.prediction_enabled not in (0, 1)
                or packet.prediction_building not in (0, 1)
                or packet.prediction_complete not in (0, 1)
                or packet.has_geometry not in (0, 1)
            ):
                raise ReplayQueryError(f"RVIS row {index} has invalid prediction flags")
            packets.append(packet)
        self.visual_packets = packets

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
            "eventCount": len(self.events),
            "eventCursorCount": len(self.event_cursors),
            "solverHashCount": len(self.solver_hashes),
            "solverCheckpointCount": len(self.solver_checkpoints),
            "visualPacketCount": len(self.visual_packets),
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
            "bodyDictionaryEntryBytes": self.manifest.get("bodyDictionaryEntryBytes"),
            "bodyPoseBytes": self.manifest.get("bodyPoseBytes"),
            "branchEntryBytes": self.manifest.get("branchEntryBytes", 0),
            "eventEntryBytes": self.manifest.get("eventEntryBytes", 0),
            "eventCursorEntryBytes": self.manifest.get("eventCursorEntryBytes", 0),
            "solverHashBytes": self.manifest.get("solverHashBytes", 0),
            "solverBodyBytes": self.manifest.get("solverBodyBytes", 0),
            "visualPacketEntryBytes": self.manifest.get("visualPacketEntryBytes", 0),
            "visualPredictionBytes": self.manifest.get("visualPredictionBytes", 0),
            "visualPredictionHash": self.manifest.get("visualPredictionHash", 0),
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

    def selected_events(self, frame_range: str | None) -> list[EventInfo]:
        if not frame_range:
            return list(self.events)
        start, end = parse_frame_range(frame_range)
        return [row for row in self.events if start <= row.frame_index <= end]

    def selected_event_cursors(self, frame_range: str | None) -> list[EventCursorInfo]:
        if not frame_range:
            return list(self.event_cursors)
        start, end = parse_frame_range(frame_range)
        return [row for row in self.event_cursors if start <= row.frame_index <= end]

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
        body_struct = BODY_VISUAL_STATE_V3 if self.version >= 3 else BODY_POSE_V2
        for body_ordinal in range(body_count):
            if cursor + body_struct.size > len(raw):
                raise ReplayQueryError("PRES chunk ended mid-body-pose")
            values = body_struct.unpack_from(raw, cursor)
            cursor += body_struct.size
            dictionary_index, px, py, pz, qx, qy, qz, qw = values[:8]
            if self.version >= 3:
                (
                    lvx,
                    lvy,
                    lvz,
                    avx,
                    avy,
                    avz,
                    visual_flags,
                    _reserved_flags,
                    sleep_island_visual_id,
                    body_contact_count,
                    _reserved_contact,
                    max_penetration,
                    normal_impulse_sum,
                ) = values[8:]
            else:
                lvx = lvy = lvz = avx = avy = avz = 0.0
                visual_flags = sleep_island_visual_id = body_contact_count = 0
                max_penetration = normal_impulse_sum = 0.0
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
                    "mass": round_float(body.mass) if body else 0.0,
                    "fixed": body.fixed if body else False,
                    "position": [round_float(px), round_float(py), round_float(pz)],
                    "orientation": [round_float(qx), round_float(qy), round_float(qz), round_float(qw)],
                    "linearVelocity": [round_float(lvx), round_float(lvy), round_float(lvz)],
                    "angularVelocity": [round_float(avx), round_float(avy), round_float(avz)],
                    "sleeping": bool(visual_flags & 1),
                    "sleepSupported": bool(visual_flags & 2),
                    "sleepInhibited": bool(visual_flags & 4),
                    "collisionContact": bool(visual_flags & 8),
                    "sleepIslandVisualId": sleep_island_visual_id,
                    "contactCount": body_contact_count,
                    "maxPenetration": round_float(max_penetration),
                    "normalImpulseSum": round_float(normal_impulse_sum),
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

    def presentation_packet_hashes(self) -> list[dict[str, object]]:
        """Hash the exact v3+ body fields shared with prediction/live packets."""
        if self.version < 3:
            raise ReplayQueryError("exact presentation packet hashes require replay version 3 or newer")

        def append_bytes(value: int, payload: bytes) -> int:
            for byte in payload:
                value ^= byte
                value = (value * PRESENTATION_PACKET_FNV_PRIME) & 0xFFFFFFFFFFFFFFFF
            return value

        raw = self._chunk_bytes("PRES")
        hashes: list[dict[str, object]] = []
        for frame in self.frames:
            cursor = frame.presentation_offset + FRAME_HEADER.size
            value = append_bytes(PRESENTATION_PACKET_FNV_OFFSET, struct.pack("<Q", frame.body_count))
            for _ in range(frame.body_count):
                if cursor + BODY_VISUAL_STATE_V3.size > len(raw):
                    raise ReplayQueryError("PRES chunk ended mid-v3 visual-state record")
                dictionary_index = struct.unpack_from("<I", raw, cursor)[0]
                if dictionary_index >= len(self.bodies):
                    raise ReplayQueryError("PRES visual-state record has an invalid dictionary index")
                body = self.bodies[dictionary_index]
                value = append_bytes(value, struct.pack("<I", body.body_id))
                value = append_bytes(value, struct.pack("<i", body.model_index))
                # Wire offsets are dictionary index, position, orientation,
                # linear velocity. Hash the original float bytes so no Python
                # conversion can canonicalize a bit pattern.
                value = append_bytes(value, raw[cursor + 4 : cursor + 44])
                cursor += BODY_VISUAL_STATE_V3.size
            hashes.append(
                {
                    "frameIndex": frame.frame_index,
                    "bodyCount": frame.body_count,
                    "hash": hash_text(value),
                }
            )
        return hashes

    def presentation_frame_headers(self) -> list[dict[str, object]]:
        """Return exact, ordered presentation headers without decoding body rows."""
        raw = self._chunk_bytes("PRES")
        rows: list[dict[str, object]] = []
        for ordinal, frame in enumerate(self.frames):
            if frame.presentation_offset + FRAME_HEADER.size > len(raw):
                raise ReplayQueryError(f"frame header {ordinal} points outside PRES chunk")
            values = FRAME_HEADER.unpack_from(raw, frame.presentation_offset)
            frame_index = int(values[0])
            body_count = int(values[22])
            if frame_index != frame.frame_index or body_count != frame.body_count:
                raise ReplayQueryError(
                    f"frame header/index mismatch at ordinal {ordinal}: "
                    f"index_frame={frame.frame_index} header_frame={frame_index} "
                    f"index_bodies={frame.body_count} header_bodies={body_count}"
                )
            flags = int(values[8])
            rows.append(
                {
                    "frameIndex": frame_index,
                    "sceneFrame": int(values[1]),
                    "timeSecondsBits": float64_bits_text(float(values[2])),
                    "dtBits": float32_bits_text(float(values[3])),
                    "stateHash": hash_text(int(values[4])),
                    "contactCount": int(values[5]),
                    "pipelineRecordCount": int(values[6]),
                    "checkpointBoundary": bool(values[7]),
                    "fixedStep": bool(flags & FLAG_FIXED_STEP),
                    "worldFlags": flags,
                    "gravityBits": float32_bits_text(float(values[10])),
                    "fluidHeightBits": float32_bits_text(float(values[11])),
                    "fluidDensityBits": float32_bits_text(float(values[12])),
                    "cameraEyeBits": [float32_bits_text(float(value)) for value in values[13:16]],
                    "cameraViewBits": [float32_bits_text(float(value)) for value in values[16:19]],
                    "cameraUpBits": [float32_bits_text(float(value)) for value in values[19:22]],
                    "bodyCount": body_count,
                }
            )
        return rows

    def body_samples(self, body: BodyInfo, frames: Iterable[FrameIndex], limit: int) -> list[dict[str, object]]:
        samples: list[dict[str, object]] = []
        raw = self._chunk_bytes("PRES")
        body_struct = BODY_VISUAL_STATE_V3 if self.version >= 3 else BODY_POSE_V2
        for frame in frames:
            if len(samples) >= limit:
                break
            if frame.presentation_offset + FRAME_HEADER.size > len(raw):
                raise ReplayQueryError("frame offset points outside PRES chunk")
            cursor = frame.presentation_offset + FRAME_HEADER.size
            for _ in range(frame.body_count):
                if cursor + body_struct.size > len(raw):
                    raise ReplayQueryError("PRES chunk ended mid-body-pose")
                values = body_struct.unpack_from(raw, cursor)
                cursor += body_struct.size
                dictionary_index, px, py, pz, qx, qy, qz, qw = values[:8]
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

    def event_samples(self, frames: str | None, limit: int) -> list[dict[str, object]]:
        samples: list[dict[str, object]] = []
        for row in self.selected_events(frames):
            if len(samples) >= limit:
                break
            decoded = decoded_event_payload(row)
            samples.append(
                {
                    "frameIndex": row.frame_index,
                    "sequence": row.sequence,
                    "branchId": row.branch_id,
                    "parentBranchId": row.parent_branch_id,
                    "kind": row.kind_name,
                    "kindId": row.kind,
                    "payloadVersion": row.payload_version,
                    "flags": row.flags,
                    "values": list(row.values),
                    "data0": hash_text(row.data0),
                    "sourceFrame": row.source_frame,
                    "sourceSolverHash": hash_text(row.source_solver_hash),
                    "text": row.text,
                    "decoded": decoded,
                }
            )
        return samples

    def event_cursor_samples(self, frames: str | None, limit: int) -> list[dict[str, object]]:
        samples: list[dict[str, object]] = []
        for row in self.selected_event_cursors(frames):
            if len(samples) >= limit:
                break
            samples.append(
                {
                    "frameIndex": row.frame_index,
                    "eventCursor": row.event_cursor,
                    "flags": row.flags,
                    "solverHash": hash_text(row.solver_hash),
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
                    "eventCursor": row.event_cursor,
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
                        "pointJointCount": row.point_joint_count,
                        "motionEligibilityStateCount": row.motion_eligibility_state_count,
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


def cmd_events(replay: ReplayV2, args: argparse.Namespace) -> None:
    samples = replay.event_samples(args.frames, args.limit)
    print_json(
        {
            "eventCount": len(replay.events),
            "samplesReturned": len(samples),
            "samples": samples,
            "note": None if replay.events else "This v2 artifact does not include replay event chunks.",
        }
    )


def cmd_event_cursors(replay: ReplayV2, args: argparse.Namespace) -> None:
    samples = replay.event_cursor_samples(args.frames, args.limit)
    print_json(
        {
            "eventCursorCount": len(replay.event_cursors),
            "samplesReturned": len(samples),
            "samples": samples,
            "note": None if replay.event_cursors else "This v2 artifact does not include checkpoint event cursors.",
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

    events = subparsers.add_parser("events", help="Print saved timeline/runtime event records")
    events.add_argument("--frames", help="Inclusive replay frame window, for example 1200:1260")
    events.add_argument("--limit", type=int, default=20)
    events.set_defaults(func=cmd_events)

    event_cursors = subparsers.add_parser("event-cursors", help="Print checkpoint event cursor records")
    event_cursors.add_argument("--frames", help="Inclusive replay frame window, for example 1200:1260")
    event_cursors.add_argument("--limit", type=int, default=20)
    event_cursors.set_defaults(func=cmd_event_cursors)

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
