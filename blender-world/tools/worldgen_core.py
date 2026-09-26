"""Deterministic terrain and metadata generation shared by tests and Blender."""

from __future__ import annotations

from collections import deque
from dataclasses import dataclass
import copy
import json
import math
from pathlib import Path
from typing import Any

import numpy as np


@dataclass
class WorldData:
    kind: str
    seed: int
    height: np.ndarray
    land: np.ndarray
    water: np.ndarray
    moisture: np.ndarray
    slope: np.ndarray
    biome: np.ndarray
    anchors: dict[str, list[float]]
    routes: dict[str, dict[str, Any]]


def load_config(path: str | Path) -> dict[str, Any]:
    with Path(path).open("r", encoding="utf-8") as handle:
        config = json.load(handle)
    validate_config(config)
    return config


def validate_config(config: dict[str, Any]) -> None:
    if config.get("schema_version") != "1.0.0":
        raise ValueError("schema_version must be 1.0.0")
    if config.get("blender_version") != "5.0.1":
        raise ValueError("blender_version must be 5.0.1")
    seeds = config.get("seeds", {})
    if set(seeds) != {"atlas", "region", "turbulent"} or not all(
        isinstance(value, int) for value in seeds.values()
    ):
        raise ValueError("seeds must define integer atlas, region and turbulent values")

    region = config.get("region", {})
    samples = region.get("samples", [])
    chunks = region.get("chunks", [])
    if len(samples) != 2 or len(chunks) != 2:
        raise ValueError("region samples and chunks must have two values")
    if any((samples[index] - 1) % chunks[index] for index in range(2)):
        raise ValueError("region chunks require sample counts with shared borders")
    if region.get("size_m") != [1024, 1024]:
        raise ValueError("region size_m must be 1024 x 1024")

    turbulent = config.get("turbulent", {})
    if turbulent.get("size_m") != [256, 256]:
        raise ValueError("turbulent size_m must be 256 x 256")
    if any(value < 3 or value % 2 == 0 for value in turbulent.get("samples", [])):
        raise ValueError("turbulent samples must be odd and at least three")

    atlas = config.get("atlas", {})
    if atlas.get("size_km") != [220, 140]:
        raise ValueError("atlas size_km must be 220 x 140")
    realms = config.get("realms", [])
    if len(realms) != 6 or any(realm.get("status") != "PROPOSTA" for realm in realms):
        raise ValueError("exactly six provisional realms are required")


def _smoothstep(value: np.ndarray) -> np.ndarray:
    return value * value * (3.0 - 2.0 * value)


def _value_noise(shape: tuple[int, int], seed: int, scale: int) -> np.ndarray:
    height, width = shape
    cells_x = max(2, math.ceil((width - 1) / scale) + 1)
    cells_y = max(2, math.ceil((height - 1) / scale) + 1)
    rng = np.random.default_rng(seed)
    grid = rng.uniform(-1.0, 1.0, (cells_y + 1, cells_x + 1))

    x = np.linspace(0.0, cells_x - 1.0, width)
    y = np.linspace(0.0, cells_y - 1.0, height)
    xi = np.minimum(np.floor(x).astype(np.int32), cells_x - 1)
    yi = np.minimum(np.floor(y).astype(np.int32), cells_y - 1)
    xf = _smoothstep(x - xi)
    yf = _smoothstep(y - yi)

    g00 = grid[yi[:, None], xi[None, :]]
    g10 = grid[yi[:, None], xi[None, :] + 1]
    g01 = grid[yi[:, None] + 1, xi[None, :]]
    g11 = grid[yi[:, None] + 1, xi[None, :] + 1]
    top = g00 * (1.0 - xf[None, :]) + g10 * xf[None, :]
    bottom = g01 * (1.0 - xf[None, :]) + g11 * xf[None, :]
    return top * (1.0 - yf[:, None]) + bottom * yf[:, None]


