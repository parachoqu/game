"""Build the complete editable Blender world master from deterministic data."""

from __future__ import annotations

import json
import math
from pathlib import Path
import random
import sys

import bpy
import numpy as np
from mathutils import Vector


TOOLS = Path(__file__).resolve().parent
ROOT = TOOLS.parent
PROJECT = ROOT.parent
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

from blender_assets import build_asset_library, make_scatter_sources, place_asset
from blender_common import (
    configure_scene,
    create_camera,
    create_curve_tube,
    create_grid_mesh,
    create_marker,
    create_ribbon,
    create_scatter_points,
    create_sun,
    create_text,
    hex_rgba,
    make_material,
    make_mesh_object,
    make_pbr_image_material,
    make_vertex_color_material,
    new_collection,
    sample_height,
    save_float_exr,
)
from worldgen_core import (
    build_route_polyline,
    chunk_heightfield,
    generate_world_data,
    load_config,
    region_river_center,
)


PALETTE = {
    "emerald": "#245F50",
    "teal": "#153F46",
    "ochre": "#B77A35",
    "terracotta": "#A24E35",
    "cobalt": "#2E4F91",
    "stone": "#76736D",
    "amber": "#E3A64C",
    "cyan": "#42E0DE",
    "violet": "#7455A5",
}


def build_materials() -> dict[str, bpy.types.Material]:
    texture_root = PROJECT / "modelos 3d animados" / "texturas"

    def pbr(name: str, folder: str, prefix: str, tint, scale: float):
        # Each Poly Haven texture set is stored directly in its named folder.
        # Keeping this as a single folder component also makes Blender's packed
        # image paths reproducible across workstations.
        base = texture_root / folder
        return make_pbr_image_material(
            name,
            base / f"{prefix}_diff_2k.png",
            base / f"{prefix}_rough_2k.png",
            base / f"{prefix}_nor_gl_2k.png",
            tint=tint,
            scale=scale,
        )

    materials = {
        "atlas": make_vertex_color_material("MAT_AtlasBiome", 0.9),
        "terrain": make_vertex_color_material("MAT_RegionBiome", 0.84),
        "turbulent_ground": make_vertex_color_material("MAT_TurbulentGround", 0.9),
        "grass": pbr("MAT_Grass_PBR_CC0", "terreno/sparse_grass", "sparse_grass", (0.72, 0.94, 0.72, 1), 2.2),
        "road": pbr("MAT_Road_PBR_CC0", "terreno/rocky_trail_02", "rocky_trail_02", (0.95, 0.78, 0.62, 1), 1.8),
        "forest": pbr("MAT_ForestGround_PBR_CC0", "terreno/forrest_ground_01", "forrest_ground_01", (0.62, 0.76, 0.58, 1), 2.0),
        "riverbed": pbr("MAT_Riverbed_PBR_CC0", "terreno/river_small_rocks", "river_small_rocks", (0.72, 0.78, 0.76, 1), 2.1),
        "rock": pbr("MAT_Rock_PBR_CC0", "rochas/dark_rock", "dark_rock", (0.72, 0.78, 0.82, 1), 1.5),
        "stone": pbr("MAT_Stone_PBR_CC0", "rochas/rock_3", "rock_3", (0.92, 0.88, 0.76, 1), 1.3),
        "bark": pbr("MAT_Bark_PBR_CC0", "cascas/bark_brown_02", "bark_brown_02", (0.82, 0.7, 0.58, 1), 1.4),
        "pine_bark": pbr("MAT_PineBark_PBR_CC0", "cascas/pine_bark", "pine_bark", (0.72, 0.68, 0.62, 1), 1.5),
        "foliage": make_material("MAT_FoliageEmerald", hex_rgba(PALETTE["emerald"]), 0.83),
        "pine_foliage": make_material("MAT_PineFoliageTeal", hex_rgba(PALETTE["teal"]), 0.86),
        "plaster": make_material("MAT_PlasterOchre", hex_rgba(PALETTE["ochre"]), 0.78),
        "terracotta": make_material("MAT_Terracotta", hex_rgba(PALETTE["terracotta"]), 0.8),
        "field_green": make_material("MAT_ProductiveFieldGreen", (0.24, 0.38, 0.12, 1), 0.96),
        "field_ochre": make_material("MAT_ProductiveFieldOchre", (0.48, 0.30, 0.09, 1), 0.94),
        "wood": make_material("MAT_DarkWood", (0.12, 0.075, 0.045, 1), 0.72),
        "metal": make_material("MAT_AgedMetal", (0.18, 0.2, 0.19, 1), 0.38, metallic=0.68),
        "cobalt": make_material("MAT_CobaltCloth", hex_rgba(PALETTE["cobalt"]), 0.88),
        "cloth": make_material("MAT_CampCloth", (0.38, 0.17, 0.11, 1), 0.92),
        "ember": make_material("MAT_Ember", (0.28, 0.06, 0.01, 1), 0.5, emission=(1.0, 0.16, 0.01, 1), emission_strength=4.5),
        "dark": make_material("MAT_CaveDark", (0.008, 0.012, 0.016, 1), 1.0),
        "water": make_material("MAT_WaterTeal", (0.035, 0.2, 0.25, 1), 0.16, metallic=0.08, alpha=0.78),
        "ocean": make_material("MAT_OceanDeep", (0.015, 0.08, 0.14, 1), 0.22, metallic=0.12, alpha=0.94),
        "cyan": make_material("MAT_AnomalyCyan", hex_rgba(PALETTE["cyan"]), 0.2, emission=hex_rgba(PALETTE["cyan"]), emission_strength=3.0),
        "cyan_surface": make_material("MAT_AnomalyCyanSurface", (0.015, 0.18, 0.22, 0.58), 0.24, emission=hex_rgba(PALETTE["cyan"]), emission_strength=1.35, alpha=0.58),
        "violet": make_material("MAT_AnomalyViolet", hex_rgba(PALETTE["violet"]), 0.34, emission=hex_rgba(PALETTE["violet"]), emission_strength=2.2),
        "snow": make_material("MAT_Snow", (0.72, 0.82, 0.88, 1), 0.72),
        "label": make_material("MAT_Label", (0.95, 0.78, 0.34, 1), 0.5, emission=(0.5, 0.18, 0.02, 1), emission_strength=0.35),
    }
    return materials


