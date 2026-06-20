# File: tools/bake_hulls.py
# Purpose: Serialize convex hull runtime topology and mass data into .hull files.

from __future__ import annotations

import argparse
import math
import struct
import sys
from dataclasses import dataclass
from pathlib import Path


MAX_VERTICES = 64
MAX_FACES = 96
MAX_EDGES = 160
MAX_FACE_VERTICES = 16
MAX_FACE_INDICES = MAX_FACES * MAX_FACE_VERTICES
TOLERANCE = 0.00005


@dataclass
class SourceHull:
    path: Path
    name: str
    vertices: list[tuple[float, float, float]]
    faces: list[list[int]]


@dataclass
class BakedFace:
    normal: tuple[float, float, float]
    plane_offset: float
    indices: list[int]


@dataclass
class BakedEdge:
    vertex_a: int
    vertex_b: int
    face_a: int
    face_b: int


@dataclass
class BakedHull:
    source: SourceHull
    source_hash: int
    center_of_mass: tuple[float, float, float]
    volume: float
    centered_vertices: list[tuple[float, float, float]]
    faces: list[BakedFace]
    edges: list[BakedEdge]
    bounding_radius: float
    inertia_half_extents: tuple[float, float, float]
    unit_inertia: tuple[float, float, float]
    projected_surface_area: float


class HullError(RuntimeError):
    pass


def vec_add(a: tuple[float, float, float], b: tuple[float, float, float]) -> tuple[float, float, float]:
    return (a[0] + b[0], a[1] + b[1], a[2] + b[2])


def vec_sub(a: tuple[float, float, float], b: tuple[float, float, float]) -> tuple[float, float, float]:
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def vec_mul(a: tuple[float, float, float], s: float) -> tuple[float, float, float]:
    return (a[0] * s, a[1] * s, a[2] * s)


def dot(a: tuple[float, float, float], b: tuple[float, float, float]) -> float:
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def cross(a: tuple[float, float, float], b: tuple[float, float, float]) -> tuple[float, float, float]:
    return (
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    )


def mag_sq(a: tuple[float, float, float]) -> float:
    return dot(a, a)


def normalize(v: tuple[float, float, float], context: str) -> tuple[float, float, float]:
    length_sq = mag_sq(v)
    if length_sq <= 1.0e-10:
        raise HullError(f"{context}: degenerate normal")
    inv = 1.0 / math.sqrt(length_sq)
    return vec_mul(v, inv)


def parse_float(value: str, path: Path, line_number: int, field: str) -> float:
    try:
        parsed = float(value)
    except ValueError as exc:
        raise HullError(f"{path}:{line_number}: invalid {field}") from exc
    if not math.isfinite(parsed):
        raise HullError(f"{path}:{line_number}: invalid {field}")
    return parsed


def parse_index(value: str, path: Path, line_number: int, field: str) -> int:
    try:
        parsed = int(value, 10)
    except ValueError as exc:
        raise HullError(f"{path}:{line_number}: invalid {field}") from exc
    if parsed < 0 or parsed > 65535:
        raise HullError(f"{path}:{line_number}: invalid {field}")
    return parsed


