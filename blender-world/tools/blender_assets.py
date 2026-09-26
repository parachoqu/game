"""Medium-detail reusable environment kit for the Blender world."""

from __future__ import annotations

import math
from typing import Callable, Sequence

import bpy
import bmesh
from mathutils import Euler, Matrix, Vector


def _transform(bm, verts, location, scale, rotation=(0.0, 0.0, 0.0)) -> None:
    matrix = (
        Matrix.Translation(Vector(location))
        @ Euler(rotation, "XYZ").to_matrix().to_4x4()
        @ Matrix.Diagonal(Vector((scale[0], scale[1], scale[2], 1.0)))
    )
    bmesh.ops.transform(bm, verts=verts, matrix=matrix, space=Matrix.Identity(4))


def _mark_new_faces(bm: bmesh.types.BMesh, before: set, material_index: int) -> None:
    for face in set(bm.faces) - before:
        face.material_index = material_index


def _box(bm, size, location, material_index=0, rotation=(0.0, 0.0, 0.0)) -> None:
    before = set(bm.faces)
    result = bmesh.ops.create_cube(bm, size=1.0)
    _transform(bm, result["verts"], location, size, rotation)
    _mark_new_faces(bm, before, material_index)


def _cone(
    bm,
    radius1,
    radius2,
    depth,
    location,
    material_index=0,
    segments=10,
    rotation=(0.0, 0.0, 0.0),
) -> None:
    before = set(bm.faces)
    result = bmesh.ops.create_cone(
        bm,
        cap_ends=True,
        cap_tris=False,
        segments=segments,
        radius1=radius1,
        radius2=radius2,
        depth=depth,
    )
    _transform(bm, result["verts"], location, (1.0, 1.0, 1.0), rotation)
    _mark_new_faces(bm, before, material_index)


def _sphere(bm, radius, location, scale=(1.0, 1.0, 1.0), material_index=0, subdivisions=2) -> None:
    before = set(bm.faces)
    result = bmesh.ops.create_icosphere(bm, subdivisions=subdivisions, radius=radius)
    _transform(bm, result["verts"], location, scale)
    _mark_new_faces(bm, before, material_index)


def _branch(bm, start, end, radius, material_index=0, segments=8) -> None:
    start_vector = Vector(start)
    end_vector = Vector(end)
    direction = end_vector - start_vector
    midpoint = (start_vector + end_vector) * 0.5
    before = set(bm.faces)
    result = bmesh.ops.create_cone(
        bm,
        cap_ends=True,
        cap_tris=False,
        segments=segments,
        radius1=radius,
        radius2=radius * 0.68,
        depth=direction.length,
    )
    rotation = direction.to_track_quat("Z", "Y").to_matrix().to_4x4()
    matrix = Matrix.Translation(midpoint) @ rotation
    bmesh.ops.transform(bm, verts=result["verts"], matrix=matrix, space=Matrix.Identity(4))
    _mark_new_faces(bm, before, material_index)


def _asset(
    name: str,
    asset_type: str,
    collection: bpy.types.Collection,
    materials: Sequence[bpy.types.Material],
    builder: Callable[[bmesh.types.BMesh], None],
) -> bpy.types.Object:
    mesh = bpy.data.meshes.new(f"{name}_Mesh")
    bm = bmesh.new()
    builder(bm)
    bm.normal_update()
    bm.to_mesh(mesh)
    bm.free()
    mesh.update()
    obj = bpy.data.objects.new(name, mesh)
    collection.objects.link(obj)
    for material in materials:
        mesh.materials.append(material)
    obj["asset_type"] = asset_type
    obj["production_level"] = "medium_detail_base"
    return obj