def biome_colors(biome: np.ndarray, target: str) -> np.ndarray:
    if target == "atlas":
        colors = np.array([
            (0.19, 0.42, 0.24, 1.0),
            (0.25, 0.34, 0.19, 1.0),
            (0.35, 0.38, 0.36, 1.0),
            (0.12, 0.34, 0.34, 1.0),
            (0.52, 0.34, 0.18, 1.0),
            (0.08, 0.25, 0.33, 1.0),
            (0.35, 0.18, 0.46, 1.0),
        ], dtype=np.float32)
    elif target == "region":
        colors = np.array([
            (0.22, 0.44, 0.24, 1.0),
            (0.28, 0.37, 0.20, 1.0),
            (0.38, 0.39, 0.36, 1.0),
            (0.11, 0.36, 0.34, 1.0),
            (0.50, 0.32, 0.18, 1.0),
            (0.06, 0.26, 0.34, 1.0),
            (0.38, 0.16, 0.5, 1.0),
        ], dtype=np.float32)
    else:
        colors = np.array([(0.24, 0.14, 0.31, 1.0)] * 7, dtype=np.float32)
        colors[6] = (0.26, 0.17, 0.38, 1.0)
    return colors[np.asarray(biome, dtype=np.int32)]


def create_scene(name: str, first: bool = False) -> bpy.types.Scene:
    if first:
        scene = bpy.context.scene
        scene.name = name
    else:
        scene = bpy.data.scenes.new(name)
    return scene


def scene_collections(scene: bpy.types.Scene, prefix: str) -> dict[str, bpy.types.Collection]:
    return {
        "sources": new_collection(f"{prefix}__FONTES_PROCEDURAIS", scene=scene),
        "base": new_collection(f"{prefix}__BASE_EDITAVEL", scene=scene),
        "manual": new_collection(f"{prefix}__ACABAMENTO_MANUAL", scene=scene),
        "export": new_collection(f"{prefix}__PREPARACAO_EXPORTACAO", scene=scene),
        "cameras": new_collection(f"{prefix}__CAMERAS", scene=scene),
        "lights": new_collection(f"{prefix}__LUZ", scene=scene),
    }


def simple_plane(name, bounds, z, collection, material):
    min_x, min_y, max_x, max_y = bounds
    vertices = [(min_x, min_y, z), (max_x, min_y, z), (max_x, max_y, z), (min_x, max_y, z)]
    obj = make_mesh_object(name, collection, vertices, [(0, 1, 2, 3)])
    obj.data.materials.append(material)
    return obj


def terrain_z(data, x, y, size_x, size_y, lift=0.0):
    return sample_height(data.height, x, y, size_x, size_y) + lift


def add_house(assets, collection, name, location, scale=1.0, rotation=0.0):
    x, y, z = location
    wall = place_asset(assets["wall"], f"{name}_Facade", collection, (x, y, z), rotation=(0, 0, rotation), scale=(scale, scale, scale))
    rear = place_asset(assets["wall"], f"{name}_Rear", collection, (x, y + math.cos(rotation) * 6.0 * scale, z), rotation=(0, 0, rotation + math.pi), scale=(scale, scale, scale))
    roof = place_asset(assets["roof"], f"{name}_Roof", collection, (x, y + 3.0 * scale, z + 4.4 * scale), rotation=(0, 0, rotation), scale=(scale, scale, scale))
    for obj in (wall, rear, roof):
        obj["architecture_role"] = "inhabited_outpost"
    return wall


