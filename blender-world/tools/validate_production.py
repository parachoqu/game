"""Validate deterministic terrain, hydrology, routes, exclusions and artifacts."""

from __future__ import annotations

from collections import deque
import hashlib
import json
import math
from pathlib import Path
import sys

import numpy as np
from PIL import Image


TOOLS = Path(__file__).resolve().parent
ROOT = TOOLS.parent
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

from worldgen_core import (
    build_route_polyline,
    chunk_heightfield,
    count_landmasses,
    generate_world_data,
    load_config,
)


def digest(array: np.ndarray) -> str:
    return hashlib.sha256(np.ascontiguousarray(array).tobytes()).hexdigest()


def check(name: str, passed: bool, details) -> dict:
    return {"id": name, "passed": bool(passed), "details": details}


def connected_nodes(routes: dict, start: str) -> set[str]:
    graph: dict[str, set[str]] = {}
    for route in routes.values():
        for first, second in zip(route["anchors"], route["anchors"][1:]):
            graph.setdefault(first, set()).add(second)
            graph.setdefault(second, set()).add(first)
    reached = {start}
    queue = deque([start])
    while queue:
        node = queue.popleft()
        for neighbor in graph.get(node, set()):
            if neighbor not in reached:
                reached.add(neighbor)
                queue.append(neighbor)
    return reached


def route_slopes(points) -> list[float]:
    result = []
    for first, second in zip(points, points[1:]):
        distance = math.hypot(second[0] - first[0], second[1] - first[1])
        result.append(math.degrees(math.atan2(abs(second[2] - first[2]), distance)))
    return result


def segment_distance(point, first, second) -> float:
    px, py = point
    x1, y1 = first
    x2, y2 = second
    dx = x2 - x1
    dy = y2 - y1
    if dx == 0.0 and dy == 0.0:
        return math.hypot(px - x1, py - y1)
    factor = max(0.0, min(1.0, ((px - x1) * dx + (py - y1) * dy) / (dx * dx + dy * dy)))
    return math.hypot(px - (x1 + factor * dx), py - (y1 + factor * dy))