def build_asset_library(collection: bpy.types.Collection, materials: dict[str, bpy.types.Material]) -> dict[str, bpy.types.Object]:
    assets: dict[str, bpy.types.Object] = {}

    def broadleaf(bm):
        _cone(bm, 0.72, 0.48, 6.8, (0, 0, 3.4), 0, segments=10)
        _branch(bm, (0, 0, 5.4), (2.2, 0.4, 7.4), 0.28, 0)
        _branch(bm, (0, 0, 5.1), (-1.7, -0.8, 7.0), 0.24, 0)
        _sphere(bm, 2.65, (0.0, 0.0, 8.0), (1.18, 0.92, 0.86), 1)
        _sphere(bm, 2.0, (2.0, 0.35, 7.7), (1.05, 0.9, 0.8), 1)
        _sphere(bm, 1.8, (-1.8, -0.7, 7.5), (0.95, 1.0, 0.82), 1)

    assets["broadleaf_tree"] = _asset(
        "ASSET_BroadleafTree", "broadleaf_tree", collection,
        [materials["bark"], materials["foliage"]], broadleaf,
    )

    def pine(bm):
        _cone(bm, 0.58, 0.34, 8.0, (0, 0, 4.0), 0, segments=9)
        for index, (radius, z) in enumerate(((3.4, 4.8), (2.9, 6.4), (2.2, 7.9), (1.4, 9.25))):
            _cone(bm, radius, 0.12, 3.8, (0, 0, z), 1, segments=12)

    assets["pine_tree"] = _asset(
        "ASSET_PineTree", "pine_tree", collection,
        [materials["pine_bark"], materials["pine_foliage"]], pine,
    )

    def dead_tree(bm):
        _cone(bm, 0.58, 0.3, 7.2, (0, 0, 3.6), 0, segments=9)
        _branch(bm, (0, 0, 4.5), (2.8, 0.4, 7.0), 0.26, 0)
        _branch(bm, (0, 0, 5.7), (-2.2, -0.7, 8.2), 0.22, 0)
        _branch(bm, (0, 0, 6.4), (1.0, 0.3, 9.2), 0.18, 0)

    assets["dead_tree"] = _asset("ASSET_DeadTree", "dead_tree", collection, [materials["bark"]], dead_tree)

    def shrub(bm):
        for location, radius in (((0, 0, 0.8), 1.2), ((0.9, 0.1, 0.7), 0.9), ((-0.8, -0.2, 0.65), 0.85)):
            _sphere(bm, radius, location, (1.0, 0.82, 0.7), 0, subdivisions=1)

    assets["shrub"] = _asset("ASSET_Shrub", "shrub", collection, [materials["foliage"]], shrub)

    def grass(bm):
        for index in range(11):
            angle = index * 2.399
            radius = 0.15 + 0.04 * (index % 3)
            _cone(
                bm, 0.055, 0.0, 0.8 + 0.08 * (index % 4),
                (math.cos(angle) * radius, math.sin(angle) * radius, 0.42),
                0, segments=4, rotation=(0.12 * math.sin(angle), 0.12 * math.cos(angle), angle),
            )

    assets["grass"] = _asset("ASSET_GrassClump", "grass", collection, [materials["grass"]], grass)

    def rock(bm):
        _sphere(bm, 1.5, (0, 0, 1.0), (1.35, 0.9, 0.72), 0, subdivisions=2)
        _sphere(bm, 0.75, (1.1, -0.2, 0.55), (0.8, 0.65, 0.6), 0, subdivisions=1)

    assets["rock"] = _asset("ASSET_Rock", "rock", collection, [materials["rock"]], rock)

    def bridge(bm):
        _box(bm, (10.0, 4.5, 0.65), (0, 0, 2.8), 1)
        for x in (-4.2, 0.0, 4.2):
            _box(bm, (1.1, 4.8, 5.0), (x, 0, 0.0), 0)
        for y in (-2.05, 2.05):
            _box(bm, (10.0, 0.3, 1.1), (0, y, 3.7), 0)

    assets["bridge"] = _asset("ASSET_Bridge", "bridge", collection, [materials["stone"], materials["wood"]], bridge)

    def dock(bm):
        _box(bm, (8.0, 4.0, 0.35), (0, 0, 1.8), 0)
        for x in (-3.5, 0.0, 3.5):
            for y in (-1.6, 1.6):
                _box(bm, (0.35, 0.35, 4.0), (x, y, 0.0), 0)
        _box(bm, (0.12, 4.5, 0.12), (0, 0, 2.1), 1)

    assets["dock"] = _asset("ASSET_Dock", "dock", collection, [materials["wood"], materials["metal"]], dock)

    def wall(bm):
        _box(bm, (6.0, 0.8, 3.8), (0, 0, 1.9), 1)
        _box(bm, (6.4, 1.2, 0.9), (0, 0, 0.45), 0)
        _box(bm, (0.8, 1.1, 4.2), (-2.8, 0, 2.1), 0)
        _box(bm, (0.8, 1.1, 4.2), (2.8, 0, 2.1), 0)

    assets["wall"] = _asset("ASSET_Wall", "wall", collection, [materials["stone"], materials["plaster"]], wall)

    def arch(bm):
        _box(bm, (1.25, 1.7, 5.2), (-2.2, 0, 2.6), 0)
        _box(bm, (1.25, 1.7, 5.2), (2.2, 0, 2.6), 0)
        _box(bm, (5.7, 1.7, 1.3), (0, 0, 5.0), 0)

    assets["arch"] = _asset("ASSET_Arch", "arch", collection, [materials["stone"]], arch)

    def roof(bm):
        _box(bm, (6.0, 3.7, 0.42), (0, -1.55, 0), 0, rotation=(math.radians(31), 0, 0))
        _box(bm, (6.0, 3.7, 0.42), (0, 1.55, 0), 0, rotation=(-math.radians(31), 0, 0))

    assets["roof"] = _asset("ASSET_Roof", "roof", collection, [materials["terracotta"]], roof)

    def tower(bm):
        _cone(bm, 3.4, 3.15, 9.5, (0, 0, 4.75), 0, segments=12)
        _cone(bm, 4.0, 0.25, 4.0, (0, 0, 11.0), 1, segments=12)
        for angle in (0, math.pi / 2, math.pi, math.pi * 1.5):
            _box(bm, (0.8, 0.35, 1.6), (math.cos(angle) * 3.25, math.sin(angle) * 3.25, 7.0), 2, rotation=(0, 0, angle))

    assets["tower"] = _asset(
        "ASSET_Tower", "tower", collection,
        [materials["stone"], materials["terracotta"], materials["cobalt"]], tower,
    )

    def tent(bm):
        _cone(bm, 3.5, 0.3, 4.8, (0, 0, 2.4), 0, segments=4, rotation=(0, 0, math.radians(45)))
        _box(bm, (0.18, 0.18, 5.2), (0, 0, 2.6), 1)

    assets["tent"] = _asset("ASSET_Tent", "tent", collection, [materials["cloth"], materials["wood"]], tent)

    def market_stall(bm):
        _box(bm, (5.0, 3.4, 0.35), (0, 0, 1.0), 0)
        for x in (-2.2, 2.2):
            for y in (-1.45, 1.45):
                _box(bm, (0.22, 0.22, 4.5), (x, y, 2.25), 0)
        _box(bm, (5.5, 3.9, 0.28), (0, 0, 4.4), 1, rotation=(0.06, 0, 0))

    assets["market_stall"] = _asset(
        "ASSET_MarketStall", "market_stall", collection,
        [materials["wood"], materials["cobalt"]], market_stall,
    )

    def mine(bm):
        _box(bm, (1.1, 1.8, 5.0), (-2.35, 0, 2.5), 0)
        _box(bm, (1.1, 1.8, 5.0), (2.35, 0, 2.5), 0)
        _box(bm, (5.8, 1.8, 1.2), (0, 0, 5.0), 0)
        _box(bm, (3.4, 0.55, 3.8), (0, 0.68, 1.9), 2)
        for x in (-1.7, 1.7):
            _box(bm, (0.35, 2.1, 5.4), (x, 0, 2.7), 1)

    assets["mine"] = _asset(
        "ASSET_MineEntrance", "mine", collection,
        [materials["stone"], materials["wood"], materials["dark"]], mine,
    )

    def observatory(bm):
        _cone(bm, 6.5, 6.0, 1.3, (0, 0, 0.65), 0, segments=16)
        _cone(bm, 4.0, 3.8, 1.0, (0, 0, 1.8), 0, segments=16)
        _box(bm, (0.35, 10.0, 0.35), (0, 0, 6.0), 1, rotation=(0, math.radians(42), 0))
        _box(bm, (0.35, 8.0, 0.35), (0, 0, 6.0), 1, rotation=(math.radians(90), 0, 0))
        for angle in (0, math.pi / 2, math.pi, math.pi * 1.5):
            _box(bm, (0.55, 0.55, 5.0), (math.cos(angle) * 4.9, math.sin(angle) * 4.9, 3.2), 0)

    assets["observatory"] = _asset(
        "ASSET_Observatory", "observatory", collection,
        [materials["stone"], materials["metal"]], observatory,
    )

    def camp(bm):
        _cone(bm, 2.8, 0.25, 4.0, (-2.5, 0, 2.0), 0, segments=4, rotation=(0, 0, math.radians(45)))
        _cone(bm, 2.4, 0.25, 3.5, (2.3, 1.2, 1.75), 0, segments=4, rotation=(0, 0, math.radians(45)))
        for index in range(9):
            angle = index / 9.0 * math.tau
            _box(bm, (0.35, 0.35, 3.0), (math.cos(angle) * 5.3, math.sin(angle) * 5.3, 1.5), 1, rotation=(0, 0, angle))
        _sphere(bm, 0.7, (0, -0.8, 0.45), (1.0, 1.0, 0.5), 2, subdivisions=1)

    assets["camp"] = _asset(
        "ASSET_Camp", "camp", collection,
        [materials["cloth"], materials["wood"], materials["ember"]], camp,
    )

    def ruin(bm):
        _box(bm, (1.0, 1.0, 6.0), (-3.0, 0, 3.0), 0, rotation=(0.05, 0.08, -0.08))
        _box(bm, (1.0, 1.0, 4.2), (0, 0, 2.1), 0, rotation=(-0.12, 0.05, 0.18))
        _box(bm, (1.0, 1.0, 7.0), (3.2, 0, 3.5), 0, rotation=(0.08, -0.06, 0.04))
        _box(bm, (7.2, 1.0, 1.0), (0, 0, 6.0), 0, rotation=(0.0, 0.1, 0.0))
        _sphere(bm, 1.1, (-1.1, 0.4, 0.5), (1.3, 0.8, 0.5), 0, subdivisions=1)

    assets["ruin"] = _asset("ASSET_Ruin", "ruin", collection, [materials["stone"]], ruin)

    positions = [
        (-22, 15), (-14, 15), (-6, 15), (2, 15), (10, 15), (18, 15),
        (-22, 3), (-14, 3), (-6, 3), (2, 3), (10, 3), (18, 3),
        (-22, -10), (-12, -10), (-2, -10), (8, -10), (18, -10), (-22, -22),
    ]
    for obj, (x, y) in zip(assets.values(), positions):
        obj.location = (x, y, 0.0)
    return assets


def make_scatter_sources(assets: dict[str, bpy.types.Object]) -> tuple[dict[str, bpy.types.Object], bpy.types.Collection]:
    collection = bpy.data.collections.new("SCATTER_SOURCES_INTERNAL")
    collection.use_fake_user = True
    sources = {}
    for asset_type in ("broadleaf_tree", "pine_tree", "dead_tree", "rock", "shrub", "grass"):
        source = bpy.data.objects.new(f"SOURCE_{asset_type}", assets[asset_type].data)
        source.location = (0.0, 0.0, 0.0)
        source["asset_type"] = asset_type
        source["internal_scatter_source"] = True
        collection.objects.link(source)
        sources[asset_type] = source
    return sources, collection


def place_asset(
    source: bpy.types.Object,
    name: str,
    collection: bpy.types.Collection,
    location,
    rotation=(0.0, 0.0, 0.0),
    scale=(1.0, 1.0, 1.0),
) -> bpy.types.Object:
    obj = bpy.data.objects.new(name, source.data)
    obj.location = location
    obj.rotation_euler = rotation
    obj.scale = scale
    obj["asset_type"] = source.get("asset_type", "unknown")
    obj["linked_asset_source"] = source.name
    collection.objects.link(obj)
    return obj