def build_atlas(scene, collections, data, config, materials, assets):
    configure_scene(scene, (0.038, 0.065, 0.09, 1), 0.42)
    scene["world_schema_version"] = config["schema_version"]
    scene["scale_contract"] = "1 BU = 1 km"
    scene["world_bounds_km"] = "220 x 140"
    terrain = create_grid_mesh(
        "ATLAS_Terrain",
        data.height,
        (-110.0, -70.0, 110.0, 70.0),
        collections["base"],
        materials["atlas"],
        biome_colors(data.biome, "atlas"),
        target_samples=(321, 205),
    )
    terrain["atlas_terrain"] = True
    terrain["status"] = "BASE_PROCEDURAL_EDITAVEL"
    ocean = simple_plane("ATLAS_Ocean", (-114, -74, 114, 74), -0.03, collections["base"], materials["ocean"])
    ocean["water_body"] = "global_ocean"

    river_specs = [
        [(-77, 20), (-60, 12), (-45, 5), (-30, -8), (-15, -28), (-2, -38)],
        [(-55, 33), (-40, 25), (-26, 12), (-14, -2), (3, -12)],
        [(57, 20), (67, 12), (76, 3), (82, -10)],
    ]
    for index, path in enumerate(river_specs, 1):
        points = [(x, y, max(0.04, terrain_z(data, x, y, 220, 140, 0.04))) for x, y in path]
        ribbon = create_ribbon(f"ATLAS_River_{index:02d}", points, 0.8, collections["base"], materials["water"])
        ribbon["hydrology_layer"] = True

    political = new_collection("ATLAS_POLITICAL", scene=scene)
    political["status"] = "PROPOSTA"
    realm_layout = {
        "valdora": (-34, -16, 24, 17),
        "serrabruma": (-15, 18, 28, 18),
        "verdeferro": (-67, 7, 25, 19),
        "marevia": (-42, -35, 29, 13),
        "pedrassol": (2, -5, 20, 17),
        "altavela": (-77, 31, 18, 14),
    }
    realm_colors = ["#D9A441", "#5B83B5", "#3E7A59", "#4F8D94", "#A95F3C", "#7662A6"]
    for realm, color in zip(config["realms"], realm_colors):
        cx, cy, rx, ry = realm_layout[realm["id"]]
        material = make_material(f"MAT_Realm_{realm['name']}", hex_rgba(color), 0.6, emission=hex_rgba(color), emission_strength=0.18)
        points = []
        for index in range(49):
            angle = index / 48.0 * math.tau
            x = cx + math.cos(angle) * rx
            y = cy + math.sin(angle) * ry
            points.append((x, y, terrain_z(data, x, y, 220, 140, 0.14)))
        boundary = create_curve_tube(f"REALM_{realm['name']}_Boundary", points, 0.13, political, material, cyclic=True)
        boundary["realm_id"] = realm["id"]
        boundary["status"] = "PROPOSTA"
        label_z = terrain_z(data, cx, cy, 220, 140, 0.5)
        label = create_text(f"REALM_{realm['name']}_Label", realm["name"], political, (cx, cy, label_z), 2.25, material)
        label.rotation_euler = (0.0, 0.0, 0.0)
        label["status"] = "PROPOSTA"

    risk = new_collection("ATLAS_RISK", scene=scene)
    risk["layer"] = "risk"
    risk["status"] = "PROPOSTA"
    risk.hide_render = True
    for risk_id, center_x, center_y, radius_x, radius_y in (
        ("serrabruma_passes", -13.0, 23.0, 22.0, 10.0),
        ("altavela_ruins", -72.0, 32.0, 15.0, 8.0),
        ("second_continent_interior", 73.0, 8.0, 24.0, 15.0),
    ):
        points = []
        for index in range(33):
            angle = index / 32.0 * math.tau
            x = center_x + math.cos(angle) * radius_x
            y = center_y + math.sin(angle) * radius_y
            points.append((x, y, terrain_z(data, x, y, 220, 140, 0.22)))
        zone = create_curve_tube(f"RISK_{risk_id}", points, 0.22, risk, materials["terracotta"], cyclic=True)
        zone["risk_id"] = risk_id
        zone["status"] = "PROPOSTA"

    anomalies = new_collection("ATLAS_ANOMALIES", scene=scene)
    anomalies["layer"] = "anomalous_phenomena"
    anomalies["status"] = "PROPOSTA"
    anomalies.hide_render = True
    for anomaly_id, x, y, radius in (
        ("regional_turbulent", -16.0, 18.0, 3.0),
        ("second_continent_signal", 72.0, 15.0, 4.0),
    ):
        points = []
        for index in range(33):
            angle = index / 32.0 * math.tau
            px = x + math.cos(angle) * radius
            py = y + math.sin(angle) * radius
            points.append((px, py, terrain_z(data, px, py, 220, 140, 0.35)))
        marker = create_curve_tube(f"ANOMALY_{anomaly_id}", points, 0.18, anomalies, materials["violet"], cyclic=True)
        marker["anomaly_id"] = anomaly_id
        marker["status"] = "PROPOSTA"

    landmarks = new_collection("ATLAS_LANDMARKS", scene=scene)
    landmark_specs = [
        ("Capital_Valdora", -33, -17, "tower"),
        ("Cidade_Verdeferro", -57, 10, "tower"),
        ("Passagem_Serrabruma", -12, 24, "arch"),
        ("Mina_Verdeferro", -61, 6, "mine"),
        ("Porto_Marevia", -39, -38, "dock"),
        ("Entreposto_Marevia", -15, -34, "market_stall"),
        ("Fortaleza_Pedrassol", 4, -2, "tower"),
        ("Observatorio_Altavela", -74, 30, "observatory"),
        ("Ruinas_Altavela", -66, 38, "ruin"),
        ("Observatorio_Segundo_Continente", 72, 15, "observatory"),
        ("Ruinas_Segundo_Continente", 87, 8, "ruin"),
        ("Expedicao_Segundo_Continente", 51, -4, "camp"),
    ]
    for name, x, y, asset_type in landmark_specs:
        z = max(0.1, terrain_z(data, x, y, 220, 140, 0.1))
        marker = place_asset(assets[asset_type], f"ATLAS_{name}", landmarks, (x, y, z), scale=(0.16, 0.16, 0.16))
        marker["atlas_landmark"] = name
        marker["symbolic_scale"] = True

    atlas_routes = [
        ("Valdora_Verdeferro", [(-33, -17), (-45, -6), (-57, 10)]),
        ("Valdora_Serrabruma", [(-33, -17), (-23, 2), (-12, 24)]),
        ("Valdora_Marevia", [(-33, -17), (-36, -27), (-39, -38)]),
        ("Valdora_Pedrassol", [(-33, -17), (-13, -11), (4, -2)]),
        ("Altavela_Passagem", [(-74, 30), (-43, 29), (-12, 24)]),
        ("Second_Expedition", [(51, -4), (61, 4), (72, 15)]),
    ]
    for route_id, coordinates in atlas_routes:
        points = [(x, y, terrain_z(data, x, y, 220, 140, 0.2)) for x, y in coordinates]
        route = create_curve_tube(f"ATLAS_ROUTE_{route_id}", points, 0.16, landmarks, materials["road"])
        route["atlas_route"] = route_id
        route["route_status"] = "PROPOSTA"

    create_sun(scene, collections["lights"], 3.2, (0.45, -0.3, -0.7))
    top_physical = create_camera("CAM_01_ATLAS_FISICO", scene, collections["cameras"], (0, 0, 185), (0, 0, 0), 50)
    top_physical.data.type = "ORTHO"
    top_physical.data.ortho_scale = 242
    top_political = create_camera("CAM_02_ATLAS_POLITICO", scene, collections["cameras"], (0, 0, 186), (0, 0, 0), 50)
    top_political.data.type = "ORTHO"
    top_political.data.ortho_scale = 242
    create_camera("CAM_03_CONTINENTE_PRINCIPAL", scene, collections["cameras"], (-115, -105, 82), (-34, 0, 1.5), 52)
    create_camera("CAM_04_SEGUNDO_CONTINENTE", scene, collections["cameras"], (122, -62, 56), (70, 7, 1.2), 58)
    scene.camera = top_physical