def _fractal_noise(shape: tuple[int, int], seed: int) -> np.ndarray:
    smallest = max(4, min(shape) // 64)
    scales = [smallest * 16, smallest * 8, smallest * 4, smallest * 2, smallest]
    result = np.zeros(shape, dtype=np.float64)
    amplitude = 1.0
    total = 0.0
    for octave, scale in enumerate(scales):
        result += _value_noise(shape, seed + octave * 7919, max(2, scale)) * amplitude
        total += amplitude
        amplitude *= 0.52
    result /= total
    maximum = max(abs(float(result.min())), abs(float(result.max())), 1e-9)
    return result / maximum


def _slope_degrees(height: np.ndarray, step: float) -> np.ndarray:
    dz, dx = np.gradient(height, step, step)
    return np.degrees(np.arctan(np.hypot(dx, dz)))


def sample_height_bilinear(
    height: np.ndarray,
    x: float,
    y: float,
    size_x: float,
    size_y: float,
) -> float:
    """Sample a centered height field without depending on Blender."""
    rows, cols = height.shape
    col = float(np.clip((x / size_x + 0.5) * (cols - 1), 0.0, cols - 1.0))
    row = float(np.clip((y / size_y + 0.5) * (rows - 1), 0.0, rows - 1.0))
    col0 = int(math.floor(col))
    row0 = int(math.floor(row))
    col1 = min(col0 + 1, cols - 1)
    row1 = min(row0 + 1, rows - 1)
    factor_x = col - col0
    factor_y = row - row0
    first = height[row0, col0] * (1.0 - factor_x) + height[row0, col1] * factor_x
    second = height[row1, col0] * (1.0 - factor_x) + height[row1, col1] * factor_x
    return float(first * (1.0 - factor_y) + second * factor_y)


def region_river_center(
    x: np.ndarray | float,
    crossings: list[list[float]] | tuple[tuple[float, float], ...] | None = None,
    amplitudes: list[float] | tuple[float, ...] | None = None,
) -> np.ndarray | float:
    """River centerline passing exactly through both authored crossings."""
    scalar = np.isscalar(x)
    values = np.asarray(x, dtype=np.float64)
    crossings = crossings or ((-512.0, -166.0), (0.0, -145.0), (275.0, -118.0), (512.0, -94.0))
    nodes_x = np.asarray([point[0] for point in crossings], dtype=np.float64)
    nodes_y = np.asarray([point[1] for point in crossings], dtype=np.float64)
    center = np.interp(values, nodes_x, nodes_y)
    amplitudes = amplitudes or (9.0, -7.0, 6.0)
    for index, amplitude in enumerate(amplitudes):
        start_x, end_x = nodes_x[index], nodes_x[index + 1]
        selection = (values >= start_x) & (values <= end_x)
        factor = (values - start_x) / (end_x - start_x)
        center = np.where(selection, center + amplitude * np.sin(factor * math.pi), center)
    return float(center) if scalar else center


def build_route_polyline(
    data: WorldData,
    config: dict[str, Any],
    route: dict[str, Any],
    steps_per_segment: int = 16,
    lift: float = 0.45,
) -> list[tuple[float, float, float]]:
    """Build an editable route whose road surface obeys its sustained grade.

    The XY line follows authored anchors and a small deterministic lateral arc.
    The Z profile starts on the terrain and only raises low samples, creating
    bridge or embankment sections where the raw ground would exceed the route's
    declared grade. This keeps the route surface traversable without flattening
    the source terrain or changing manual collections.
    """
    if data.kind == "region":
        anchors = config["region_anchors"]
        size = 1024.0
    elif data.kind == "turbulent":
        anchors = config["turbulent_anchors"]
        size = 256.0
    else:
        raise ValueError(f"routes are unsupported for {data.kind}")

    horizontal: list[tuple[float, float]] = []
    for segment_index, (first_name, second_name) in enumerate(zip(route["anchors"], route["anchors"][1:])):
        first = anchors[first_name]
        second = anchors[second_name]
        for step in range(steps_per_segment + 1):
            if segment_index and step == 0:
                continue
            factor = step / steps_per_segment
            x = first[0] * (1.0 - factor) + second[0] * factor
            y = first[1] * (1.0 - factor) + second[1] * factor
            arc = math.sin(factor * math.pi) * math.sin((segment_index + 1) * 1.7) * 4.0
            delta_x = second[0] - first[0]
            delta_y = second[1] - first[1]
            length = max(math.hypot(delta_x, delta_y), 1.0)
            x += -delta_y / length * arc
            y += delta_x / length * arc
            horizontal.append((x, y))

    elevations = np.asarray(
        [sample_height_bilinear(data.height, x, y, size, size) + lift for x, y in horizontal],
        dtype=np.float64,
    )
    distances = np.asarray(
        [math.hypot(second[0] - first[0], second[1] - first[1]) for first, second in zip(horizontal, horizontal[1:])],
        dtype=np.float64,
    )
    max_rise = np.tan(math.radians(float(route["max_sustained_slope_deg"]))) * distances

    # Raising the lower endpoint is the minimum non-destructive grading that
    # satisfies each neighboring constraint. Repeating both directions
    # propagates a required bridge/embankment elevation along the full route.
    for _ in range(len(elevations) * 2):
        changed = False
        for index, allowed in enumerate(max_rise):
            difference = elevations[index + 1] - elevations[index]
            if difference > allowed + 1e-9:
                elevations[index] = elevations[index + 1] - allowed
                changed = True
            elif difference < -allowed - 1e-9:
                elevations[index + 1] = elevations[index] - allowed
                changed = True
        for index in range(len(max_rise) - 1, -1, -1):
            allowed = max_rise[index]
            difference = elevations[index + 1] - elevations[index]
            if difference > allowed + 1e-9:
                elevations[index] = elevations[index + 1] - allowed
                changed = True
            elif difference < -allowed - 1e-9:
                elevations[index + 1] = elevations[index] - allowed
                changed = True
        if not changed:
            break

    return [
        (float(x), float(y), float(z))
        for (x, y), z in zip(horizontal, elevations)
    ]


def _atlas_data(config: dict[str, Any]) -> WorldData:
    spec = config["atlas"]
    width, height = spec["samples"]
    x = np.linspace(-110.0, 110.0, width)
    z = np.linspace(-70.0, 70.0, height)
    xx, zz = np.meshgrid(x, z)
    noise = _fractal_noise((height, width), config["seeds"]["atlas"])

    main = 1.0 - (
        np.power(np.abs((xx + 36.0) / 61.0), 2.35)
        + np.power(np.abs((zz + 1.5) / 43.0), 2.15)
    )
    second = 1.0 - (
        np.power(np.abs((xx - 70.0) / 36.0), 2.25)
        + np.power(np.abs((zz - 7.0) / 26.0), 2.05)
    )
    coast_field = np.maximum(main, second) + noise * 0.19
    land = coast_field > 0.0

    main_ridge = np.exp(-np.square((zz - (0.31 * (xx + 30.0) + 5.0)) / 10.0))
    second_ridge = np.exp(-np.square((zz + 0.22 * (xx - 70.0) - 8.0) / 8.0))
    uplift = np.where(main >= second, main_ridge * 2.7, second_ridge * 2.2)
    terrain = np.clip(coast_field, 0.0, None) * (0.18 + 0.72 * (noise + 1.0)) + uplift
    terrain = np.where(land, terrain, -0.18 - np.minimum(np.abs(coast_field), 1.0) * 0.5)
    lake_mask = np.zeros((height, width), dtype=bool)
    for lake in config["hydrology"]["atlas_lakes"]:
        center_x, center_z = lake["center_km"]
        radius_x, radius_z = lake["radii_km"]
        lake_mask |= (
            np.square((xx - center_x) / radius_x)
            + np.square((zz - center_z) / radius_z)
        ) <= 1.0
    lake_mask &= land
    land = land & ~lake_mask
    terrain = np.where(lake_mask, -0.06, terrain)
    terrain = terrain.astype(np.float32)
    water = ~land
    moisture = np.clip(0.62 + _fractal_noise((height, width), config["seeds"]["atlas"] + 53) * 0.3 - np.maximum(terrain, 0) * 0.06, 0.0, 1.0)
    step = max(spec["size_km"][0] / (width - 1), spec["size_km"][1] / (height - 1))
    slope = _slope_degrees(terrain, step).astype(np.float32)

    biome = np.zeros((height, width), dtype=np.uint8)
    biome[(terrain > 0.65) & (terrain <= 1.7)] = 1
    biome[terrain > 1.7] = 2
    biome[(moisture > 0.72) & (terrain < 0.7) & land] = 3
    biome[(moisture < 0.38) & (terrain < 1.4) & land] = 4
    biome[(terrain >= 0.0) & (terrain < 0.16)] = 5
    biome[water] = 5

    anchors = {
        "region_commercial": [-31.0, -12.0],
        "observatory_second_continent": [72.0, 15.0],
        "expedition_coast": [48.0, -3.0],
    }
    return WorldData(
        kind="atlas",
        seed=config["seeds"]["atlas"],
        height=terrain,
        land=land,
        water=water,
        moisture=moisture.astype(np.float32),
        slope=slope,
        biome=biome,
        anchors=anchors,
        routes={},
    )


def _region_data(config: dict[str, Any]) -> WorldData:
    spec = config["region"]
    width, height = spec["samples"]
    x = np.linspace(-512.0, 512.0, width)
    z = np.linspace(-512.0, 512.0, height)
    xx, zz = np.meshgrid(x, z)
    noise = _fractal_noise((height, width), config["seeds"]["region"])

    north_rise = 11.0 + (zz + 512.0) * 0.034
    west_hills = 30.0 * np.exp(-np.square((xx + 330.0) / 150.0))
    east_hills = 22.0 * np.exp(-np.square((xx - 350.0) / 165.0))
    northern_massif = 44.0 * np.exp(-np.square((zz - 230.0) / 190.0))
    gorge_gap = 36.0 * np.exp(-np.square((xx + 25.0) / 90.0)) * np.exp(-np.square((zz - 130.0) / 210.0))
    terrain = north_rise + west_hills + east_hills + northern_massif - gorge_gap + noise * 10.0

    hydrology = config["hydrology"]
    river_center = region_river_center(
        xx,
        hydrology["region_river_crossings_m"],
        hydrology["region_meander_amplitudes_m"],
    )
    river_distance = np.abs(zz - river_center)
    river_carve = np.exp(-np.square(river_distance / hydrology["river_carve_width_m"]))
    terrain = terrain * (1.0 - river_carve * 0.82) + (spec["water_level_m"] - 2.6) * river_carve * 0.82
    terrain = terrain.astype(np.float32)
    # The authored river crosses the complete region and reaches both map
    # boundaries. Keeping the water mask independent from small terrain noise
    # prevents accidental dry gaps after regeneration.
    water = river_distance <= hydrology["river_half_width_m"]
    land = ~water
    moisture_noise = _fractal_noise((height, width), config["seeds"]["region"] + 61)
    moisture = np.clip(0.36 + 0.52 * np.exp(-river_distance / 85.0) + moisture_noise * 0.22, 0.0, 1.0)
    step = spec["size_m"][0] / (width - 1)
    slope = _slope_degrees(terrain, step).astype(np.float32)

    biome = np.zeros((height, width), dtype=np.uint8)
    biome[(terrain > 36.0) & (terrain <= 68.0)] = 1
    biome[(terrain > 68.0) | (slope > 26.0)] = 2
    biome[(moisture > 0.68) & (terrain < 38.0)] = 3
    biome[(moisture < 0.31) & (terrain > 38.0)] = 4
    biome[water] = 5

    return WorldData(
        kind="region",
        seed=config["seeds"]["region"],
        height=terrain,
        land=land,
        water=water,
        moisture=moisture.astype(np.float32),
        slope=slope,
        biome=biome,
        anchors=copy.deepcopy(config["region_anchors"]),
        routes=copy.deepcopy(config["region_routes"]),
    )


def _turbulent_data(config: dict[str, Any]) -> WorldData:
    spec = config["turbulent"]
    width, height = spec["samples"]
    x = np.linspace(-128.0, 128.0, width)
    z = np.linspace(-128.0, 128.0, height)
    xx, zz = np.meshgrid(x, z)
    noise = _fractal_noise((height, width), config["seeds"]["turbulent"])
    radial = np.hypot(xx, zz)
    ravine = 12.0 * np.exp(-np.square((xx - 15.0 * np.sin(zz / 35.0)) / 24.0))
    rings = np.sin(radial / 13.0 + noise * 1.7) * 4.0
    terrain = 18.0 + noise * 15.0 + rings - ravine
    terrain += 8.0 * np.exp(-np.square(radial / 48.0))
    terrain = terrain.astype(np.float32)
    land = np.ones((height, width), dtype=bool)
    water = np.zeros((height, width), dtype=bool)
    moisture = np.clip(0.38 + _fractal_noise((height, width), config["seeds"]["turbulent"] + 97) * 0.25, 0.0, 1.0)
    step = spec["size_m"][0] / (width - 1)
    slope = _slope_degrees(terrain, step).astype(np.float32)
    biome = np.full((height, width), 6, dtype=np.uint8)
    routes = {
        "anomalous_crossing": {
            "anchors": ["entry", "portal_core", "objective_ruins", "exit_east"],
            "width_m": 3.0,
            "max_sustained_slope_deg": 18.0,
            "max_peak_slope_deg": 28.0,
            "risk": "turbulenta",
        },
        "alternate_exit": {
            "anchors": ["portal_core", "exit_west"],
            "width_m": 2.5,
            "max_sustained_slope_deg": 20.0,
            "max_peak_slope_deg": 28.0,
            "risk": "turbulenta",
        },
    }
    return WorldData(
        kind="turbulent",
        seed=config["seeds"]["turbulent"],
        height=terrain,
        land=land,
        water=water,
        moisture=moisture.astype(np.float32),
        slope=slope,
        biome=biome,
        anchors=copy.deepcopy(config["turbulent_anchors"]),
        routes=routes,
    )


def generate_world_data(config: dict[str, Any], kind: str) -> WorldData:
    validate_config(config)
    if kind == "atlas":
        return _atlas_data(config)
    if kind == "region":
        return _region_data(config)
    if kind == "turbulent":
        return _turbulent_data(config)
    raise ValueError(f"unknown world kind: {kind}")


def chunk_heightfield(height: np.ndarray, chunks_x: int, chunks_y: int) -> dict[tuple[int, int], np.ndarray]:
    rows, cols = height.shape
    if (cols - 1) % chunks_x or (rows - 1) % chunks_y:
        raise ValueError("heightfield cannot be divided into chunks with shared borders")
    stride_x = (cols - 1) // chunks_x
    stride_y = (rows - 1) // chunks_y
    return {
        (col, row): height[
            row * stride_y : (row + 1) * stride_y + 1,
            col * stride_x : (col + 1) * stride_x + 1,
        ]
        for row in range(chunks_y)
        for col in range(chunks_x)
    }


def count_landmasses(mask: np.ndarray, min_cells: int = 1) -> int:
    visited = np.zeros(mask.shape, dtype=bool)
    count = 0
    rows, cols = mask.shape
    for row, col in zip(*np.nonzero(mask & ~visited)):
        if visited[row, col]:
            continue
        queue = deque([(int(row), int(col))])
        visited[row, col] = True
        size = 0
        while queue:
            current_row, current_col = queue.popleft()
            size += 1
            for delta_row, delta_col in ((-1, 0), (1, 0), (0, -1), (0, 1)):
                next_row = current_row + delta_row
                next_col = current_col + delta_col
                if (
                    0 <= next_row < rows
                    and 0 <= next_col < cols
                    and mask[next_row, next_col]
                    and not visited[next_row, next_col]
                ):
                    visited[next_row, next_col] = True
                    queue.append((next_row, next_col))
        if size >= min_cells:
            count += 1
    return count


def flow_accumulation_d8(height: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    rows, cols = height.shape
    receiver = np.full((rows, cols, 2), -1, dtype=np.int32)
    indegree = np.zeros((rows, cols), dtype=np.int32)
    offsets = [
        (-1, -1), (-1, 0), (-1, 1),
        (0, -1), (0, 1),
        (1, -1), (1, 0), (1, 1),
    ]
    for row in range(rows):
        for col in range(cols):
            best = float(height[row, col])
            target = None
            for delta_row, delta_col in offsets:
                next_row = row + delta_row
                next_col = col + delta_col
                if 0 <= next_row < rows and 0 <= next_col < cols:
                    candidate = float(height[next_row, next_col])
                    if candidate < best:
                        best = candidate
                        target = (next_row, next_col)
            if target is not None:
                receiver[row, col] = target
                indegree[target] += 1

    accumulation = np.ones((rows, cols), dtype=np.float64)
    queue = deque((row, col) for row in range(rows) for col in range(cols) if indegree[row, col] == 0)
    while queue:
        row, col = queue.popleft()
        target_row, target_col = receiver[row, col]
        if target_row >= 0:
            accumulation[target_row, target_col] += accumulation[row, col]
            indegree[target_row, target_col] -= 1
            if indegree[target_row, target_col] == 0:
                queue.append((int(target_row), int(target_col)))
    return accumulation, receiver


def build_export_manifest(config: dict[str, Any], data: dict[str, WorldData]) -> dict[str, Any]:
    manifest: dict[str, Any] = {
        "schema_version": "1.0.0",
        "generator": "Projeto Game Blender World",
        "blender_version": config["blender_version"],
        "seeds": copy.deepcopy(config["seeds"]),
        "status": "PREPARACAO_FUTURA_SEM_INTEGRACAO_NA_DEMO",
        "scenes": {},
    }
    if "atlas" in data:
        manifest["scenes"]["ATLAS_MUNDO"] = {
            "unit_meters": 1000.0,
            "bounds_km": [-110.0, -70.0, 110.0, 70.0],
            "heightmap_exr": "maps/atlas_height.exr",
            "biome_mask": "maps/atlas_biome.png",
            "landmarks": copy.deepcopy(data["atlas"].anchors),
            "realms": copy.deepcopy(config["realms"]),
            "water_bodies": {
                "ocean": {"classification": "open_ocean"},
                "lakes": copy.deepcopy(config["hydrology"]["atlas_lakes"]),
            },
            "glb": "atlas/atlas_mundo.glb",
        }
    if "region" in data:
        chunks_x, chunks_y = config["region"]["chunks"]
        chunks = [
            {
                "id": f"region_{col}_{row}",
                "file": f"region/chunks/region_{col}_{row}.glb",
                "col": col,
                "row": row,
            }
            for row in range(chunks_y)
            for col in range(chunks_x)
        ]
        manifest["scenes"]["REGIAO_COMERCIAL"] = {
            "unit_meters": 1.0,
            "bounds_m": [-512.0, -512.0, 512.0, 512.0],
            "chunks": chunks,
            "heightmap_exr": "maps/region_height.exr",
            "heightmap_png16": "maps/region_height.png",
            "biome_mask": "maps/region_biome.png",
            "water_mask": "maps/region_water.png",
            "water_bodies": {
                "main_river": {
                    "classification": "open_boundary_to_boundary",
                    "crossings_m": copy.deepcopy(config["hydrology"]["region_river_crossings_m"]),
                },
                "lakes": [],
            },
            "anchors": copy.deepcopy(data["region"].anchors),
            "routes": copy.deepcopy(data["region"].routes),
            "instances": "region/instances.json",
        }
    if "turbulent" in data:
        manifest["scenes"]["TURBULENTA_01"] = {
            "unit_meters": 1.0,
            "bounds_m": [-128.0, -128.0, 128.0, 128.0],
            "heightmap_exr": "maps/turbulent_height.exr",
            "anchors": copy.deepcopy(data["turbulent"].anchors),
            "routes": copy.deepcopy(data["turbulent"].routes),
            "glb": "turbulent/turbulenta_01.glb",
        }
    return manifest