def read_source_hull(path: Path) -> SourceHull:
    version = None
    name = path.stem
    vertices: list[tuple[float, float, float]] = []
    faces: list[list[int]] = []

    for line_number, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        parts = line.split()
        token = parts[0]

        if token == "hull_version":
            if len(parts) != 2:
                raise HullError(f"{path}:{line_number}: invalid hull_version")
            version = parts[1]
            if version != "2":
                raise HullError(f"{path}:{line_number}: convex hull assets must use hull_version 2")
            continue

        if token == "name":
            if len(parts) != 2:
                raise HullError(f"{path}:{line_number}: invalid name")
            name = parts[1]
            continue

        if version is None:
            raise HullError(f"{path}:{line_number}: hull_version 2 must appear before hull data")

        if token == "source_vertex":
            if len(parts) != 4:
                raise HullError(f"{path}:{line_number}: invalid source_vertex")
            vertices.append(
                (
                    parse_float(parts[1], path, line_number, "source_vertex.x"),
                    parse_float(parts[2], path, line_number, "source_vertex.y"),
                    parse_float(parts[3], path, line_number, "source_vertex.z"),
                )
            )
            continue
        if token == "source_face":
            if len(parts) < 4:
                raise HullError(f"{path}:{line_number}: source_face needs at least three vertices")
            faces.append([parse_index(value, path, line_number, "source_face.index") for value in parts[1:]])
            continue

    if version is None:
        raise HullError(f"{path}: missing hull_version")
    if len(vertices) < 4 or len(vertices) > MAX_VERTICES:
        raise HullError(f"{path}: hull must have between 4 and {MAX_VERTICES} source vertices")
    if len(faces) < 4 or len(faces) > MAX_FACES:
        raise HullError(f"{path}: hull must have between 4 and {MAX_FACES} source faces")
    total_indices = sum(len(face) for face in faces)
    if total_indices > MAX_FACE_INDICES:
        raise HullError(f"{path}: hull exceeds {MAX_FACE_INDICES} source face indices")
    for face in faces:
        if len(face) > MAX_FACE_VERTICES:
            raise HullError(f"{path}: hull face exceeds {MAX_FACE_VERTICES} vertices")
        for index in face:
            if index >= len(vertices):
                raise HullError(f"{path}: face references vertex {index} but only {len(vertices)} exist")

    return SourceHull(path=path, name=name, vertices=vertices, faces=faces)