def route_points(data, config, route) -> list[tuple[float, float, float]]:
    return build_route_polyline(data, config, route)


def distance_to_segment(point, start, end):
    px, py = point
    sx, sy = start
    ex, ey = end
    dx, dy = ex - sx, ey - sy
    if dx == 0 and dy == 0:
        return math.hypot(px - sx, py - sy)
    factor = max(0.0, min(1.0, ((px - sx) * dx + (py - sy) * dy) / (dx * dx + dy * dy)))
    return math.hypot(px - (sx + factor * dx), py - (sy + factor * dy))


def build_region(scene, collections, data, config, materials, assets, scatter_sources):
    configure_scene(scene, (0.075, 0.105, 0.125, 1), 0.5)
    scene["scale_contract"] = "1 BU = 1 m"
    scene["region_bounds_m"] = "1024 x 1024"
    height_chunks = chunk_heightfield(data.height, 4, 4)
    biome_chunks = chunk_heightfield(data.biome, 4, 4)
    for row in range(4):
        for col in range(4):
            bounds = (-512 + col * 256, -512 + row * 256, -256 + col * 256, -256 + row * 256)
            chunk = create_grid_mesh(
                f"REGION_Terrain_{col}_{row}",
                height_chunks[(col, row)],
                bounds,
                collections["base"],
                materials["terrain"],
                biome_colors(biome_chunks[(col, row)], "region"),
                target_samples=(65, 65),
            )
            chunk["terrain_chunk"] = True
            chunk["chunk_id"] = f"region_{col}_{row}"
            chunk["full_height_samples"] = "257 x 257 shared border"

    river_points = []
    for x in np.linspace(-512.0, 512.0, 129):
        y = region_river_center(
            float(x),
            config["hydrology"]["region_river_crossings_m"],
            config["hydrology"]["region_meander_amplitudes_m"],
        )
        river_points.append((float(x), y, config["hydrology"]["river_surface_m"]))
    river = create_ribbon("REGION_MainRiver", river_points, 17.0, collections["base"], materials["water"])
    river["water_body"] = "main_river"

    roads = new_collection("REGION_ROUTES", scene=scene)
    route_polylines = {}
    for route_id, route in data.routes.items():
        points = route_points(data, config, route)
        route_polylines[route_id] = [(point[0], point[1]) for point in points]
        ribbon = create_ribbon(f"ROUTE_{route_id}", points, route["width_m"], roads, materials["road"])
        ribbon["route_id"] = route_id
        ribbon["risk"] = route["risk"]
        ribbon["max_sustained_slope_deg"] = route["max_sustained_slope_deg"]
        ribbon["max_peak_slope_deg"] = route["max_peak_slope_deg"]

    architecture = new_collection("REGION_ARCHITECTURE", scene=scene)
    waterworks = new_collection("REGION_WATERWORKS", scene=scene)
    markers = new_collection("REGION_POI_MARKERS", scene=scene)
    for poi_id, coordinate in data.anchors.items():
        x, y = coordinate
        z = terrain_z(data, x, y, 1024, 1024, 1.0)
        create_marker(f"POI_{poi_id}", markers, (x, y, z), "poi_id", poi_id)

    south = data.anchors["outpost_south"]
    south_z = terrain_z(data, *south, 1024, 1024)
    productive_field_bounds = [
        (-165.0, -470.0, -92.0, -430.0),
        (-165.0, -418.0, -92.0, -378.0),
        (95.0, -468.0, 172.0, -426.0),
        (98.0, -414.0, 175.0, -372.0),
    ]
    for index, bounds in enumerate(productive_field_bounds):
        center_x = (bounds[0] + bounds[2]) * 0.5
        center_y = (bounds[1] + bounds[3]) * 0.5
        field = simple_plane(
            f"South_ProductiveField_{index + 1:02d}",
            bounds,
            terrain_z(data, center_x, center_y, 1024, 1024, 0.3),
            architecture,
            materials["field_green" if index % 2 == 0 else "field_ochre"],
        )
        field["facility_type"] = "productive_field"
    for index, (dx, dy, rot, scale) in enumerate(((-45, -16, 0.15, 1.4), (38, -8, -0.2, 1.25), (-32, 38, 0.45, 1.1), (45, 45, -0.5, 1.0))):
        add_house(assets, architecture, f"SouthHouse_{index+1:02d}", (south[0] + dx, south[1] + dy, south_z), scale, rot)
    place_asset(assets["market_stall"], "SouthMarket_01", architecture, (south[0] + 10, south[1] + 24, south_z), scale=(1.25, 1.25, 1.25))
    place_asset(assets["market_stall"], "SouthMarket_02", architecture, (south[0] + 26, south[1] + 22, south_z), rotation=(0, 0, 0.3))
    workshop = add_house(assets, architecture, "SouthWorkshop", (-62.0, -372.0, terrain_z(data, -62.0, -372.0, 1024, 1024)), 1.18, 0.25)
    workshop["facility_type"] = "workshop_exterior"
    warehouse = add_house(assets, architecture, "SouthWarehouse", (72.0, -378.0, terrain_z(data, 72.0, -378.0, 1024, 1024)), 1.65, -0.12)
    warehouse["facility_type"] = "warehouse_exterior"
    stable = place_asset(assets["market_stall"], "SouthStable", architecture, (74.0, -430.0, terrain_z(data, 74.0, -430.0, 1024, 1024)), rotation=(0, 0, -0.12), scale=(1.8, 1.5, 1.35))
    stable["facility_type"] = "stable_exterior"
    place_asset(assets["tower"], "SouthOutpost_Tower", architecture, (south[0] - 63, south[1] + 26, south_z), scale=(0.85, 0.85, 0.85))
    place_asset(assets["dock"], "SouthOutpost_Dock", architecture, (105, -167, config["region"]["water_level_m"] - 1.5), rotation=(0, 0, 0.15), scale=(1.25, 1.25, 1.25))

    for index, coordinates in enumerate((
        [(-82.0, region_river_center(-82.0, config["hydrology"]["region_river_crossings_m"], config["hydrology"]["region_meander_amplitudes_m"])), (-64.0, -225.0), (-42.0, -300.0), (-18.0, -370.0)],
        [(92.0, region_river_center(92.0, config["hydrology"]["region_river_crossings_m"], config["hydrology"]["region_meander_amplitudes_m"])), (92.0, -225.0), (87.0, -300.0), (76.0, -365.0)],
    ), 1):
        points = [(x, y, terrain_z(data, x, y, 1024, 1024, 0.55)) for x, y in coordinates]
        canal = create_ribbon(f"REGION_IrrigationCanal_{index:02d}", points, 3.5, waterworks, materials["water"])
        canal["water_body"] = "irrigation_canal"
        canal["facility_type"] = "productive_waterwork"

    north = data.anchors["outpost_north"]
    north_z = terrain_z(data, *north, 1024, 1024)
    for index, (dx, dy, rot) in enumerate(((-32, -12, 0.15), (30, 0, -0.2), (-10, 34, 0.45))):
        add_house(assets, architecture, f"NorthHouse_{index+1:02d}", (north[0] + dx, north[1] + dy, north_z), 1.0, rot)
    place_asset(assets["tower"], "NorthOutpost_Tower", architecture, (north[0] + 58, north[1] - 10, north_z), scale=(0.8, 0.8, 0.8))

    main_bridge = data.anchors["bridge_main"]
    place_asset(assets["bridge"], "Bridge_Main", architecture, (main_bridge[0], main_bridge[1], 8.4), rotation=(0, 0, math.pi / 2), scale=(1.65, 1.4, 1.35))
    minor_bridge = data.anchors["bridge_minor"]
    place_asset(assets["bridge"], "Bridge_Minor", architecture, (minor_bridge[0], minor_bridge[1], 8.9), rotation=(0, 0, math.pi / 2), scale=(1.15, 1.05, 1.0))

    mine = data.anchors["mine"]
    mine_obj = place_asset(assets["mine"], "POI_Mine_Entrance", architecture, (mine[0], mine[1], terrain_z(data, *mine, 1024, 1024)), rotation=(0, 0, -0.25), scale=(1.5, 1.5, 1.5))
    mine_obj["poi_id"] = "mine"
    for index in range(7):
        place_asset(assets["rock"], f"Mine_Rock_{index:02d}", architecture, (mine[0] - 15 + index * 5, mine[1] + 8 + (index % 2) * 4, terrain_z(data, mine[0] - 15 + index * 5, mine[1] + 8 + (index % 2) * 4, 1024, 1024)), scale=(1.5, 1.2, 1.0))

    cave = data.anchors["cave_west"]
    cave_obj = place_asset(assets["mine"], "POI_CaveWest_Entrance", architecture, (cave[0], cave[1], terrain_z(data, *cave, 1024, 1024)), rotation=(0, 0, 0.55), scale=(1.25, 1.25, 1.15))
    cave_obj["poi_id"] = "cave_west"
    cave_obj["facility_type"] = "cave_exterior"

    camp = data.anchors["camp"]
    camp_obj = place_asset(assets["camp"], "POI_ReactiveCamp", architecture, (camp[0], camp[1], terrain_z(data, *camp, 1024, 1024)), rotation=(0, 0, -0.35), scale=(1.35, 1.35, 1.35))
    camp_obj["poi_id"] = "camp"
    camp_obj["state_variants"] = "occupied damaged rebuilding"

    observatory = data.anchors["observatory"]
    observatory_obj = place_asset(assets["observatory"], "POI_RegionalObservatory", architecture, (observatory[0], observatory[1], terrain_z(data, *observatory, 1024, 1024)), scale=(1.8, 1.8, 1.8))
    observatory_obj["poi_id"] = "observatory"
    observatory_obj["distinct_from_second_continent"] = True

    wastes = data.anchors["wastes"]
    for index, (dx, dy, rot) in enumerate(((0, 0, 0.2), (24, 18, -0.4), (-28, 26, 0.6))):
        ruin = place_asset(assets["ruin"], f"Wastes_Ruin_{index+1:02d}", architecture, (wastes[0] + dx, wastes[1] + dy, terrain_z(data, wastes[0] + dx, wastes[1] + dy, 1024, 1024)), rotation=(0, 0, rot), scale=(1.4, 1.4, 1.4))
        ruin["poi_id"] = "wastes" if index == 0 else "wastes_satellite"

    portal = data.anchors["portal_turbulent"]
    portal_z = terrain_z(data, *portal, 1024, 1024)
    vertices = [(-3.0, 0, 0), (3.0, 0, 0), (3.0, 0, 9.0), (-3.0, 0, 9.0)]
    portal_mesh = make_mesh_object("POI_TurbulentPortal_Surface", architecture, vertices, [(0, 1, 2, 3)])
    portal_mesh.location = (portal[0], portal[1], portal_z)
    portal_mesh.data.materials.append(materials["cyan_surface"])
    portal_mesh["poi_id"] = "portal_turbulent"
    portal_mesh["anomaly_portal"] = True
    place_asset(assets["arch"], "POI_TurbulentPortal_Arch", architecture, (portal[0], portal[1] + 0.4, portal_z), scale=(1.6, 1.2, 1.6))

    def near_routes(x, y, clearance=12.0):
        for polyline in route_polylines.values():
            for start, end in zip(polyline, polyline[1:]):
                if distance_to_segment((x, y), start, end) < clearance:
                    return True
        return False

    anchors = list(data.anchors.values())

    def scatter_points(count, seed, predicate, clearance=13.0):
        rng = random.Random(seed)
        points = []
        attempts = 0
        while len(points) < count and attempts < count * 40:
            attempts += 1
            x = rng.uniform(-500, 500)
            y = rng.uniform(-500, 500)
            if near_routes(x, y, clearance):
                continue
            if any(
                math.hypot(x - anchor[0], y - anchor[1]) < config["distribution"]["region"]["anchor_exclusion_m"]
                for anchor in anchors
            ):
                continue
            if any(
                minimum_x <= x <= maximum_x and minimum_y <= y <= maximum_y
                for minimum_x, minimum_y, maximum_x, maximum_y
                in config["distribution"]["region"]["reserved_rectangles_m"].values()
            ):
                continue
            row = int(np.clip((y / 1024 + 0.5) * (data.biome.shape[0] - 1), 0, data.biome.shape[0] - 1))
            col = int(np.clip((x / 1024 + 0.5) * (data.biome.shape[1] - 1), 0, data.biome.shape[1] - 1))
            if data.water[row, col] or not predicate(int(data.biome[row, col]), float(data.slope[row, col]), x, y):
                continue
            points.append((x, y, float(data.height[row, col]) - 0.1))
        return points

    nature = new_collection("REGION_NATURE_GEOMETRY_NODES", scene=scene)
    distribution = config["distribution"]["region"]
    exclusion = distribution["route_exclusion_m"]
    broadleaf_points = scatter_points(distribution["broadleaf_count"], 260924021, lambda biome, slope, x, y: biome in (0, 1, 3) and slope < 23 and x < 250, exclusion["broadleaf"])
    pine_points = scatter_points(distribution["pine_count"], 260924022, lambda biome, slope, x, y: biome in (1, 2) and (y > 20 or x < -170), exclusion["pine"])
    rock_points = scatter_points(distribution["rock_count"], 260924023, lambda biome, slope, x, y: slope > 13 or biome in (2, 4), exclusion["rock"])
    grass_points = scatter_points(distribution["grass_count"], 260924024, lambda biome, slope, x, y: biome in (0, 1, 3) and slope < 18, exclusion["grass"])
    create_scatter_points("SCATTER_Broadleaf", broadleaf_points, scatter_sources["broadleaf_tree"], nature, "GN_SCATTER_BROADLEAF", 0.78, 1.32)
    create_scatter_points("SCATTER_Pines", pine_points, scatter_sources["pine_tree"], nature, "GN_SCATTER_PINES", 0.78, 1.28)
    create_scatter_points("SCATTER_Rocks", rock_points, scatter_sources["rock"], nature, "GN_SCATTER_ROCKS", 0.65, 1.8)
    create_scatter_points("SCATTER_Grass", grass_points, scatter_sources["grass"], nature, "GN_SCATTER_GRASS", 0.7, 1.5)

    create_sun(scene, collections["lights"], 3.6, (0.58, -0.28, -0.78))
    create_camera("CAM_05_REGION_AEREA", scene, collections["cameras"], (720, -760, 930), (0, 0, 30), 52)
    create_camera("CAM_06_VALE_ENTREPOSTO", scene, collections["cameras"], (145, -505, 98), (0, -360, 24), 46)
    create_camera("CAM_07_BOSQUE_MINA", scene, collections["cameras"], (-470, -230, 115), (-340, -70, 45), 52)
    create_camera("CAM_08_GARGANTA", scene, collections["cameras"], (115, -45, 125), (-20, 115, 55), 56)
    create_camera("CAM_09_ACAMPAMENTO_OBSERVATORIO", scene, collections["cameras"], (420, -20, 180), (-10, 150, 65), 35)
    create_camera("CAM_10_ERMOS_ENTREPOSTO_NORTE", scene, collections["cameras"], (-495, 470, 185), (-120, 345, 80), 56)
    scene.camera = bpy.data.objects["CAM_05_REGION_AEREA"]