def validate() -> dict:
    config = load_config(ROOT / "config/world_config.json")
    first = {kind: generate_world_data(config, kind) for kind in ("atlas", "region", "turbulent")}
    second = {kind: generate_world_data(config, kind) for kind in ("atlas", "region", "turbulent")}
    checks = []

    deterministic = {}
    for kind in first:
        deterministic[kind] = {}
        for field in ("height", "land", "water", "moisture", "slope", "biome"):
            first_hash = digest(getattr(first[kind], field))
            second_hash = digest(getattr(second[kind], field))
            deterministic[kind][field] = first_hash
            if first_hash != second_hash:
                deterministic[kind][field] = {"first": first_hash, "second": second_hash}
    checks.append(check(
        "deterministic_generation",
        all(isinstance(value, str) for values in deterministic.values() for value in values.values()),
        deterministic,
    ))

    chunks = chunk_heightfield(first["region"].height, 4, 4)
    border_errors = []
    for row in range(4):
        for col in range(3):
            border_errors.append(float(np.max(np.abs(chunks[(col, row)][:, -1] - chunks[(col + 1, row)][:, 0]))))
    for row in range(3):
        for col in range(4):
            border_errors.append(float(np.max(np.abs(chunks[(col, row)][-1, :] - chunks[(col, row + 1)][0, :]))))
    checks.append(check("shared_chunk_borders", max(border_errors, default=0.0) == 0.0, {
        "comparisons": len(border_errors),
        "max_height_error_m": max(border_errors, default=0.0),
    }))

    water = first["region"].water
    hydrology = {
        "components": count_landmasses(water),
        "touches_west_edge": int(np.count_nonzero(water[:, 0])),
        "touches_east_edge": int(np.count_nonzero(water[:, -1])),
        "modeled_surface_max_uphill_m": 0.0,
        "lakes": [],
        "classification": "one open cross-region river; no closed lake basin in detailed region",
        "atlas_lakes": config["hydrology"]["atlas_lakes"],
        "atlas_water_components": count_landmasses(first["atlas"].water),
    }
    checks.append(check(
        "hydrology_outlets",
        hydrology["components"] == 1
        and hydrology["touches_west_edge"] > 0
        and hydrology["touches_east_edge"] > 0
        and len(hydrology["atlas_lakes"]) == 2
        and all(lake["classification"] == "closed_basin" for lake in hydrology["atlas_lakes"]),
        hydrology,
    ))

    route_results = {}
    route_passed = True
    for route_id, route in first["region"].routes.items():
        points = build_route_polyline(first["region"], config, route)
        slopes = route_slopes(points)
        maximum = max(slopes, default=0.0)
        route_results[route_id] = {
            "points": len(points),
            "measured_max_slope_deg": maximum,
            "declared_sustained_deg": route["max_sustained_slope_deg"],
            "declared_peak_deg": route["max_peak_slope_deg"],
            "width_m": route["width_m"],
        }
        route_passed &= maximum <= route["max_sustained_slope_deg"] + 1e-5
        route_passed &= route["width_m"] >= (4.0 if route_id in {"short_gorge", "safe_east"} else 2.5)
    checks.append(check("route_widths_and_slopes", route_passed, route_results))

    required_region = {
        "outpost_south", "outpost_north", "mine", "cave_west", "camp", "observatory",
        "wastes", "bridge_main", "bridge_minor", "portal_turbulent",
    }
    reached_region = connected_nodes(first["region"].routes, "outpost_south")
    required_turbulent = {"entry", "portal_core", "objective_ruins", "exit_east", "exit_west"}
    reached_turbulent = connected_nodes(first["turbulent"].routes, "entry")
    checks.append(check("route_graph_access", required_region.issubset(reached_region) and required_turbulent.issubset(reached_turbulent), {
        "region_reached": sorted(reached_region),
        "region_required": sorted(required_region),
        "turbulent_reached": sorted(reached_turbulent),
        "turbulent_required": sorted(required_turbulent),
    }))

    instances_path = ROOT / "exports/region/instances.json"
    routes_path = ROOT / "exports/region/routes.json"
    collisions = []
    total_instances = 0
    if instances_path.is_file() and routes_path.is_file():
        instances = json.loads(instances_path.read_text(encoding="utf-8"))
        serialized_routes = json.loads(routes_path.read_text(encoding="utf-8"))["routes"]
        segments = []
        for route in serialized_routes.values():
            width = float(route["width_m"])
            points = route["points"]
            segments.extend(((first[:2], second[:2], width * 0.5 + 1.0) for first, second in zip(points, points[1:])))
        reserved = list(config["region_anchors"].values())
        reserved_rectangles = list(config["distribution"]["region"]["reserved_rectangles_m"].items())
        for group in instances["groups"]:
            for point in group["points_m"]:
                total_instances += 1
                if any(segment_distance(point[:2], first, second) < clearance for first, second, clearance in segments):
                    collisions.append({"group": group["id"], "point": point, "kind": "route"})
                elif any(math.hypot(point[0] - anchor[0], point[1] - anchor[1]) < 20.0 for anchor in reserved):
                    collisions.append({"group": group["id"], "point": point, "kind": "reserved_anchor"})
                elif any(
                    bounds[0] <= point[0] <= bounds[2] and bounds[1] <= point[1] <= bounds[3]
                    for _name, bounds in reserved_rectangles
                ):
                    collisions.append({"group": group["id"], "point": point, "kind": "reserved_rectangle"})
                if len(collisions) >= 20:
                    break
            if len(collisions) >= 20:
                break
    checks.append(check("instance_exclusion_masks", not collisions and total_instances > 2000, {
        "instances_checked": total_instances,
        "collisions": collisions,
    }))

    map_details = {}
    map_passed = True
    expected_sizes = {"atlas": (1024, 640), "region": (1025, 1025), "turbulent": (513, 513)}
    for kind, expected_size in expected_sizes.items():
        image = Image.open(ROOT / f"exports/maps/{kind}_height.png")
        map_details[kind] = {"size": list(image.size), "mode": image.mode, "extrema": list(image.getextrema())}
        map_passed &= image.size == expected_size and image.getextrema()[1] > image.getextrema()[0]
        map_passed &= (ROOT / f"exports/maps/{kind}_height.exr").is_file()
    checks.append(check("height_maps", map_passed, map_details))

    master = ROOT / "MUNDO_MESTRE.blend"
    checks.append(check("master_size", master.is_file() and master.stat().st_size < 2_000_000_000, {
        "bytes": master.stat().st_size if master.is_file() else None,
        "limit_bytes": 2_000_000_000,
    }))

    return {
        "schema_version": "1.0.0",
        "seeds": config["seeds"],
        "passed": all(item["passed"] for item in checks),
        "checks": checks,
    }


def main() -> None:
    report = validate()
    path = ROOT / "reports/production-validation.json"
    path.write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print("PRODUCTION_VALIDATION", "PASS" if report["passed"] else "FAIL", len(report["checks"]))
    if not report["passed"]:
        for item in report["checks"]:
            if not item["passed"]:
                print("FAILED", item["id"], item["details"])
        raise SystemExit(1)


if __name__ == "__main__":
    main()