def fnv1a64_update(data: bytes, value: int) -> int:
    for byte in data:
        value ^= byte
        value = (value * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return value


def source_hash(source: SourceHull) -> int:
    value = 1469598103934665603
    value = fnv1a64_update(struct.pack("<I", len(source.vertices)), value)
    for vertex in source.vertices:
        value = fnv1a64_update(struct.pack("<ddd", *vertex), value)
    value = fnv1a64_update(struct.pack("<I", len(source.faces)), value)
    for face in source.faces:
        value = fnv1a64_update(struct.pack("<I", len(face)), value)
        for index in face:
            value = fnv1a64_update(struct.pack("<H", index), value)
    return value


def compute_mass_properties(source: SourceHull, faces: list[BakedFace]) -> tuple[tuple[float, float, float], float]:
    signed_volume = 0.0
    cx = cy = cz = 0.0

    for face in faces:
        root = source.vertices[face.indices[0]]
        for i in range(1, len(face.indices) - 1):
            b = source.vertices[face.indices[i]]
            c = source.vertices[face.indices[i + 1]]
            tetra_volume = dot(root, cross(b, c)) / 6.0
            signed_volume += tetra_volume
            cx += tetra_volume * (root[0] + b[0] + c[0]) * 0.25
            cy += tetra_volume * (root[1] + b[1] + c[1]) * 0.25
            cz += tetra_volume * (root[2] + b[2] + c[2]) * 0.25

    if abs(signed_volume) <= 1.0e-8:
        raise HullError(f"{source.path}: hull has near-zero signed volume")
    volume = abs(signed_volume)
    if volume <= 1.0e-5:
        raise HullError(f"{source.path}: hull has invalid volume")
    return (cx / signed_volume, cy / signed_volume, cz / signed_volume), volume


def bake_source_hull(source: SourceHull) -> BakedHull:
    centroid = (0.0, 0.0, 0.0)
    for vertex in source.vertices:
        centroid = vec_add(centroid, vertex)
    centroid = vec_mul(centroid, 1.0 / len(source.vertices))

    canonical_faces: list[BakedFace] = []
    for face_index, face in enumerate(source.faces):
        a = source.vertices[face[0]]
        b = source.vertices[face[1]]
        c = source.vertices[face[2]]
        normal = normalize(cross(vec_sub(b, a), vec_sub(c, a)), f"{source.path}: face {face_index}")
        flip = dot(normal, vec_sub(centroid, a)) > 0.0
        if flip:
            normal = vec_mul(normal, -1.0)
        plane_offset = dot(normal, a)
        for vertex_index in face:
            face_vertex = source.vertices[vertex_index]
            if abs(dot(normal, face_vertex) - plane_offset) > 1.0e-3:
                raise HullError(f"{source.path}: face {face_index} is not planar")
        indices = list(reversed(face)) if flip else list(face)
        canonical_faces.append(BakedFace(normal=normal, plane_offset=plane_offset, indices=indices))

    for face_index, face in enumerate(canonical_faces):
        for vertex in source.vertices:
            signed_distance = dot(face.normal, vertex) - face.plane_offset
            if signed_distance > 1.0e-3:
                raise HullError(f"{source.path}: hull is not convex near face {face_index}")

    edges_by_vertices: dict[tuple[int, int], BakedEdge] = {}
    for face_index, face in enumerate(canonical_faces):
        for i, a in enumerate(face.indices):
            b = face.indices[(i + 1) % len(face.indices)]
            key = (min(a, b), max(a, b))
            edge = edges_by_vertices.get(key)
            if edge is None:
                if len(edges_by_vertices) >= MAX_EDGES:
                    raise HullError(f"{source.path}: hull exceeds {MAX_EDGES} edges")
                edges_by_vertices[key] = BakedEdge(key[0], key[1], face_index, 0xFFFF)
            elif edge.face_b != 0xFFFF:
                raise HullError(f"{source.path}: edge {key[0]}-{key[1]} has more than two faces")
            else:
                edge.face_b = face_index

    edges = list(edges_by_vertices.values())
    for edge in edges:
        if edge.face_b == 0xFFFF:
            raise HullError(f"{source.path}: edge {edge.vertex_a}-{edge.vertex_b} has only one adjacent face")

    center_of_mass, volume = compute_mass_properties(source, canonical_faces)
    centered_vertices = [vec_sub(vertex, center_of_mass) for vertex in source.vertices]

    baked_faces: list[BakedFace] = []
    for face in canonical_faces:
        first_vertex = centered_vertices[face.indices[0]]
        baked_faces.append(BakedFace(face.normal, dot(face.normal, first_vertex), face.indices))

    min_v = [float("inf"), float("inf"), float("inf")]
    max_v = [float("-inf"), float("-inf"), float("-inf")]
    bounding_radius = 0.0
    for vertex in centered_vertices:
        for axis in range(3):
            min_v[axis] = min(min_v[axis], vertex[axis])
            max_v[axis] = max(max_v[axis], vertex[axis])
        bounding_radius = max(bounding_radius, math.sqrt(mag_sq(vertex)))

    inertia_half_extents = tuple(
        value if value > TOLERANCE else bounding_radius
        for value in (
            (max_v[0] - min_v[0]) * 0.5,
            (max_v[1] - min_v[1]) * 0.5,
            (max_v[2] - min_v[2]) * 0.5,
        )
    )
    projected_surface_area = (
        4.0 * inertia_half_extents[0] * inertia_half_extents[1]
        + 4.0 * inertia_half_extents[0] * inertia_half_extents[2]
        + 4.0 * inertia_half_extents[1] * inertia_half_extents[2]
    ) / 3.0
    unit_inertia = (
        (inertia_half_extents[1] * inertia_half_extents[1] + inertia_half_extents[2] * inertia_half_extents[2]) / 3.0,
        (inertia_half_extents[0] * inertia_half_extents[0] + inertia_half_extents[2] * inertia_half_extents[2]) / 3.0,
        (inertia_half_extents[0] * inertia_half_extents[0] + inertia_half_extents[1] * inertia_half_extents[1]) / 3.0,
    )

    return BakedHull(
        source=source,
        source_hash=source_hash(source),
        center_of_mass=center_of_mass,
        volume=volume,
        centered_vertices=centered_vertices,
        faces=baked_faces,
        edges=edges,
        bounding_radius=bounding_radius,
        inertia_half_extents=inertia_half_extents,
        unit_inertia=unit_inertia,
        projected_surface_area=projected_surface_area,
    )


def fmt(value: float) -> str:
    if value == 0.0:
        value = 0.0
    return f"{value:.9g}"


def fmt_vec(v: tuple[float, float, float]) -> str:
    return f"{fmt(v[0])} {fmt(v[1])} {fmt(v[2])}"


def serialize_hull(baked: BakedHull) -> str:
    lines: list[str] = [
        "# Convex hull asset.",
        "# source_* directives preserve editable geometry; runtime reads baked directives.",
        "hull_version 2",
        f"name {baked.source.name}",
        f"source_hash 0x{baked.source_hash:016x}",
        "",
        "# Editable source geometry.",
    ]
    for vertex in baked.source.vertices:
        lines.append(f"source_vertex {fmt_vec(vertex)}")
    for face in baked.source.faces:
        lines.append("source_face " + " ".join(str(index) for index in face))

    lines.extend(
        [
            "",
            "# Baked runtime metadata.",
            f"center_of_mass {fmt_vec(baked.center_of_mass)}",
            f"volume {fmt(baked.volume)}",
            f"bounding_radius {fmt(baked.bounding_radius)}",
            f"inertia_half_extents {fmt_vec(baked.inertia_half_extents)}",
            f"unit_inertia {fmt_vec(baked.unit_inertia)}",
            f"projected_surface_area {fmt(baked.projected_surface_area)}",
            "",
            "# Baked centered vertices.",
        ]
    )
    for vertex in baked.centered_vertices:
        lines.append(f"vertex {fmt_vec(vertex)}")

    lines.append("")
    lines.append("# Baked faces: normal.xyz planeOffset vertexIndices...")
    for face in baked.faces:
        lines.append(
            f"face {fmt_vec(face.normal)} {fmt(face.plane_offset)} "
            + " ".join(str(index) for index in face.indices)
        )

    lines.append("")
    lines.append("# Baked edges: vertexA vertexB faceA faceB")
    for edge in baked.edges:
        lines.append(f"edge {edge.vertex_a} {edge.vertex_b} {edge.face_a} {edge.face_b}")

    lines.append("")
    return "\n".join(lines)


def discover_hulls(repo: Path, explicit_paths: list[Path]) -> list[Path]:
    if explicit_paths:
        return [path if path.is_absolute() else repo / path for path in explicit_paths]
    return sorted((repo / "SkullbonezData" / "hulls").glob("*.hull"))


def main() -> int:
    parser = argparse.ArgumentParser(description="Bake or check serialized convex hull runtime data.")
    parser.add_argument("--repo", type=Path, default=Path.cwd())
    parser.add_argument("--write", action="store_true", help="Rewrite hull files with baked hull_version 2 data.")
    parser.add_argument("--check", action="store_true", help="Fail if any hull file is not current serialized output.")
    parser.add_argument("paths", nargs="*", type=Path)
    args = parser.parse_args()

    if args.write == args.check:
        parser.error("pass exactly one of --write or --check")

    repo = args.repo.resolve()
    paths = discover_hulls(repo, args.paths)
    stale: list[Path] = []

    for path in paths:
        source = read_source_hull(path)
        expected = serialize_hull(bake_source_hull(source))
        current = path.read_text(encoding="utf-8")
        if args.write:
            if current != expected:
                path.write_text(expected, encoding="utf-8", newline="\n")
                print(f"baked {path.relative_to(repo)}")
            else:
                print(f"current {path.relative_to(repo)}")
        elif current != expected:
            stale.append(path)

    if stale:
        print("ERROR: stale hull serialization:")
        for path in stale:
            print(f"  {path.relative_to(repo)}")
        return 1

    if args.check:
        print(f"BAKE_HULLS: OK ({len(paths)} hulls checked)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