def build_turbulent(scene, collections, data, config, materials, assets, scatter_sources):
    configure_scene(scene, (0.035, 0.025, 0.06, 1), 0.3)
    scene["scale_contract"] = "1 BU = 1 m"
    scene["turbulent_bounds_m"] = "256 x 256"
    terrain = create_grid_mesh(
        "TURBULENT_Terrain",
        data.height,
        (-128, -128, 128, 128),
        collections["base"],
        materials["turbulent_ground"],
        biome_colors(data.biome, "turbulent"),
        target_samples=(129, 129),
    )
    terrain["terrain_chunk"] = False
    paths = new_collection("TURBULENT_PATHS", scene=scene)
    for route_id, route in data.routes.items():
        points = route_points(data, config, route)
        ribbon = create_ribbon(f"TURBULENT_ROUTE_{route_id}", points, route["width_m"], paths, materials["road"])
        ribbon["route_id"] = route_id

    markers = new_collection("TURBULENT_ANCHORS", scene=scene)
    for anchor_id, (x, y) in data.anchors.items():
        create_marker(
            f"TURBULENT_{anchor_id}", markers,
            (x, y, terrain_z(data, x, y, 256, 256, 1.0)),
            "anchor_id", anchor_id,
        )

    environment = new_collection("TURBULENT_ENVIRONMENT", scene=scene)
    portal_z = terrain_z(data, 0, 0, 256, 256)
    portal_points = []
    for index in range(65):
        angle = index / 64.0 * math.tau
        portal_points.append((math.cos(angle) * 8.0, 0.0, portal_z + 10.0 + math.sin(angle) * 10.0))
    portal = create_curve_tube("TURBULENT_PortalRing", portal_points, 0.72, environment, materials["cyan"], cyclic=True)
    portal["anomaly_portal"] = True
    surface_vertices = [(-7.0, 0.15, portal_z), (7.0, 0.15, portal_z), (7.0, 0.15, portal_z + 20.0), (-7.0, 0.15, portal_z + 20.0)]
    surface = make_mesh_object("TURBULENT_PortalSurface", environment, surface_vertices, [(0, 1, 2, 3)])
    surface.data.materials.append(materials["cyan_surface"])
    surface["anomaly_portal"] = True
    place_asset(assets["arch"], "TURBULENT_PortalArch", environment, (0, 1.2, portal_z), scale=(2.3, 1.7, 3.4))

    for index, (x, y, z, scale) in enumerate((
        (-42, 20, 35, 2.0), (38, -15, 29, 1.6), (60, 55, 42, 1.2),
        (-66, 72, 31, 1.3), (15, 78, 37, 1.1),
    )):
        fragment = place_asset(assets["rock"], f"TURBULENT_FloatingFragment_{index:02d}", environment, (x, y, z), rotation=(0.3 * index, 0.18 * index, 0.4 * index), scale=(scale, scale * 0.75, scale * 0.9))
        fragment["floating_fragment"] = True

    for index, (x, y, rotation) in enumerate(((-58, 38, 0.4), (-82, 63, -0.2), (57, 50, 0.7))):
        place_asset(assets["ruin"], f"TURBULENT_Ruin_{index:02d}", environment, (x, y, terrain_z(data, x, y, 256, 256)), rotation=(0, 0, rotation), scale=(1.0, 1.0, 1.0))
    for index, (x, y) in enumerate(((-70, -10), (-45, 68), (74, 18), (82, 82))):
        place_asset(assets["dead_tree"], f"TURBULENT_DeadTree_{index:02d}", environment, (x, y, terrain_z(data, x, y, 256, 256)), rotation=(0, 0, index * 0.7), scale=(1.2, 1.2, 1.2))

    roots = [
        [(-95, -35), (-65, -18), (-35, 5), (-8, 16)],
        [(100, -22), (70, -5), (45, 28), (10, 35)],
        [(-88, 110), (-58, 76), (-20, 58), (18, 42)],
    ]
    for index, root in enumerate(roots):
        points = [(x, y, terrain_z(data, x, y, 256, 256, 0.9)) for x, y in root]
        create_curve_tube(f"TURBULENT_Root_{index:02d}", points, 1.25, environment, materials["bark"])

    rng = random.Random(260924039)
    rock_points = []
    turbulent_distribution = config["distribution"]["turbulent"]
    for _ in range(turbulent_distribution["rock_attempts"]):
        x = rng.uniform(-120, 120)
        y = rng.uniform(-120, 120)
        if math.hypot(x, y) < turbulent_distribution["portal_exclusion_radius_m"]:
            continue
        rock_points.append((x, y, terrain_z(data, x, y, 256, 256)))
    create_scatter_points("TURBULENT_ScatterRocks", rock_points, scatter_sources["rock"], environment, "GN_SCATTER_TURBULENT_ROCKS", 0.45, 1.35)

    create_sun(scene, collections["lights"], 2.2, (0.75, -0.2, -1.05))
    for name, color, energy, location in (
        ("PortalCyan", hex_rgba(PALETTE["cyan"]), 1500.0, (0.0, -7.0, portal_z + 9.0)),
        ("RuinViolet", hex_rgba(PALETTE["violet"]), 850.0, (-48.0, 24.0, portal_z + 13.0)),
    ):
        light_data = bpy.data.lights.new(f"Light_{name}", type="POINT")
        light_data.color = color[:3]
        light_data.energy = energy
        light_data.shadow_soft_size = 7.0
        light_object = bpy.data.objects.new(f"Light_{name}", light_data)
        light_object.location = location
        collections["lights"].objects.link(light_object)
    create_camera("CAM_11_TURBULENTA_AEREA", scene, collections["cameras"], (195, -205, 235), (0, 12, 18), 52)
    create_camera("CAM_12_TURBULENTA_PORTAL", scene, collections["cameras"], (58, -92, 38), (0, 0, portal_z + 8), 48)
    scene.camera = bpy.data.objects["CAM_11_TURBULENTA_AEREA"]


def build_library_scene(scene, collections, materials):
    configure_scene(scene, (0.15, 0.13, 0.11, 1), 0.7)
    scene["scale_contract"] = "1 BU = 1 m"
    assets_collection = new_collection("LIBRARY_ASSETS", scene=scene)
    assets = build_asset_library(assets_collection, materials)
    sources, source_collection = make_scatter_sources(assets)
    create_sun(scene, collections["lights"], 3.0, (0.55, -0.3, -0.75))
    return assets, sources, source_collection


def build_validation(scene, collections, materials, assets):
    configure_scene(scene, (0.09, 0.08, 0.075, 1), 0.75)
    scene["scale_contract"] = "1 BU = 1 m"
    validation = new_collection("VALIDATION_SWATCHES", scene=scene)
    material_values = list(materials.items())[:18]
    for index, (material_name, material) in enumerate(material_values):
        row, col = divmod(index, 6)
        source = assets["rock"] if index % 2 == 0 else assets["wall"]
        obj = place_asset(source, f"VALIDATE_{material_name}", validation, ((col - 2.5) * 8.5, (1 - row) * 10.0, 0.0), scale=(0.65, 0.65, 0.65))
        # Validation swatches need their own mesh datablocks so assigning a
        # diagnostic material cannot alter the reusable library asset.
        obj.data = obj.data.copy()
        if obj.data.materials:
            obj.data.materials[0] = material
        else:
            obj.data.materials.append(material)
        obj["validation_swatch"] = material_name
    person_vertices = [(-0.3, -0.2, 0), (0.3, -0.2, 0), (0.3, 0.2, 0), (-0.3, 0.2, 0), (0, 0, 1.8)]
    person_faces = [(0, 1, 4), (1, 2, 4), (2, 3, 4), (3, 0, 4), (0, 3, 2, 1)]
    person = make_mesh_object("VALIDATE_HumanScale_1_8m", validation, person_vertices, person_faces)
    person.data.materials.append(materials["cobalt"])
    person["reference_height_m"] = 1.8
    create_sun(scene, collections["lights"], 3.5, (0.4, -0.35, -0.7))


def add_embedded_sources(config):
    text = bpy.data.texts.new("README_PROJETO_GAME")
    text.write(
        "MUNDO_MESTRE.blend\n"
        "Atlas: 1 BU = 1 km. Regiao e Turbulenta: 1 BU = 1 m.\n"
        "FONTES_PROCEDURAIS devem ser preservadas. Regeneracao nunca sobrescreve ACABAMENTO_MANUAL.\n"
        "Reinos, fronteiras e nomes politicos estao marcados como PROPOSTA.\n"
        "O pipeline deste arquivo nao escreve na demo Three.js nem a integra nesta etapa.\n"
    )
    config_text = bpy.data.texts.new("world_config.json")
    config_text.write(json.dumps(config, indent=2, ensure_ascii=False))
    for path in (
        TOOLS / "worldgen_core.py",
        TOOLS / "worldgen_io.py",
        TOOLS / "build_master.py",
        TOOLS / "export_world.py",
        TOOLS / "render_world.py",
        TOOLS / "regenerate_sources.py",
        TOOLS / "validate_blend.py",
        TOOLS / "validate_glbs.py",
        TOOLS / "validate_production.py",
        TOOLS / "verify_demo_integrity.py",
        TOOLS / "create_inventory.py",
    ):
        source = bpy.data.texts.new(path.name)
        source.write(path.read_text(encoding="utf-8"))


def create_workflow_contracts(config):
    for name in ("FONTES_PROCEDURAIS", "BASE_EDITAVEL", "ACABAMENTO_MANUAL", "PREPARACAO_EXPORTACAO"):
        collection = bpy.data.collections.new(name)
        collection.use_fake_user = True
        collection["workflow_contract"] = True
        collection["regeneration_policy"] = "never overwrite manual" if name == "ACABAMENTO_MANUAL" else "versioned"
    sources = bpy.data.collections["FONTES_PROCEDURAIS"]
    for kind in ("atlas", "region", "turbulent"):
        seed = config["seeds"][kind]
        versioned = bpy.data.collections.new(f"GEN_{kind.upper()}__seed_{seed}__v001")
        versioned["generation_kind"] = kind
        versioned["seed"] = seed
        versioned["version"] = 1
        versioned["source_data"] = f"//source/data/{kind}.npz"
        versioned["regeneration_policy"] = "append version; never overwrite BASE_EDITAVEL or ACABAMENTO_MANUAL"
        sources.children.link(versioned)
        marker = bpy.data.objects.new(f"SOURCE_SEED_{kind.upper()}", None)
        marker["seed"] = seed
        marker["generator"] = "worldgen_core.py"
        marker["status"] = "BASE_PROCEDURAL_PRESERVADA"
        versioned.objects.link(marker)


def main():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    config = load_config(ROOT / "config" / "world_config.json")
    atlas_data = generate_world_data(config, "atlas")
    region_data = generate_world_data(config, "region")
    turbulent_data = generate_world_data(config, "turbulent")
    materials = build_materials()

    atlas_scene = create_scene("ATLAS_MUNDO", first=True)
    region_scene = create_scene("REGIAO_COMERCIAL")
    turbulent_scene = create_scene("TURBULENTA_01")
    library_scene = create_scene("BIBLIOTECA_LOCAL")
    validation_scene = create_scene("VALIDACAO")

    atlas_collections = scene_collections(atlas_scene, "ATLAS")
    region_collections = scene_collections(region_scene, "REGION")
    turbulent_collections = scene_collections(turbulent_scene, "TURBULENT")
    library_collections = scene_collections(library_scene, "LIBRARY")
    validation_collections = scene_collections(validation_scene, "VALIDATION")

    assets, scatter_sources, _ = build_library_scene(library_scene, library_collections, materials)
    build_atlas(atlas_scene, atlas_collections, atlas_data, config, materials, assets)
    build_region(region_scene, region_collections, region_data, config, materials, assets, scatter_sources)
    build_turbulent(turbulent_scene, turbulent_collections, turbulent_data, config, materials, assets, scatter_sources)
    build_validation(validation_scene, validation_collections, materials, assets)
    create_workflow_contracts(config)
    add_embedded_sources(config)

    maps = ROOT / "exports" / "maps"
    maps.mkdir(parents=True, exist_ok=True)
    save_float_exr(maps / "atlas_height.exr", "DATA_AtlasHeight", atlas_data.height)
    save_float_exr(maps / "region_height.exr", "DATA_RegionHeight", region_data.height)
    save_float_exr(maps / "turbulent_height.exr", "DATA_TurbulentHeight", turbulent_data.height)

    bpy.context.window.scene = atlas_scene
    bpy.ops.file.pack_all()
    output = ROOT / "MUNDO_MESTRE.blend"
    bpy.ops.wm.save_as_mainfile(filepath=str(output), check_existing=False)
    print("MASTER_BUILD_OK", output, len(bpy.data.objects), len(bpy.data.materials))


if __name__ == "__main__":
    main()
